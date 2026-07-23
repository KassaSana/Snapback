#include "doctest_wrapper.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/settings.hpp"
#include "app/state.hpp"
#include "util/logger.hpp"

using namespace snapback;

namespace {

class OneShotHook final : public InputHook {
public:
    void run(InputCallback on_event) override {
        CaptureEvent event;
        event.event_type = EventType::KeyPress;
        event.timestamp_secs = 1.0;
        event.app_name = "Cursor";
        event.window_title = "state.cpp - Snapback";
        on_event(event);
        emitted_.store(true, std::memory_order_release);

        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void stop() override { running_.store(false, std::memory_order_relaxed); }

    bool emitted() const { return emitted_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> running_{true};
    std::atomic<bool> emitted_{false};
};

class ReturningHook final : public InputHook {
public:
    void run(InputCallback) override { returned_.store(true, std::memory_order_release); }
    void stop() override {}

    bool returned() const { return returned_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> returned_{false};
};

CaptureEvent ev(EventType type, double ts, const char* app = "Cursor",
                const char* title = "state.cpp - Snapback") {
    CaptureEvent e;
    e.event_type = type;
    e.timestamp_secs = ts;
    e.app_name = app;
    e.window_title = title;
    return e;
}

std::unique_ptr<AppState> make_state() {
    auto storage = Storage::open_memory();
    if (!storage) throw std::runtime_error("failed to open in-memory storage");
    return std::make_unique<AppState>(std::move(*storage));
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_app_state_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cells;
    std::istringstream in(line);
    std::string cell;
    while (std::getline(in, cell, ',')) cells.push_back(cell);
    return cells;
}

bool contains_log(const DiagnosticsSnapshot& diagnostics, const std::string& text) {
    return std::any_of(diagnostics.recent_logs.begin(), diagnostics.recent_logs.end(),
                       [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

// Value of `column` in the final data row of a CSV. Used instead of substring-matching a
// row, which silently matches whichever column happens to share the value.
double last_csv_column(const std::string& csv, const std::string& column) {
    std::istringstream in(csv);
    std::string header_line;
    if (!std::getline(in, header_line)) return std::numeric_limits<double>::quiet_NaN();
    const auto header = split_csv_line(header_line);
    const auto it = std::find(header.begin(), header.end(), column);
    if (it == header.end()) return std::numeric_limits<double>::quiet_NaN();
    const auto index = static_cast<std::size_t>(std::distance(header.begin(), it));

    std::string line;
    std::string last;
    while (std::getline(in, line)) {
        if (!line.empty()) last = line;
    }
    const auto cells = split_csv_line(last);
    if (index >= cells.size()) return std::numeric_limits<double>::quiet_NaN();
    return std::stod(cells[index]);
}

}  // namespace

TEST_CASE("AppState idle wiring goes AFK after the threshold and wakes on input") {
    auto state = make_state();
    const std::int64_t t = kDefaultIdleThresholdMs;

    // First step seeds the clock; input keeps us active.
    CHECK(state->update_idle_for_test(0, /*had_input=*/true) == IdleTransition::None);
    CHECK_FALSE(state->is_idle());

    // No input across the threshold -> AFK, exactly one WentIdle edge.
    CHECK(state->update_idle_for_test(t - 1, false) == IdleTransition::None);
    CHECK(state->update_idle_for_test(t, false) == IdleTransition::WentIdle);
    CHECK(state->is_idle());
    CHECK(state->update_idle_for_test(t + 500, false) == IdleTransition::None);  // no repeat

    // Input wakes us.
    CHECK(state->update_idle_for_test(t + 600, true) == IdleTransition::WokeUp);
    CHECK_FALSE(state->is_idle());
}

TEST_CASE("AppState binds Pomodoro to an active session and exposes transition edges") {
    auto state = make_state();
    CHECK_THROWS_WITH(state->start_pomodoro_for_test(0), "no active session");

    const auto session = state->start_session("Finish Pomodoro wiring", FocusMode::Normal);
    const auto started = state->start_pomodoro_for_test(100);
    CHECK(started.running);
    CHECK(started.phase == PomodoroPhase::Work);
    CHECK(started.remaining_ms == 25 * 60 * 1000);

    CHECK_FALSE(state->update_pomodoro_for_test(100 + 25 * 60 * 1000 - 1).has_value());
    const auto transition = state->update_pomodoro_for_test(100 + 25 * 60 * 1000);
    REQUIRE(transition.has_value());
    CHECK(transition->phase == PomodoroPhase::ShortBreak);
    CHECK(transition->completed_work_intervals == 1);
    CHECK_FALSE(state->update_pomodoro_for_test(100 + 25 * 60 * 1000).has_value());

    state->stop_session(session.session_id);
    const auto stopped = state->pomodoro_status();
    CHECK_FALSE(stopped.running);
    CHECK(stopped.phase == PomodoroPhase::Work);
    CHECK(stopped.completed_work_intervals == 0);
}

TEST_CASE("AppState focus_summary aggregates persisted predictions") {
    auto state = make_state();
    auto session = state->start_session("Write tests", FocusMode::Deep);
    // Drive a few events far enough apart to clear the 1s prediction throttle.
    for (int i = 0; i < 4; ++i) {
        state->process_event_for_test(ev(EventType::KeyPress, 1.0 + i * 2.0));
    }
    const auto summary = state->focus_summary(100);
    CHECK(summary.sample_count >= 1);
    CHECK(summary.avg_focus_score >= 0.0);
    CHECK(summary.peak_focus_score >= summary.avg_focus_score);
    state->stop_session(session.session_id);
}

TEST_CASE("AppState freezes prediction generation while idle") {
    auto state = make_state();
    // A normal event produces a prediction.
    state->process_event_for_test(ev(EventType::KeyPress, 1.0));
    CHECK(state->latest_prediction().has_value());

    // Force AFK, then the next event must NOT overwrite/produce a prediction.
    state->update_idle_for_test(0, true);
    state->update_idle_for_test(kDefaultIdleThresholdMs, false);
    REQUIRE(state->is_idle());
    const auto before = state->latest_prediction()->timestamp;
    state->process_event_for_test(ev(EventType::KeyPress, 100.0));
    CHECK(state->latest_prediction()->timestamp == before);  // unchanged: no new prediction
}

TEST_CASE("AppState starts and stops sessions through storage") {
    auto state = make_state();
    auto session = state->start_session("Ship phase five", FocusMode::Deep);

    CHECK(session.status == "ACTIVE");
    CHECK(session.focus_mode == "deep");
    REQUIRE(state->active_session().has_value());
    CHECK(state->active_session()->session_id == session.session_id);

    state->stop_session(session.session_id);
    CHECK(state->active_session() == std::nullopt);

    TempDir temp;
    const auto exported = state->export_training_data(temp.path, session.session_id);
    CHECK(exported.label_count == 1);
    const auto labels = read_file(temp.path / "labels.csv");
    CHECK(labels.find(",auto,") != std::string::npos);
    CHECK(labels.find("inferred from session recap") != std::string::npos);
}

TEST_CASE("AppState writes a real elapsed time into exported features") {
    // Regression guard for the bug the unit tests missed: every FeatureExtractor test used
    // reset_for_session(explicit), while start_session passed nullopt — so feature[0] was
    // 0.0 in every row ever persisted and every training CSV, and nothing noticed. This
    // asserts the production path end to end, through export, where the model reads it.
    auto state = make_state();
    auto session = state->start_session("Ship the extractor fix", FocusMode::Normal);

    // Events are spaced far enough apart to clear the ~1/sec prediction throttle, so each
    // one produces a persisted feature row.
    state->process_event_for_test(ev(EventType::WindowFocusChange, 1000.0, "Cursor"));
    state->process_event_for_test(ev(EventType::KeyPress, 1002.0, "Cursor"));
    state->process_event_for_test(ev(EventType::KeyPress, 1045.0, "Cursor"));
    state->stop_session(session.session_id);

    TempDir temp;
    const auto exported = state->export_training_data(temp.path, session.session_id);
    REQUIRE(exported.feature_count > 0);

    const auto features = read_file(temp.path / "features.csv");
    // Read the named column explicitly rather than substring-matching the row: a plain
    // find(",45,") also matches time_in_current_app, which is 45 here too, so it passed
    // even with the bug present. Verified by reintroducing the bug and watching this fail.
    CHECK(last_csv_column(features, "seconds_since_session_start") == doctest::Approx(45.0));
}

TEST_CASE("AppState accepts an injected logger and stays silent on the happy path") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info);
    auto state = std::make_unique<AppState>(std::move(*storage), std::filesystem::path{}, &logger);

    auto session = state->start_session("Ship phase five", FocusMode::Deep);
    state->stop_session(session.session_id);

    CHECK(state->active_session() == std::nullopt);
    // Normal stop/save-label path never warns; the injected logger only speaks up on the
    // failure branch this same constructor param is wired to (storage.cpp's prune path
    // is the one exercised directly in test_storage.cpp).
    CHECK(log_out.str().empty());
}

TEST_CASE("AppState processes synthetic events into predictions and persisted rows") {
    auto state = make_state();
    auto session = state->start_session("Implement classifier", FocusMode::Normal);

    state->process_event_for_test(ev(EventType::WindowFocusChange, 100.0));
    state->process_event_for_test(ev(EventType::KeyPress, 101.2));
    state->process_event_for_test(ev(EventType::KeyPress, 102.5));

    auto latest = state->latest_prediction();
    REQUIRE(latest.has_value());
    CHECK(latest->session_id == session.session_id);
    CHECK(latest->focus_state != "");
    CHECK(latest->goal_alignment >= 0.0);

    auto history = state->prediction_history(10);
    CHECK(history.size() >= 1);

    auto recap = state->session_recap(session.session_id);
    CHECK(recap.session_id == session.session_id);
    CHECK(recap.avg_focus_score > 0.0);
}

TEST_CASE("AppState labels and export training data") {
    auto state = make_state();
    auto session = state->start_session("Export from app state", FocusMode::Recovery);
    state->process_event_for_test(ev(EventType::WindowFocusChange, 200.0));
    state->submit_label(FocusLabel::Productive, "manual", "steady");

    TempDir temp;
    auto exported = state->export_training_data(temp.path, session.session_id);
    CHECK(exported.feature_count >= 1);
    CHECK(exported.label_count == 1);

    const auto labels = read_file(temp.path / "labels.csv");
    CHECK(labels.find(",1,manual,") != std::string::npos);
    CHECK(labels.find("steady") != std::string::npos);

    auto sessions = state->session_history(5);
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].record.session_id == session.session_id);

    TempDir temp_other;
    auto other_session = state->start_session("Other session", FocusMode::Normal);
    state->process_event_for_test(ev(EventType::WindowFocusChange, 300.0));
    exported = state->export_training_data(temp_other.path, session.session_id);
    CHECK(exported.feature_count >= 1);
    CHECK(exported.label_count == 1);
    const auto scoped_features = read_file(temp_other.path / "features.csv");
    CHECK(scoped_features.find("Export from app state") != std::string::npos);
    CHECK(scoped_features.find("Other session") == std::string::npos);
    (void)other_session;
}

TEST_CASE("AppState persists default focus mode settings") {
    TempDir temp;
    auto storage = Storage::open_memory();
    REQUIRE(storage);

    auto state = std::make_unique<AppState>(std::move(*storage), temp.path);
    CHECK(state->settings().default_focus_mode == FocusMode::Normal);

    state->set_focus_mode(FocusMode::Deep);
    CHECK(state->settings().default_focus_mode == FocusMode::Deep);

    const auto raw = read_file(temp.path / kSettingsFileName);
    CHECK(raw.find("\"defaultFocusMode\": \"deep\"") != std::string::npos);

    auto storage2 = Storage::open_memory();
    REQUIRE(storage2);
    auto reloaded = std::make_unique<AppState>(std::move(*storage2), temp.path);
    CHECK(reloaded->settings().default_focus_mode == FocusMode::Deep);
}

TEST_CASE("AppState persists privacy settings and suppresses private events") {
    TempDir temp;
    auto storage = Storage::open_memory();
    REQUIRE(storage);
    auto state = std::make_unique<AppState>(std::move(*storage), temp.path);

    state->set_private_mode(true);
    state->set_privacy_exclusions({"  Banking  ", "BANKING", "1Password"});
    CHECK(state->privacy_settings().private_mode);
    CHECK(state->privacy_settings().excluded_apps == std::vector<std::string>{"Banking", "1Password"});

    state->start_session("Ship privacy", FocusMode::Normal);
    state->process_event_for_test(ev(EventType::KeyPress, 1.0, "Cursor"));
    CHECK(state->prediction_history(10).empty());

    auto storage2 = Storage::open_memory();
    REQUIRE(storage2);
    auto reloaded = std::make_unique<AppState>(std::move(*storage2), temp.path);
    CHECK(reloaded->privacy_settings().private_mode);
    CHECK(reloaded->privacy_settings().excluded_apps ==
          std::vector<std::string>{"Banking", "1Password"});
}

TEST_CASE("AppState excludes matching apps without affecting other apps") {
    auto state = make_state();
    state->set_privacy_exclusions({"password"});
    state->start_session("Test exclusion", FocusMode::Normal);

    state->process_event_for_test(ev(EventType::KeyPress, 1.0, "1Password"));
    CHECK(state->prediction_history(10).empty());

    state->process_event_for_test(ev(EventType::KeyPress, 2.0, "Cursor"));
    CHECK(state->prediction_history(10).size() == 1);
}

TEST_CASE("AppState analytics aggregates predictions, hourly buckets, and app context") {
    auto state = make_state();
    state->start_session("Analyze focus", FocusMode::Normal);
    state->process_event_for_test(ev(EventType::WindowFocusChange, 1.0, "Cursor",
                                     "state.cpp - Snapback"));
    state->process_event_for_test(ev(EventType::KeyPress, 2.0, "Cursor",
                                     "state.cpp - Snapback"));

    const auto summary = state->analytics();
    CHECK(summary.sample_count == 2);
    CHECK(summary.avg_focus_score >= 0.0);
    REQUIRE(summary.hourly.size() == 1);
    CHECK(summary.hourly[0].sample_count == 2);
    REQUIRE(summary.top_apps.size() == 1);
    CHECK(summary.top_apps[0].app_name == "Cursor");
    CHECK(summary.top_apps[0].window_count == 1);
}

TEST_CASE("AppState creates and exports day or week summary reports") {
    TempDir temp;
    auto storage = Storage::open_memory();
    REQUIRE(storage);
    auto state = std::make_unique<AppState>(std::move(*storage), temp.path);
    state->start_session("Daily summary", FocusMode::Normal);
    state->process_event_for_test(ev(EventType::KeyPress, 1.0, "Cursor"));

    const auto report = state->summary_report("day");
    CHECK(report.window == "day");
    CHECK(report.session_count == 1);
    CHECK(report.sample_count == 1);
    CHECK_THROWS_AS(state->summary_report("month"), std::runtime_error);

    const auto exported = state->export_summary_report(temp.path / "exports", "week");
    CHECK(exported.window == "week");
    CHECK(std::filesystem::exists(exported.output_path));
    CHECK(read_file(exported.output_path).find("\"window\": \"week\"") != std::string::npos);
}

TEST_CASE("AppState persists editable goal categories") {
    TempDir temp;
    auto storage = Storage::open_memory();
    REQUIRE(storage);
    auto state = std::make_unique<AppState>(std::move(*storage), temp.path);
    state->set_goal_categories({{"design", {"brand", "visual"}}});
    REQUIRE(state->goal_categories().size() == 1);
    CHECK(state->goal_categories()[0].name == "design");

    auto storage2 = Storage::open_memory();
    REQUIRE(storage2);
    auto reloaded = std::make_unique<AppState>(std::move(*storage2), temp.path);
    REQUIRE(reloaded->goal_categories().size() == 1);
    CHECK(reloaded->goal_categories()[0].keywords[0] == "brand");
}

TEST_CASE("AppState fires a snapback payload on return from a long distraction") {
    auto state = make_state();
    state->start_session("implement the classifier", FocusMode::Normal);

    // 1. Focused + on-task in the IDE: establishes the "last good context". No snapback.
    state->process_event_for_test(ev(EventType::WindowFocusChange, 100.0, "Cursor",
                                     "classifier.cpp - Snapback"));
    CHECK(state->latest_snapback() == std::nullopt);

    // 2. Switch to a clearly off-task window (distracting title) -> Distracted. The
    //    ContextTracker keys off on-task gating, not classifier thrash, so a distracting
    //    title is the deterministic lever.
    state->process_event_for_test(ev(EventType::WindowFocusChange, 101.0, "Google Chrome",
                                     "YouTube - Recommended"));
    CHECK(state->latest_snapback() == std::nullopt);

    // 3. Return to the IDE after > min_distraction (30s) -> the return edge fires the
    //    snapback for the remembered context.
    state->process_event_for_test(ev(EventType::WindowFocusChange, 140.0, "Cursor",
                                     "classifier.cpp - Snapback"));

    auto payload = state->take_snapback();
    REQUIRE(payload.has_value());
    CHECK(payload->app_name == "Cursor");
    CHECK(payload->distraction_duration_secs >= 30);
    // take_snapback() drains it: a second take returns nothing.
    CHECK(state->take_snapback() == std::nullopt);
}

TEST_CASE("AppState dismiss_snapback clears the payload and unsticks the tracker for a "
          "second snapback") {
    auto state = make_state();
    state->start_session("implement the classifier", FocusMode::Normal);

    // First distraction/return cycle: establish context, drift, come back -> Recovering.
    state->process_event_for_test(ev(EventType::WindowFocusChange, 100.0, "Cursor",
                                     "classifier.cpp - Snapback"));
    state->process_event_for_test(ev(EventType::WindowFocusChange, 101.0, "Google Chrome",
                                     "YouTube - Recommended"));
    state->process_event_for_test(ev(EventType::WindowFocusChange, 140.0, "Cursor",
                                     "classifier.cpp - Snapback"));
    REQUIRE(state->latest_snapback().has_value());

    // Without dismiss_snapback(), ContextTracker has no other way out of Recovering —
    // dismiss_recovery() is its only caller — so a second distraction/return cycle would
    // silently produce no payload at all. Calling it here is what proves the fix, not
    // just that the pending payload got cleared.
    state->dismiss_snapback();
    CHECK(state->latest_snapback() == std::nullopt);

    // Second distraction/return cycle, well past the first: should fire again.
    state->process_event_for_test(ev(EventType::WindowFocusChange, 200.0, "Google Chrome",
                                     "YouTube - Recommended"));
    state->process_event_for_test(ev(EventType::WindowFocusChange, 240.0, "Cursor",
                                     "classifier.cpp - Snapback"));

    auto second = state->take_snapback();
    REQUIRE(second.has_value());
    CHECK(second->app_name == "Cursor");
}

TEST_CASE("AppState app-rule CRUD upserts, updates in place, and deletes") {
    auto state = make_state();

    auto rule = state->upsert_app_rule("youtube", AppRuleKind::Block, "no videos");
    CHECK(rule.pattern == "youtube");
    CHECK(rule.rule_type == AppRuleKind::Block);
    REQUIRE(state->app_rules().size() == 1);

    // Same pattern -> update in place (same id), not a second row.
    auto updated = state->upsert_app_rule("youtube", AppRuleKind::Allow, std::nullopt);
    CHECK(updated.id == rule.id);
    CHECK(updated.rule_type == AppRuleKind::Allow);
    CHECK(state->app_rules().size() == 1);

    state->delete_app_rule(rule.id);
    CHECK(state->app_rules().empty());
}

TEST_CASE("AppState context timeline records window changes for the active session") {
    auto state = make_state();
    state->start_session("write the docs", FocusMode::Normal);

    state->process_event_for_test(ev(EventType::WindowFocusChange, 10.0, "Cursor",
                                     "auth.ts - Snapback"));
    state->process_event_for_test(ev(EventType::WindowFocusChange, 11.0, "Google Chrome",
                                     "YouTube"));

    auto timeline = state->context_timeline(std::nullopt, 10);
    REQUIRE(timeline.size() == 1);
    CHECK(timeline[0].app_name == "Cursor");
    CHECK(timeline[0].file_hint == "auth.ts");
    CHECK(timeline[0].summary == "Editing auth.ts");

    // No active session and no explicit id -> empty, not an error.
    auto other = make_state();
    CHECK(other->context_timeline(std::nullopt, 10).empty());
}

TEST_CASE("AppState serves concurrent reads during a writer without deadlock or races") {
    // Exercises the Phase-3 two-lock design: a writer runs the full compute+persist path
    // while a reader hammers the hot UI reads plus a storage-backed command. If the lock
    // order were wrong this would deadlock (caught by the ctest timeout); if a read raced
    // it would throw or return junk.
    auto state = make_state();
    auto session = state->start_session("concurrency", FocusMode::Normal);

    constexpr int kEvents = 3000;
    std::atomic<bool> writer_done{false};
    std::atomic<bool> reader_ok{true};

    std::thread writer([&] {
        double ts = 0.0;
        for (int i = 0; i < kEvents; ++i) {
            ts += 2.0;  // >1s apart -> every event runs a full classify + persist
            state->process_event_for_test(
                ev(EventType::KeyPress, ts, "Cursor", "state.cpp - Snapback"));
        }
        writer_done.store(true);
    });

    std::uint64_t reads = 0;
    while (!writer_done.load()) {
        try {
            auto health = state->health();
            (void)state->latest_prediction();
            (void)state->active_session();
            if (reads % 64 == 0) (void)state->session_recap(session.session_id);
            if (health.status.empty()) reader_ok.store(false);
        } catch (...) {
            reader_ok.store(false);
        }
        ++reads;
    }
    writer.join();

    CHECK(reader_ok.load());
    auto latest = state->latest_prediction();
    REQUIRE(latest.has_value());
    CHECK(latest->session_id == session.session_id);
    CHECK(state->session_recap(session.session_id).session_id == session.session_id);
}

TEST_CASE("AppState health reflects offline engine before capture starts") {
    auto state = make_state();
    auto health = state->health();

    CHECK(health.status == "offline");
    CHECK_FALSE(health.capture_running);
    CHECK(health.classifier.backend == "heuristic");
}

TEST_CASE("AppState health reports a capture hook that stopped unexpectedly") {
    auto state = make_state();
    ReturningHook hook;
    state->start_engine_for_test(&hook);

    bool returned = false;
    for (int attempt = 0; attempt < 5000 && !returned; ++attempt) {
        returned = hook.returned();
        if (!returned) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(returned);
    if (!returned) {
        state->stop_engine();
        return;
    }

    HealthStatus health;
    bool failed = false;
    for (int attempt = 0; attempt < 5000 && !failed; ++attempt) {
        health = state->health();
        failed = health.capture_failed;
        if (!failed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(failed);
    CHECK(health.status == "capture_failed");
    CHECK_FALSE(health.capture_running);
    REQUIRE(health.capture_failure_reason.has_value());
    CHECK(*health.capture_failure_reason == "input hook stopped unexpectedly");
    state->stop_engine();
}

TEST_CASE("AppState health explains prediction freshness and suppression") {
    auto state = make_state();

    auto health = state->health();
    CHECK_FALSE(health.last_prediction_age_secs.has_value());
    CHECK(health.prediction_suppression_reason == "no_session");

    const auto session = state->start_session("Explain prediction health", FocusMode::Normal);
    health = state->health();
    CHECK_FALSE(health.last_prediction_age_secs.has_value());
    CHECK(health.prediction_suppression_reason == "none");

    state->process_event_for_test(ev(EventType::KeyPress, 1.0));
    health = state->health();
    REQUIRE(health.last_prediction_age_secs.has_value());
    CHECK(*health.last_prediction_age_secs >= 0.0);
    CHECK(health.prediction_suppression_reason == "none");

    state->update_idle_for_test(0, true);
    state->update_idle_for_test(kDefaultIdleThresholdMs, false);
    CHECK(state->health().prediction_suppression_reason == "idle");

    state->set_private_mode(true);
    CHECK(state->health().prediction_suppression_reason == "private_mode");
    state->set_private_mode(false);

    state->update_idle_for_test(kDefaultIdleThresholdMs + 1, true);
    state->stop_session(session.session_id);
    CHECK(state->health().prediction_suppression_reason == "no_session");
}

TEST_CASE("AppState contains engine tick exceptions and keeps the engine online") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info);
    auto state = std::make_unique<AppState>(std::move(*storage), std::filesystem::path{}, &logger);
    OneShotHook hook;

    state->set_emit_hook([](const char*, const std::string&) {
        throw std::runtime_error("intentional emit failure");
    });
    state->start_engine_for_test(&hook);

    bool emitted = false;
    for (int attempt = 0; attempt < 5000 && !emitted; ++attempt) {
        emitted = hook.emitted();
        if (!emitted) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(emitted);
    if (!emitted) {
        state->stop_engine();
        return;
    }

    bool logged = false;
    for (int attempt = 0; attempt < 5000 && !logged; ++attempt) {
        logged = contains_log(state->diagnostics(), "engine tick failed: intentional emit failure");
        if (!logged) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(logged);
    CHECK(state->health().status == "online");
    state->stop_engine();
}
