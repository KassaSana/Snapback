#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "app/settings.hpp"
#include "app/state.hpp"

using namespace snapback;

namespace {

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
