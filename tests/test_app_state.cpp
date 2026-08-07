#include "doctest_wrapper.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/settings.hpp"
#include "app/state.hpp"
#include "app_state_test_access.hpp"
#include "manual_clock.hpp"
#include "util/logger.hpp"

using namespace snapback;

namespace {

class OneShotHook final : public InputHook {
public:
    void run(InputCallback on_event, const std::atomic<bool>&) override {
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

    void stop() noexcept override { running_.store(false, std::memory_order_relaxed); }

    bool emitted() const { return emitted_.load(std::memory_order_acquire); }
    bool stopped() const { return !running_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> running_{true};
    std::atomic<bool> emitted_{false};
};

// Feeds two events: one to start the break clock, one far enough later that the mode's
// hyperfocus window has elapsed. Drives the real engine thread, because the bug this test
// guards was a feature that had correct logic, a passing unit test, and no caller.
class HyperfocusHook final : public InputHook {
public:
    void run(InputCallback on_event, const std::atomic<bool>&) override {
        CaptureEvent first;
        first.event_type = EventType::KeyPress;
        first.timestamp_secs = 1.0;
        first.app_name = "Cursor";
        first.window_title = "state.cpp - Snapback";
        on_event(first);

        CaptureEvent later = first;
        later.timestamp_secs = 8100.0;  // 134 minutes on, past Normal's 120-minute window
        on_event(later);

        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void stop() noexcept override { running_.store(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> running_{true};
};

class ReturningHook final : public InputHook {
public:
    void run(InputCallback, const std::atomic<bool>&) override {
        returned_.store(true, std::memory_order_release);
    }
    void stop() noexcept override {}

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

std::string utc_days_ago(int days) {
    const auto now = std::time(nullptr) - static_cast<std::time_t>(days * 24 * 60 * 60);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
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
    CHECK(AppStateTestAccess::update_idle(*state, 0, /*had_input=*/true) == IdleTransition::None);
    CHECK_FALSE(state->is_idle());

    // No input across the threshold -> AFK, exactly one WentIdle edge.
    CHECK(AppStateTestAccess::update_idle(*state, t - 1, false) == IdleTransition::None);
    CHECK(AppStateTestAccess::update_idle(*state, t, false) == IdleTransition::WentIdle);
    CHECK(state->is_idle());
    CHECK(AppStateTestAccess::update_idle(*state, t + 500, false) == IdleTransition::None);  // no repeat

    // Input wakes us.
    CHECK(AppStateTestAccess::update_idle(*state, t + 600, true) == IdleTransition::WokeUp);
    CHECK_FALSE(state->is_idle());
}

TEST_CASE("going idle pauses the session and coming back resumes it") {
    // Roadmap 7.23 / ADR-0005. This is the action idle_detector.hpp documented from the start
    // ("5 minutes of no input pauses the session") and never performed -- the edges only ever
    // emitted a UI event.
    //
    // Asserts the *wiring*, not the arithmetic: Storage stamps its own timestamps from the
    // system clock and has no injected one, so a ManualClock here cannot move a duration.
    // The span arithmetic is proven in test_storage.cpp with explicit timestamps.
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto session = state.start_session("attend me", FocusMode::Normal);
    // Starting a session is by definition attending it.
    CHECK(AppStateTestAccess::has_open_span(state, session.session_id));

    AppStateTestAccess::update_idle(state, clock.steady_ms(), /*had_input=*/true);

    // Walk away. The edge fires once the threshold is crossed; the tick writes the pause.
    clock.advance_ms(kDefaultIdleThresholdMs);
    CHECK(AppStateTestAccess::update_idle(state, clock.steady_ms(), false) ==
          IdleTransition::WentIdle);
    AppStateTestAccess::engine_tick(state);
    CHECK_FALSE(AppStateTestAccess::has_open_span(state, session.session_id));

    // Come back: a new span opens.
    CHECK(AppStateTestAccess::update_idle(state, clock.steady_ms(), true) ==
          IdleTransition::WokeUp);
    AppStateTestAccess::engine_tick(state);
    CHECK(AppStateTestAccess::has_open_span(state, session.session_id));

    state.stop_session();
    CHECK_FALSE(AppStateTestAccess::has_open_span(state, session.session_id));

    // And the whole thing produced a measurement rather than nothing.
    const auto recap = state.session_recap(session.session_id);
    CHECK(recap.active_secs.has_value());
}

namespace {

// Drives sustained activity for `minutes`, ticking once a minute the way the engine does.
// Returns every event name the emit hook saw.
std::vector<std::string> work_without_session(AppState& state, ManualClock& clock, int minutes) {
    std::vector<std::string> seen;
    state.set_emit_hook([&seen](const std::string& name, const std::string&, std::uint64_t) {
        seen.push_back(name);
    });
    for (int minute = 0; minute < minutes; ++minute) {
        clock.advance_minutes(1);
        AppStateTestAccess::update_idle(state, clock.steady_ms(), /*had_input=*/true);
        AppStateTestAccess::engine_tick(state);
    }
    state.set_emit_hook(nullptr);
    return seen;
}

bool contains(const std::vector<std::string>& names, const std::string& wanted) {
    return std::find(names.begin(), names.end(), wanted) != names.end();
}

}  // namespace

TEST_CASE("sustained work with no session raises one nudge") {
    // Roadmap 2.7 / ADR-0005. Without a session AppState records nothing at all, so someone
    // who forgets to press Start gets no data and no warning. This asks, once, rather than
    // auto-starting -- an auto-started session has no declared goal, and goal_alignment is a
    // real model input that Tier 13 trains on.
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto seen = work_without_session(state, clock, 20);
    CHECK(contains(seen, "untracked_work"));
    // Latched: one nudge per stretch, not one per tick for the five minutes past the
    // threshold. Nagging every second is how a useful prompt becomes one people disable.
    CHECK(std::count(seen.begin(), seen.end(), std::string("untracked_work")) == 1);
}

TEST_CASE("a running session never raises the untracked nudge") {
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    state.start_session("tracked", FocusMode::Normal);
    CHECK_FALSE(contains(work_without_session(state, clock, 30), "untracked_work"));
}

TEST_CASE("brief activity below the threshold raises no nudge") {
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    CHECK_FALSE(contains(work_without_session(state, clock, 5), "untracked_work"));
}

TEST_CASE("private mode never raises the untracked nudge") {
    // The user has said "do not record". Asking them to start recording is the wrong
    // direction, and the timer resets rather than pausing, so leaving private mode does not
    // immediately fire a nudge earned while it was on.
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    state.set_private_mode(true);
    CHECK_FALSE(contains(work_without_session(state, clock, 30), "untracked_work"));

    state.set_private_mode(false);
    // The half-hour above earned nothing; a fresh short stretch is still short.
    CHECK_FALSE(contains(work_without_session(state, clock, 5), "untracked_work"));
}

TEST_CASE("going idle restarts the untracked stretch instead of firing again") {
    // Walking away ends the stretch. Coming back begins a new one, so the nudge does not
    // re-fire the moment someone returns to a machine they already declined to track.
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    REQUIRE(contains(work_without_session(state, clock, 20), "untracked_work"));

    // Go idle, which clears the latch and the stretch.
    clock.advance_ms(kDefaultIdleThresholdMs);
    AppStateTestAccess::update_idle(state, clock.steady_ms(), /*had_input=*/false);
    AppStateTestAccess::engine_tick(state);

    // A short burst after returning is a new, short stretch: no nudge yet.
    CHECK_FALSE(contains(work_without_session(state, clock, 5), "untracked_work"));
    // Keep going and it earns one again.
    CHECK(contains(work_without_session(state, clock, 15), "untracked_work"));
}

TEST_CASE("replacing a session closes the replaced session's span") {
    // Roadmap 7.23 + 7.20. Starting a session while one runs replaces it, and the replaced
    // session is completed. Its span must close with it -- an open span on a completed
    // session counts to "now" indefinitely, so a session replaced last week would keep
    // accruing attended time and eventually claim more of it than it was ever open for.
    ManualClock clock;
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto first = state.start_session("first", FocusMode::Normal);
    CHECK(AppStateTestAccess::has_open_span(state, first.session_id));

    const auto second = state.start_session("second", FocusMode::Deep);
    CHECK_FALSE(AppStateTestAccess::has_open_span(state, first.session_id));
    CHECK(AppStateTestAccess::has_open_span(state, second.session_id));
}

TEST_CASE("idle edges for a session that is not running touch nothing") {
    // The engine ticks whether or not a session is open. An idle edge with none running must
    // not create a span against a stale id -- there is nothing being attended.
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto session = state.start_session("ended already", FocusMode::Normal);
    state.stop_session();

    AppStateTestAccess::update_idle(state, clock.steady_ms(), true);
    clock.advance_ms(kDefaultIdleThresholdMs);
    AppStateTestAccess::update_idle(state, clock.steady_ms(), false);
    AppStateTestAccess::engine_tick(state);

    CHECK_FALSE(AppStateTestAccess::has_open_span(state, session.session_id));
}

TEST_CASE("a session with no idle edges still records attended time") {
    // The straightforward path must not depend on an idle transition ever happening: the
    // first span opens with the session and closes when it stops.
    ManualClock clock;
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto session = state.start_session("uninterrupted", FocusMode::Normal);
    CHECK(AppStateTestAccess::has_open_span(state, session.session_id));
    state.stop_session();
    CHECK_FALSE(AppStateTestAccess::has_open_span(state, session.session_id));

    const auto recap = state.session_recap(session.session_id);
    CHECK(recap.active_secs.has_value());
}

TEST_CASE("reopening after a crash closes the dangling span at the last recorded activity") {
    // Roadmap 7.23. A process that dies with a span open leaves it open. Left alone,
    // active_secs measures that span to "now", so an app that crashed on Friday reports the
    // whole weekend as attended -- the one answer that is certainly wrong.
    //
    // File-backed rather than in-memory, because the behaviour under test only exists across
    // two processes and an in-memory database cannot be reopened.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        // Two spans of ten minutes each, then a third left open an hour before "now" -- the
        // shape a crash leaves behind. Written straight to Storage so the timestamps are
        // explicit rather than at the mercy of how fast the test runs.
        auto session = storage->create_session("crashed", FocusMode::Normal);
        session_id = session.session_id;
        storage->begin_session_span(session_id, "2026-08-05T09:00:00Z");
        REQUIRE(storage->close_session_span(session_id, "2026-08-05T09:10:00Z"));
        storage->begin_session_span(session_id, "2026-08-05T09:20:00Z");

        // The last thing the session managed to record before dying.
        PredictionRecord prediction;
        prediction.session_id = session_id;
        prediction.timestamp = "2026-08-05T09:30:00Z";
        storage->insert_prediction(prediction);
    }

    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        // Constructing AppState is what hydrates; nothing else has run yet.
        AppState state(std::move(*storage), temp.path);
        CHECK_FALSE(AppStateTestAccess::has_open_span(state, session_id));

        // Ten closed minutes plus ten from the dangling span, ended at its last evidence.
        // Not the hours since -- that is the whole point.
        const auto recap = state.session_recap(session_id);
        REQUIRE(recap.active_secs.has_value());
        CHECK(*recap.active_secs == 1200);
    }
}

TEST_CASE("a dangling span with nothing recorded collapses instead of guessing") {
    // No prediction, no context snapshot, no snapback event: the session has no evidence the
    // user was ever present after the span opened. Closing at "now" would invent attended
    // time out of an unknown, so it closes where it started and contributes nothing.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("no evidence", FocusMode::Normal).session_id;
        storage->begin_session_span(session_id, "2026-08-05T09:00:00Z");
    }
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        AppState state(std::move(*storage), temp.path);
        CHECK_FALSE(AppStateTestAccess::has_open_span(state, session_id));
        const auto recap = state.session_recap(session_id);
        REQUIRE(recap.active_secs.has_value());
        CHECK(*recap.active_secs == 0);
    }
}

TEST_CASE("attendance resumes after hydration without waiting for an idle round trip") {
    // The bug the level-based rule exists to prevent. Hydration closes the dangling span, and
    // there is no idle edge on the way back in -- the user never went idle, the process died.
    // An edge-driven rule would then record nothing until the user walked away for five
    // minutes and returned, which is a silent hole in the headline number.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("resume me", FocusMode::Normal).session_id;
        storage->begin_session_span(session_id, "2026-08-05T09:00:00Z");
    }
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        ManualClock clock;
        clock.set_steady_ms(0);
        AppState state(std::move(*storage), temp.path, nullptr, &clock);
        REQUIRE_FALSE(AppStateTestAccess::has_open_span(state, session_id));

        AppStateTestAccess::engine_tick(state);
        CHECK(AppStateTestAccess::has_open_span(state, session_id));
    }
}

TEST_CASE("a clean shutdown closes the open span") {
    // Roadmap 7.23. Exiting normally is the last moment we know the user was attending, so
    // the span closes here rather than being left for the next launch to reconstruct from
    // whatever rows happen to exist.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        AppState state(std::move(*storage), temp.path);
        session_id = state.start_session("clean exit", FocusMode::Normal).session_id;
        REQUIRE(AppStateTestAccess::has_open_span(state, session_id));
    }  // ~AppState -> stop_engine -> close_open_span_on_shutdown

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK_FALSE(reopened->has_open_span(session_id));
}

TEST_CASE("the idle threshold is a setting, applied live and persisted") {
    // Roadmap 7.23. Five minutes was inherited from a constant. It is a judgement about the
    // user's working rhythm -- reading and thinking look identical to a keyboard -- so it
    // belongs to the user.
    TempDir temp;
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), temp.path, nullptr, &clock);
    CHECK(state.settings().idle_threshold_secs == kDefaultIdleThresholdSecs);

    const auto session = state.start_session("short fuse", FocusMode::Normal);
    state.set_idle_threshold_secs(60);
    CHECK(state.settings().idle_threshold_secs == 60);

    // One minute of silence is now enough, where the default would have needed five.
    AppStateTestAccess::update_idle(state, clock.steady_ms(), /*had_input=*/true);
    clock.advance_ms(60'000);
    CHECK(AppStateTestAccess::update_idle(state, clock.steady_ms(), false) ==
          IdleTransition::WentIdle);
    AppStateTestAccess::engine_tick(state);
    CHECK_FALSE(AppStateTestAccess::has_open_span(state, session.session_id));

    // And it survives a restart, because it was written to settings.json.
    CHECK(load_app_settings(temp.path).idle_threshold_secs == 60);
}

TEST_CASE("an out-of-range idle threshold is rejected and changes nothing") {
    // Rejected rather than clamped: a clamped value looks like a setting someone chose. The
    // check runs before any mutation, so both the live detector and settings.json are
    // untouched -- 7.26's guarantee, applied to the one setter that could reach it first.
    TempDir temp;
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), temp.path);

    state.set_idle_threshold_secs(120);
    CHECK_THROWS_AS(state.set_idle_threshold_secs(kMinIdleThresholdSecs - 1), std::runtime_error);
    CHECK_THROWS_AS(state.set_idle_threshold_secs(kMaxIdleThresholdSecs + 1), std::runtime_error);
    CHECK_THROWS_AS(state.set_idle_threshold_secs(0), std::runtime_error);
    CHECK(state.settings().idle_threshold_secs == 120);
    CHECK(load_app_settings(temp.path).idle_threshold_secs == 120);
}

TEST_CASE("mouse movement alone counts as presence") {
    // Roadmap 7.23 asked whether scroll and mouse movement should count before this was
    // treated as a trustworthy default. They do, and this pins it: a mouse-only stretch --
    // reading a document, dragging a scrollbar -- is someone at their desk, and the OS idle
    // timers every user already has calibrated their expectations on agree. Requiring
    // keystrokes would report a reviewer or a reader as absent for their whole session.
    ManualClock clock;
    clock.set_steady_ms(0);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto session = state.start_session("reading", FocusMode::Normal);
    clock.advance_ms(kDefaultIdleThresholdMs);
    CHECK(AppStateTestAccess::update_idle(state, clock.steady_ms(), false) ==
          IdleTransition::WentIdle);
    AppStateTestAccess::engine_tick(state);
    REQUIRE_FALSE(AppStateTestAccess::has_open_span(state, session.session_id));

    // A mouse move is input, so the detector wakes and the span reopens.
    CaptureEvent moved;
    moved.event_type = EventType::MouseMove;
    moved.timestamp_secs = 1.0;
    AppStateTestAccess::process_event(state, moved);
    CHECK(AppStateTestAccess::update_idle(state, clock.steady_ms(), /*had_input=*/true) ==
          IdleTransition::WokeUp);
    AppStateTestAccess::engine_tick(state);
    CHECK(AppStateTestAccess::has_open_span(state, session.session_id));
}

TEST_CASE("a failed start leaves the previous session exactly as it was") {
    // Roadmap 7.25. start_session used to change focus mode and reset the extractor, tracker,
    // and Pomodoro *before* the storage write that can throw. A failed insert therefore left
    // the old database session running underneath brand-new in-memory state: the app believed
    // it was recording a session that did not exist.
    //
    // The failure here is real rather than injected. A second connection holds a write
    // transaction, and SQLite is opened with no busy timeout, so the insert gets SQLITE_BUSY
    // immediately -- which is also the honest production scenario (another Snapback process,
    // or a backup tool, holding the file).
    TempDir temp;
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), temp.path);

    const auto first = state.start_session("keep me", FocusMode::Deep);
    REQUIRE(state.settings().default_focus_mode == FocusMode::Normal);

    {
        auto blocker = Storage::open(temp.path);
        REQUIRE(blocker.has_value());
        Storage::Transaction lock_holder(*blocker);  // BEGIN IMMEDIATE: writers are blocked
        CHECK_THROWS(state.start_session("should fail", FocusMode::Recovery));
    }

    // The old session is still the active one, still ACTIVE in storage, and still attended.
    const auto active = state.active_session();
    REQUIRE(active.has_value());
    CHECK(active->session_id == first.session_id);
    CHECK(active->status == "ACTIVE");
    CHECK(AppStateTestAccess::has_open_span(state, first.session_id));
    // And nothing wrote an end to it, so it is not sitting completed-but-current.
    const auto stored = state.get_session(first.session_id);
    REQUIRE(stored.has_value());
    CHECK(stored->status == "ACTIVE");
    CHECK_FALSE(stored->ended_at.has_value());
}

TEST_CASE("replacing a session writes the same automatic label a stop would") {
    // Roadmap 7.25. Replacement completed the old row silently, so the only thing deciding
    // whether a finished session got a verdict was whether the user pressed Stop or just
    // started the next thing -- a distinction they never made deliberately.
    ManualClock clock;
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto first = state.start_session("replaced", FocusMode::Normal);
    state.start_session("replacement", FocusMode::Normal);

    TempDir temp;
    const auto exported = state.export_training_data(temp.path, first.session_id);
    CHECK(exported.label_count == 1);
    CHECK(read_file(temp.path / "labels.csv").find(",auto,") != std::string::npos);
}

TEST_CASE("stopping twice does not append a second automatic label") {
    // Roadmap 7.25. Storage::stop_session is idempotent, but the label write was not, so a
    // double-click on Stop produced two `auto` rows for one session -- different whenever a
    // prediction landed between them, with nothing to say which was meant. The labels table is
    // append-only by design, so a duplicate cannot be cleaned up afterwards.
    ManualClock clock;
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    const auto session = state.start_session("stop me twice", FocusMode::Normal);
    state.stop_session(session.session_id);
    state.stop_session(session.session_id);
    state.stop_session(session.session_id);

    TempDir temp;
    CHECK(state.export_training_data(temp.path, session.session_id).label_count == 1);
}

TEST_CASE("restarting restores the active session's saved focus mode") {
    // Roadmap 7.25. Focus mode sets the risk threshold and the hyperfocus window, so a Deep
    // session that came back as Normal was being classified against a different question than
    // the one it was started to ask -- and nothing on screen said so.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        AppState state(std::move(*storage), temp.path);
        session_id = state.start_session("deep work", FocusMode::Deep).session_id;
    }
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        AppState state(std::move(*storage), temp.path);
        const auto active = state.active_session();
        REQUIRE(active.has_value());
        CHECK(active->session_id == session_id);
        // The settings default is Normal, which is what this used to come back as.
        REQUIRE(state.settings().default_focus_mode == FocusMode::Normal);
        CHECK(active->focus_mode == "deep");
        // The live mode, not just the stored string. `settings().default_focus_mode` is what
        // the *next* session starts with; this is what the classifier is using right now, and
        // the gap between the two was the bug.
        CHECK(AppStateTestAccess::focus_mode(state) == FocusMode::Deep);
    }
}

TEST_CASE("a resumed session's elapsed feature continues instead of restarting at zero") {
    // Roadmap 7.25. `seconds_since_session_start` restarted from zero on reopen while the
    // recap beside it kept reporting the real elapsed time: one session, two contradictory
    // numbers, and the contradictory one is a model input.
    FeatureExtractor features;
    features.resume_session(3600.0);  // an hour in already

    CaptureEvent first;
    first.event_type = EventType::KeyPress;
    first.timestamp_secs = 10.0;  // a fresh process: the monotonic clock starts near zero
    features.ingest(first);

    const auto vector = features.extract(20.0);
    // 3600 already elapsed + 10 seconds of this process, not 10.
    CHECK(vector.seconds_since_session_start() == doctest::Approx(3610.0));
    // The break clock is NOT back-dated. Had it been, this would read 60 minutes -- and
    // Normal mode's hyperfocus nudge would be most of the way to firing for a break the
    // process was not running to observe.
    CHECK(vector.minutes_since_last_break() == doctest::Approx(0.0));
}

TEST_CASE("a backwards clock between runs resumes at zero rather than counting down") {
    FeatureExtractor features;
    features.resume_session(-500.0);

    CaptureEvent first;
    first.event_type = EventType::KeyPress;
    first.timestamp_secs = 10.0;
    features.ingest(first);

    CHECK(features.extract(10.0).seconds_since_session_start() == doctest::Approx(0.0));
}

TEST_CASE("AppState binds Pomodoro to an active session and exposes transition edges") {
    // ROADMAP 7.14: this used to call `start_pomodoro_for_test(100)`, a public method on the
    // shipping class whose only reason to exist was passing `now_ms` in by hand. 11.4's
    // injected clock made it redundant — `start_pomodoro()` reads the clock, so setting the
    // clock and calling the *real* API is now strictly better: it exercises the production
    // path rather than a parallel one that could drift from it.
    ManualClock clock;
    clock.set_steady_ms(100);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), {}, nullptr, &clock);

    CHECK_THROWS_WITH(state.start_pomodoro(), "no active session");

    const auto session = state.start_session("Finish Pomodoro wiring", FocusMode::Normal);
    const auto started = state.start_pomodoro();
    CHECK(started.running);
    CHECK(started.phase == PomodoroPhase::Work);
    CHECK(started.remaining_ms == 25 * 60 * 1000);

    // The 25-minute phase boundary, reached instantly. A sleep-based test could never assert
    // this at all, which is why it was previously only reachable through a `_for_test` method.
    CHECK_FALSE(AppStateTestAccess::update_pomodoro(state, 100 + 25 * 60 * 1000 - 1).has_value());
    const auto transition = AppStateTestAccess::update_pomodoro(state, 100 + 25 * 60 * 1000);
    REQUIRE(transition.has_value());
    CHECK(transition->phase == PomodoroPhase::ShortBreak);
    CHECK(transition->completed_work_intervals == 1);
    CHECK_FALSE(AppStateTestAccess::update_pomodoro(state, 100 + 25 * 60 * 1000).has_value());

    state.stop_session(session.session_id);
    const auto stopped = state.pomodoro_status();
    CHECK_FALSE(stopped.running);
    CHECK(stopped.phase == PomodoroPhase::Work);
    CHECK(stopped.completed_work_intervals == 0);
}

TEST_CASE("AppState focus_summary aggregates persisted predictions") {
    auto state = make_state();
    auto session = state->start_session("Write tests", FocusMode::Deep);
    // Drive a few events far enough apart to clear the 1s prediction throttle.
    for (int i = 0; i < 4; ++i) {
        AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0 + i * 2.0));
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
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0));
    CHECK(state->latest_prediction().has_value());

    // Force AFK, then the next event must NOT overwrite/produce a prediction.
    AppStateTestAccess::update_idle(*state, 0, true);
    AppStateTestAccess::update_idle(*state, kDefaultIdleThresholdMs, false);
    REQUIRE(state->is_idle());
    const auto before = state->latest_prediction()->timestamp;
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 100.0));
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

TEST_CASE("AppState saves an automatic label when stopping the active session") {
    auto state = make_state();
    const auto session = state->start_session("Label shutdown session", FocusMode::Normal);
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 3.0));

    state->stop_session();

    TempDir temp;
    const auto exported = state->export_training_data(temp.path, session.session_id);
    CHECK(exported.label_count == 1);
    CHECK(read_file(temp.path / "labels.csv").find(",auto,") != std::string::npos);
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
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 1000.0, "Cursor"));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1002.0, "Cursor"));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1045.0, "Cursor"));
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

    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 100.0));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 101.2));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 102.5));

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
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 200.0));
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
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 300.0));
    exported = state->export_training_data(temp_other.path, session.session_id);
    CHECK(exported.feature_count >= 1);
    // Two now, not one: the manual label above plus the automatic one that Roadmap 7.25 made
    // replacement write. Before that, whether a finished session got a verdict depended on
    // whether it ended by Stop or by being replaced, which is not a distinction the user made.
    CHECK(exported.label_count == 2);
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
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0, "Cursor"));
    CHECK(state->prediction_history(10).empty());

    auto storage2 = Storage::open_memory();
    REQUIRE(storage2);
    auto reloaded = std::make_unique<AppState>(std::move(*storage2), temp.path);
    CHECK(reloaded->privacy_settings().private_mode);
    CHECK(reloaded->privacy_settings().excluded_apps ==
          std::vector<std::string>{"Banking", "1Password"});
}

TEST_CASE("AppState deletes collected activity and resets the live session") {
    TempDir temp;
    auto storage = Storage::open_memory();
    REQUIRE(storage);
    auto state = std::make_unique<AppState>(std::move(*storage), temp.path);
    const auto session = state->start_session("Delete this", FocusMode::Normal);
    state->start_pomodoro();
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0, "Cursor"));
    state->upsert_app_rule("Cursor", AppRuleKind::Allow, std::nullopt);
    REQUIRE(state->active_session().has_value());
    REQUIRE(state->latest_prediction().has_value());

    const auto training_export = temp.path / "exports" / "training" / "features.csv";
    const auto summary_export = temp.path / "exports" / "summaries" / "summary_week.json";
    const auto support_export = temp.path / "exports" / "support" / "support.json";
    const auto deployed_model = temp.path / "model.onnx";
    for (const auto& file :
         {training_export, summary_export, support_export, deployed_model}) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << "private-derived-data";
    }

    state->delete_all_activity_data();

    CHECK(state->active_session() == std::nullopt);
    CHECK(state->latest_prediction() == std::nullopt);
    CHECK(state->prediction_history(10).empty());
    CHECK(state->session_history(10).empty());
    CHECK_FALSE(state->pomodoro_status().running);
    REQUIRE(state->app_rules().size() == 1);
    CHECK(state->app_rules().front().pattern == "Cursor");
    CHECK(state->get_session(session.session_id) == std::nullopt);
    CHECK_FALSE(std::filesystem::exists(training_export));
    CHECK_FALSE(std::filesystem::exists(summary_export));
    CHECK(std::filesystem::exists(support_export));
    CHECK(std::filesystem::exists(deployed_model));
}

TEST_CASE("AppState::delete_session removes one session and leaves the rest alone") {
    auto state = make_state();
    const auto keeper = state->start_session("Keeper", FocusMode::Normal);
    state->stop_session(keeper.session_id);
    const auto doomed = state->start_session("Doomed", FocusMode::Normal);
    state->stop_session(doomed.session_id);

    CHECK(state->delete_session(doomed.session_id));

    CHECK(state->get_session(doomed.session_id) == std::nullopt);
    REQUIRE(state->session_history(10).size() == 1);
    CHECK(state->session_history(10).front().record.session_id == keeper.session_id);
}

TEST_CASE("AppState::delete_session clears live state when the active session is deleted") {
    // The dangerous case. Deleting the row the engine is currently filling would leave
    // active_session_ pointing at a session that no longer exists: the next tick would try
    // to persist against a missing foreign key, and the UI would keep rendering a session
    // the user just erased.
    auto state = make_state();
    const auto session = state->start_session("Live", FocusMode::Deep);
    REQUIRE(state->active_session().has_value());

    CHECK(state->delete_session(session.session_id));

    CHECK(state->active_session() == std::nullopt);
    CHECK(state->latest_prediction() == std::nullopt);
    CHECK(state->session_history(10).empty());
    CHECK_FALSE(state->pomodoro_status().running);
}

TEST_CASE("AppState::delete_session evicts a deleted inactive latest prediction") {
    auto state = make_state();
    const auto doomed = state->start_session("Predicted then deleted", FocusMode::Normal);
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0));
    state->stop_session(doomed.session_id);
    const auto keeper = state->start_session("Still active", FocusMode::Normal);
    REQUIRE(state->latest_prediction().has_value());
    REQUIRE(state->latest_prediction()->session_id == doomed.session_id);

    REQUIRE(state->delete_session(doomed.session_id));

    REQUIRE(state->active_session().has_value());
    CHECK(state->active_session()->session_id == keeper.session_id);
    CHECK(state->latest_prediction() == std::nullopt);
}

TEST_CASE("AppState::delete_session retains the latest prediction from another session") {
    auto state = make_state();
    const auto keeper = state->start_session("Keep prediction", FocusMode::Normal);
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0));
    state->stop_session(keeper.session_id);
    const auto doomed = state->start_session("Delete active session", FocusMode::Normal);
    REQUIRE(state->latest_prediction().has_value());
    REQUIRE(state->latest_prediction()->session_id == keeper.session_id);

    REQUIRE(state->delete_session(doomed.session_id));

    CHECK(state->active_session() == std::nullopt);
    REQUIRE(state->latest_prediction().has_value());
    CHECK(state->latest_prediction()->session_id == keeper.session_id);
}

TEST_CASE("AppState::delete_session reports a missing session instead of throwing") {
    auto state = make_state();
    CHECK_FALSE(state->delete_session("never-existed"));
}

TEST_CASE("AppState::delete_session invalidates events already queued for the UI") {
    // Same reasoning as delete_all_activity_data: an event describing the deleted session
    // may already be sitting in the dispatch queue, and delivering it would repopulate the
    // UI with data the user just erased.
    auto state = make_state();
    const auto session = state->start_session("Queued", FocusMode::Normal);
    // A fresh AppState starts at epoch 0, and events captured before the delete carry it.
    REQUIRE(state->activity_epoch_is_current(0));

    REQUIRE(state->delete_session(session.session_id));

    CHECK_FALSE(state->activity_epoch_is_current(0));
}

TEST_CASE("AppState excludes matching apps without affecting other apps") {
    auto state = make_state();
    state->set_privacy_exclusions({"1Password"});
    state->start_session("Test exclusion", FocusMode::Normal);

    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0, "1Password"));
    CHECK(state->prediction_history(10).empty());

    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 2.0, "Cursor"));
    CHECK(state->prediction_history(10).size() == 1);
}

TEST_CASE("AppState privacy exclusions do not overmatch inside app names") {
    auto state = make_state();
    state->set_privacy_exclusions({"Chrome"});
    state->start_session("Bounded exclusion", FocusMode::Normal);

    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0, "Google Chrome"));
    CHECK(state->prediction_history(10).empty());

    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 2.0, "chromedriver"));
    CHECK(state->prediction_history(10).size() == 1);
}

TEST_CASE("AppState analytics includes predictions older than the former row cap") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("Scale analytics", FocusMode::Normal);

    const auto timestamp = utc_days_ago(2);
    Storage::Transaction transaction(*storage);
    for (int i = 0; i < 10001; ++i) {
        PredictionRecord prediction;
        prediction.session_id = session.session_id;
        prediction.focus_score = 80.0;
        prediction.distraction_risk = 0.2;
        prediction.focus_state = "PRODUCTIVE";
        prediction.timestamp = timestamp;
        storage->insert_prediction(prediction);
    }
    transaction.commit();
    storage->end_session(session.session_id);

    AppState state(std::move(*storage));
    const auto report = state.summary_report("week");
    CHECK(report.sample_count == 10001);
}

TEST_CASE("AppState analytics aggregates predictions, hourly buckets, and app context") {
    auto state = make_state();
    state->start_session("Analyze focus", FocusMode::Normal);
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 1.0, "Cursor",
                                     "state.cpp - Snapback"));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 2.0, "Cursor",
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
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0, "Cursor"));

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

TEST_CASE("AppState summary distinguishes active from completed sessions without predictions") {
    auto storage = Storage::open_memory();
    REQUIRE(storage);
    auto state = std::make_unique<AppState>(std::move(*storage));
    const auto session = state->start_session("Permission recovery", FocusMode::Normal);

    auto active_report = state->summary_report("day");
    CHECK(active_report.session_count == 1);
    CHECK(active_report.completed_session_count == 0);
    CHECK(active_report.sample_count == 0);

    state->stop_session(session.session_id);
    auto completed_report = state->summary_report("day");
    CHECK(completed_report.session_count == 1);
    CHECK(completed_report.completed_session_count == 1);
    CHECK(completed_report.sample_count == 0);
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
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 100.0, "Cursor",
                                     "classifier.cpp - Snapback"));
    CHECK(state->latest_snapback() == std::nullopt);

    // 2. Switch to a clearly off-task window (distracting title) -> Distracted. The
    //    ContextTracker keys off on-task gating, not classifier thrash, so a distracting
    //    title is the deterministic lever.
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 101.0, "Google Chrome",
                                     "YouTube - Recommended"));
    CHECK(state->latest_snapback() == std::nullopt);

    // 3. Return to the IDE after > min_distraction (30s) -> the return edge fires the
    //    snapback for the remembered context.
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 140.0, "Cursor",
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
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 100.0, "Cursor",
                                     "classifier.cpp - Snapback"));
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 101.0, "Google Chrome",
                                     "YouTube - Recommended"));
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 140.0, "Cursor",
                                     "classifier.cpp - Snapback"));
    REQUIRE(state->latest_snapback().has_value());

    // Without dismiss_snapback(), ContextTracker has no other way out of Recovering —
    // dismiss_recovery() is its only caller — so a second distraction/return cycle would
    // silently produce no payload at all. Calling it here is what proves the fix, not
    // just that the pending payload got cleared.
    state->dismiss_snapback();
    CHECK(state->latest_snapback() == std::nullopt);

    // Second distraction/return cycle, well past the first: should fire again.
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 200.0, "Google Chrome",
                                     "YouTube - Recommended"));
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 240.0, "Cursor",
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

    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 10.0, "Cursor",
                                     "auth.ts - Snapback"));
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 11.0, "Google Chrome",
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
            AppStateTestAccess::process_event(*state, 
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

TEST_CASE("AppState live reads do not wait for engine mutation") {
    // The engine computes under its state lock. Live UI reads must use the published read
    // snapshot instead of joining that critical section, or an event burst (and especially
    // ONNX inference) turns directly into UI tail latency.
    auto state = make_state();
    const auto session = state->start_session("non-blocking live reads", FocusMode::Normal);
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0));

    std::atomic<bool> state_lock_held{false};
    std::atomic<bool> release_state_lock{false};
    std::thread holder([&] {
        AppStateTestAccess::while_holding_state_lock(*state, [&] {
            state_lock_held.store(true, std::memory_order_release);
            while (!release_state_lock.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });
    });

    while (!state_lock_held.load(std::memory_order_acquire)) std::this_thread::yield();

    std::atomic<bool> reads_finished{false};
    std::atomic<bool> reads_returned_expected_values{false};
    std::thread reader([&] {
        const auto health = state->health();
        const auto latest = state->latest_prediction();
        const auto active = state->active_session();
        const auto snapback = state->latest_snapback();
        const auto classifier = state->classifier_status();
        const auto permissions = state->refresh_permissions();
        (void)permissions;
        const bool idle = state->is_idle();
        reads_returned_expected_values.store(
            health.prediction_suppression_reason == "none" && latest.has_value() &&
                latest->session_id == session.session_id && active.has_value() &&
                active->session_id == session.session_id && !snapback.has_value() &&
                classifier.backend == "heuristic" && !idle,
            std::memory_order_release);
        reads_finished.store(true, std::memory_order_release);
    });

    // Give the reader a generous scheduling window while the mutation lock stays held. A
    // correct live-read seam finishes; the old getters block until that lock is released.
    for (int attempt = 0; attempt < 200 &&
                          !reads_finished.load(std::memory_order_acquire);
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool finished_while_state_locked = reads_finished.load(std::memory_order_acquire);

    release_state_lock.store(true, std::memory_order_release);
    holder.join();
    reader.join();

    CHECK(finished_while_state_locked);
    CHECK(reads_returned_expected_values.load(std::memory_order_acquire));
}

TEST_CASE("AppState empty live reads do not fall through to storage") {
    auto state = make_state();

    std::atomic<bool> locks_held{false};
    std::atomic<bool> release_locks{false};
    std::thread holder([&] {
        AppStateTestAccess::while_holding_state_and_storage_locks(*state, [&] {
            locks_held.store(true, std::memory_order_release);
            while (!release_locks.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });
    });
    while (!locks_held.load(std::memory_order_acquire)) std::this_thread::yield();

    std::atomic<bool> reads_finished{false};
    std::atomic<bool> reads_were_empty{false};
    std::thread reader([&] {
        reads_were_empty.store(!state->active_session().has_value() &&
                                   !state->latest_prediction().has_value(),
                               std::memory_order_release);
        reads_finished.store(true, std::memory_order_release);
    });

    for (int attempt = 0; attempt < 200 &&
                          !reads_finished.load(std::memory_order_acquire);
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool finished_while_locks_held = reads_finished.load(std::memory_order_acquire);

    release_locks.store(true, std::memory_order_release);
    holder.join();
    reader.join();

    CHECK(finished_while_locks_held);
    CHECK(reads_were_empty.load(std::memory_order_acquire));
}

TEST_CASE("AppState hydrates persisted live values before publishing its first snapshot") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("resume cached state", FocusMode::Deep);
    PredictionRecord prediction;
    prediction.session_id = session.session_id;
    prediction.focus_score = 81.0;
    prediction.distraction_risk = 0.1;
    prediction.focus_state = "PRODUCTIVE";
    prediction.goal_alignment = 0.9;
    prediction.timestamp = "2026-08-01T12:00:00Z";
    storage->insert_prediction(prediction);

    AppState state(std::move(*storage));

    REQUIRE(state.active_session().has_value());
    CHECK(state.active_session()->session_id == session.session_id);
    REQUIRE(state.latest_prediction().has_value());
    CHECK(state.latest_prediction()->focus_score == doctest::Approx(81.0));
}

TEST_CASE("AppState health reflects offline engine before capture starts") {
    auto state = make_state();
    auto health = state->health();

    CHECK(health.status == "offline");
    CHECK_FALSE(health.capture_running);
    CHECK(health.classifier.backend == "heuristic");
}

TEST_CASE("AppState destruction stops a running engine") {
    OneShotHook hook;
    {
        auto state = make_state();
        state->start_engine_for_test(&hook);
        for (int attempt = 0; attempt < 5000 && !hook.emitted(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(hook.emitted());
    }

    CHECK(hook.stopped());
}

TEST_CASE("AppState confirms capture only after the backend delivers an event") {
    auto state = make_state();
    CHECK_FALSE(state->health().permissions.capture_probe_confirmed);

    OneShotHook hook;
    state->start_engine_for_test(&hook);
    for (int attempt = 0; attempt < 5000 && !hook.emitted(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(hook.emitted());

    const auto health = state->health();
    CHECK(health.permissions.capture_probe_confirmed ==
          (health.permissions.capture_available &&
           health.permissions.active_window_available));
    state->stop_engine();
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

    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1.0));
    health = state->health();
    REQUIRE(health.last_prediction_age_secs.has_value());
    CHECK(*health.last_prediction_age_secs >= 0.0);
    CHECK(health.prediction_suppression_reason == "none");

    AppStateTestAccess::update_idle(*state, 0, true);
    AppStateTestAccess::update_idle(*state, kDefaultIdleThresholdMs, false);
    CHECK(state->health().prediction_suppression_reason == "idle");

    state->set_private_mode(true);
    CHECK(state->health().prediction_suppression_reason == "private_mode");
    state->set_private_mode(false);

    AppStateTestAccess::update_idle(*state, kDefaultIdleThresholdMs + 1, true);
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

    state->set_emit_hook([](const char*, const std::string&, AppState::ActivityEpoch) {
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

TEST_CASE("activity deletion invalidates asynchronously queued prediction emissions") {
    auto state = make_state();
    OneShotHook hook;
    std::mutex queue_mutex;
    std::condition_variable queue_changed;
    struct QueuedEvent {
        std::string name;
        AppState::ActivityEpoch epoch;
    };
    std::vector<QueuedEvent> queued;

    state->set_emit_hook([&](const char* name, const std::string&,
                             AppState::ActivityEpoch epoch) {
        if (std::string(name) != "prediction") return;
        std::lock_guard lock(queue_mutex);
        queued.push_back(QueuedEvent{name, epoch});
        queue_changed.notify_all();
    });
    state->start_session("Delete during emission", FocusMode::Normal);
    state->start_engine_for_test(&hook);

    {
        std::unique_lock lock(queue_mutex);
        REQUIRE(queue_changed.wait_for(lock, std::chrono::seconds(5),
                                       [&] { return !queued.empty(); }));
    }

    state->delete_all_activity_data();
    state->stop_engine();

    std::size_t delivered = 0;
    for (const auto& event : queued) {
        if (state->activity_epoch_is_current(event.epoch)) ++delivered;
    }
    CHECK(delivered == 0);
    CHECK(state->active_session() == std::nullopt);
    CHECK(state->prediction_history(10).empty());
}


TEST_CASE("AppState emits a hyperfocus nudge once the mode's window elapses") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    auto state = std::make_unique<AppState>(std::move(*storage), std::filesystem::path{});
    HyperfocusHook hook;

    std::mutex seen_mutex;
    std::vector<std::string> events;
    std::string payload;
    state->set_emit_hook([&](const char* name, const std::string& body,
                             AppState::ActivityEpoch) {
        std::lock_guard lock(seen_mutex);
        events.emplace_back(name);
        if (std::string(name) == "hyperfocus") payload = body;
    });

    state->start_session("ship the overlay", FocusMode::Normal);
    state->start_engine_for_test(&hook);

    bool fired = false;
    for (int attempt = 0; attempt < 5000 && !fired; ++attempt) {
        {
            std::lock_guard lock(seen_mutex);
            fired = std::count(events.begin(), events.end(), "hyperfocus") > 0;
        }
        if (!fired) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    state->stop_engine();

    CHECK(fired);
    if (!fired) return;

    // The frontend reads `message`; main.cpp rebuilds the toast from `minutes`.
    const auto parsed = nlohmann::json::parse(payload);
    CHECK(parsed.contains("message"));
    CHECK(parsed.at("minutes").get<std::uint64_t>() >= 120);

    // Latched: one unbroken stretch produces exactly one nudge, not one per tick.
    std::lock_guard lock(seen_mutex);
    CHECK(std::count(events.begin(), events.end(), "hyperfocus") == 1);
}

// Roadmap 7.6 — the legible export, end to end through the real storage path. The renderer is
// unit-tested in test_data_export.cpp; what is only observable here is that the rows reaching
// it are the rows the database actually holds.
TEST_CASE("AppState exports a legible archive of what was recorded") {
    auto state = make_state();
    const auto session = state->start_session("write the export", FocusMode::Normal);
    AppStateTestAccess::process_event(*state, ev(EventType::WindowFocusChange, 1000.0, "Cursor"));
    AppStateTestAccess::process_event(*state, ev(EventType::KeyPress, 1002.0, "Cursor"));
    state->stop_session(session.session_id);

    TempDir temp;
    const auto exported = state->export_personal_data(temp.path);

    CHECK(exported.session_count == 1);
    CHECK_FALSE(exported.truncated);
    CHECK(std::filesystem::exists(exported.output_path));

    const auto markdown = read_file(exported.output_path);
    CHECK(markdown.find("# Your Snapback data") != std::string::npos);
    CHECK(markdown.find("write the export") != std::string::npos);
    CHECK(markdown.find(session.session_id) != std::string::npos);
    // The export must name what it holds before the user forwards it anywhere.
    CHECK(markdown.find("window titles") != std::string::npos);
}

TEST_CASE("AppState admits when the archive left older sessions out") {
    auto state = make_state();
    for (int i = 0; i < 3; ++i) {
        const auto session =
            state->start_session("session " + std::to_string(i), FocusMode::Normal);
        state->stop_session(session.session_id);
    }

    TempDir temp;
    // Caps are parameters precisely so this case is reachable without writing thousands of
    // rows: two of three sessions exported means the "there is more" line must appear.
    const auto exported = state->export_personal_data(temp.path, /*session_limit=*/2);

    CHECK(exported.session_count == 2);
    CHECK(exported.truncated);
    CHECK(read_file(exported.output_path).find("older sessions exist beyond this export's limit") !=
          std::string::npos);
}

TEST_CASE("AppState exports a document, not an empty file, with no history") {
    auto state = make_state();

    TempDir temp;
    const auto exported = state->export_personal_data(temp.path);

    CHECK(exported.session_count == 0);
    CHECK(exported.window_count == 0);
    const auto markdown = read_file(exported.output_path);
    CHECK_FALSE(markdown.empty());
    CHECK(markdown.find("No sessions have been recorded yet") != std::string::npos);
}
