// The only door to AppState's private test seams. ROADMAP 7.14.
//
// Those three methods used to be public, which meant the shipping binary exported an API
// telling anyone reading the header that `process_event_for_test` was a supported way to feed
// the engine. It was not. `AppState` grants this struct friendship and nothing else, so the
// seams are reachable from tests and from nowhere else.
//
// Why they still exist at all: their production caller is the engine *tick thread*, not a
// method. Driving idle or pomodoro transitions through public API would mean starting the
// engine and waiting out real durations — the sleep-based testing 11.4's clock exists to
// avoid. `start_pomodoro_for_test` was the one the clock did make redundant, and it is gone;
// a test now sets a `ManualClock` and calls the real `start_pomodoro()`.
//
// This header lives under tests/ so it is not part of the shipping tree.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/state.hpp"

namespace snapback {

struct AppStateTestAccess {
    static void while_holding_state_lock(AppState& state, const std::function<void()>& body) {
        std::lock_guard lock(state.mutex_);
        body();
    }

    static void while_holding_state_and_storage_locks(AppState& state,
                                                       const std::function<void()>& body) {
        std::lock_guard state_lock(state.mutex_);
        std::lock_guard storage_lock(state.storage_mutex_);
        body();
    }

    static void while_holding_storage_lock(AppState& state,
                                           const std::function<void()>& body) {
        std::lock_guard storage_lock(state.storage_mutex_);
        body();
    }

    static void process_event(AppState& state, const CaptureEvent& event) {
        state.process_event_for_test(event);
    }

    static IdleTransition update_idle(AppState& state, std::int64_t now_ms, bool had_input) {
        return state.update_idle_for_test(now_ms, had_input);
    }

    static std::optional<PomodoroStatus> update_pomodoro(AppState& state, std::int64_t now_ms) {
        return state.update_pomodoro_for_test(now_ms);
    }

    static FeatureVector extract_features(AppState& state, double now_secs) {
        return state.extract_features_for_test(now_secs);
    }

    // One synchronous turn of the real engine tick — the same function the engine thread
    // runs, not a reimplementation. Roadmap 7.23 needed it because session pause/resume is
    // driven by the tick's idle edges, so testing it through `update_idle_for_test` alone
    // would exercise the detector and skip everything that acts on it. Points the same way
    // as 14.2.
    // Returns what the engine loop reads: true if the drain stopped on a budget with events
    // still queued, false if it emptied the ring.
    static bool engine_tick(AppState& state) { return state.engine_tick(); }

    // Starts the capture producer WITHOUT the engine thread. `start_engine_for_test` starts
    // both, which makes "how much does one tick drain" a race against a thread already
    // draining; this lets a test fill the ring and then drive engine_tick() by hand.
    static void start_capture_only(AppState& state, InputHook* hook) {
        state.capture_.start(hook);
    }

    static void stop_capture(AppState& state) noexcept { state.capture_.stop(); }

    // Whether capture still has events the engine has not taken. Lets a test assert that
    // shutdown or a deletion left nothing behind, which is otherwise invisible.
    static bool capture_has_pending(const AppState& state) {
        return state.capture_.has_pending_events();
    }

    static bool maintenance_pending(const AppState& state) {
        return state.maintenance_pending_.load(std::memory_order_acquire);
    }

    static bool maintenance_paused(const AppState& state) {
        return state.maintenance_paused_.load(std::memory_order_acquire);
    }

    // Whether the session currently has an attended span open (Roadmap 7.23). Reaches
    // through to the owned Storage because AppState deliberately exposes no such getter —
    // "is a span open" is internal bookkeeping, not something a UI should ask.
    //
    // The wiring is what AppState-level tests can honestly check: Storage stamps its own
    // timestamps from the system clock and has no injected one, so a ManualClock here cannot
    // move a duration. The span arithmetic is proven in test_storage.cpp, where timestamps
    // are passed in explicitly.
    static bool has_open_span(AppState& state, const std::string& session_id) {
        std::lock_guard lock(state.storage_mutex_);
        return state.storage_.has_open_span(session_id);
    }

    // Writes a prediction with a caller-chosen timestamp, through the owned Storage. Rows
    // normally get their timestamp from the clock that wrote them, so there is no ordinary
    // way to make one *old* -- which is exactly what a retention test needs.
    static void insert_prediction_at(AppState& state, const std::string& session_id,
                                     std::int64_t timestamp_ms) {
        std::lock_guard lock(state.storage_mutex_);
        PredictionRecord record;
        record.session_id = session_id;
        record.focus_score = 50.0;
        record.distraction_risk = 0.2;
        record.focus_state = "PRODUCTIVE";
        record.timestamp_ms = timestamp_ms;
        state.storage_.insert_prediction(record);
    }

    // Stages the span decision phase 1 of the tick records when the user comes back from
    // idle, without having to drive a real idle cycle. The interleave AUD-04b describes --
    // decision recorded, session stopped, decision drained -- is a race between two lock
    // regions, so reproducing it by timing would be flaky by construction; staging the
    // decision makes the same sequence deterministic.
    static void stage_pending_span_open(AppState& state, const std::string& session_id) {
        std::lock_guard lock(state.mutex_);
        state.pending_span_transitions_.push_back(AppState::PendingSpanTransition{
            ++state.next_span_transition_id_, session_id, true, 0, std::nullopt});
    }

    // The session a pending span decision names, if any.
    static std::optional<std::string> pending_span_session(AppState& state) {
        std::lock_guard lock(state.mutex_);
        if (state.pending_span_transitions_.empty()) return std::nullopt;
        return state.pending_span_transitions_.front().session_id;
    }

    static std::size_t pending_span_count(AppState& state) {
        std::lock_guard lock(state.mutex_);
        return state.pending_span_transitions_.size();
    }

    static std::optional<std::int64_t> pending_span_timestamp(AppState& state) {
        std::lock_guard lock(state.mutex_);
        if (state.pending_span_transitions_.empty()) return std::nullopt;
        return state.pending_span_transitions_.front().timestamp_ms;
    }

    static bool committed_attendance(AppState& state) {
        std::lock_guard lock(state.mutex_);
        return state.committed_session_attended_;
    }

    static void fail_next_persistence_at(AppState& state, std::string stage) {
        std::lock_guard lock(state.mutex_);
        const auto fired = std::make_shared<bool>(false);
        state.persistence_test_hook_ =
            [stage = std::move(stage), fired](const char* current) {
                if (!*fired && stage == current) {
                    *fired = true;
                    throw std::runtime_error("injected persistence failure at " + stage);
                }
            };
    }

    static void clear_persistence_failure(AppState& state) {
        std::lock_guard lock(state.mutex_);
        state.persistence_test_hook_ = nullptr;
    }

    // The *live* focus mode driving the classifier right now (Roadmap 7.25). Not the same
    // thing as `settings().default_focus_mode`, which is only what the next session starts
    // with — the difference is precisely the bug: a restarted Deep session used to come back
    // being classified under Normal's threshold with nothing on screen saying so.
    static FocusMode focus_mode(AppState& state) {
        std::lock_guard lock(state.mutex_);
        return state.focus_mode_;
    }

    // Roadmap 2.15's episodes, read back through the owned Storage. AppState exposes no
    // getter yet — 2.9 and 10.11 own the surface that will show them — but the rows have to be
    // assertable now, because the whole point of the item is that a write nobody could observe
    // was missing for the entire life of the project.
    static std::vector<SnapbackEpisode> snapback_episodes(AppState& state,
                                                          const std::string& session_id) {
        std::lock_guard lock(state.storage_mutex_);
        return state.storage_.list_snapback_episodes(session_id, 100);
    }

    // Stages a pending snapback payload without driving a real drift-and-recover cycle.
    //
    // Production sets this from the context tracker inside the tick, which needs a sequence of
    // events across a focus transition to reach. The lifecycle question -- does this payload
    // survive a session change -- is independent of how it got there, so staging it directly
    // tests the thing that was actually broken instead of re-testing the tracker.
    static void stage_snapback(AppState& state, const SnapbackPayload& payload) {
        std::lock_guard lock(state.mutex_);
        state.latest_snapback_ = payload;
        state.snapback_emitted_ = true;  // as it stands after the tick has emitted the event
        state.live_read_dirty_ = true;
        state.publish_live_read_unlocked();
    }

    // Sets the alert delivery preferences directly. Roadmap 2.16.
    //
    // The production path for these is a settings command plus a settings.json write, and
    // neither is what a delivery case is about: driving them would put a filesystem in the way
    // of a question about whether an interruption fires. The snooze deadline has its own
    // public API (`snooze_alerts_for`) and the case for *that* uses it.
    static void set_alert_settings(AppState& state, const AlertDeliverySettings& alerts) {
        std::lock_guard lock(state.mutex_);
        state.settings_.alerts = alerts;
    }

    // Moves the activity boundary the way a delete does, and nothing else. AUD-07's failure
    // needs the epoch to change *between* the tick latching it and the tick checking it --
    // a window with no lock held, which is why reproducing it with a second thread would be
    // a race dressed as a test. Called from a clock the tick reads inside that window, this
    // makes the same interleave deterministic.
    static void bump_activity_epoch(AppState& state) {
        state.activity_epoch_.fetch_add(1, std::memory_order_release);
    }

    static AppState::ActivityEpoch activity_epoch(const AppState& state) noexcept {
        return state.activity_epoch_.load(std::memory_order_acquire);
    }

    static bool snapback_emitted(const AppState& state) {
        std::lock_guard lock(state.mutex_);
        return state.snapback_emitted_;
    }

    // The owned Storage, unlocked. Deliberately not wrapped in a storage_mutex_ guard the
    // way the helpers above are: its one use is to hold a Storage::Transaction open *across*
    // a synchronous engine_tick, and a guard here would deadlock the tick that has to run
    // inside it. Safe only because these tests drive the tick by hand, with no engine thread
    // started -- nothing else touches the connection.
    static Storage& storage(AppState& state) { return state.storage_; }

    static bool insert_episode(AppState& state, const SnapbackEpisode& episode) {
        std::lock_guard lock(state.storage_mutex_);
        return state.storage_.insert_snapback_episode(episode);
    }
};

}  // namespace snapback
