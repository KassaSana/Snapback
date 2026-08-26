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

    static void process_event(AppState& state, const CaptureEvent& event) {
        state.process_event_for_test(event);
    }

    static IdleTransition update_idle(AppState& state, std::int64_t now_ms, bool had_input) {
        return state.update_idle_for_test(now_ms, had_input);
    }

    static std::optional<PomodoroStatus> update_pomodoro(AppState& state, std::int64_t now_ms) {
        return state.update_pomodoro_for_test(now_ms);
    }

    // One synchronous turn of the real engine tick — the same function the engine thread
    // runs, not a reimplementation. Roadmap 7.23 needed it because session pause/resume is
    // driven by the tick's idle edges, so testing it through `update_idle_for_test` alone
    // would exercise the detector and skip everything that acts on it. Points the same way
    // as 14.2.
    static void engine_tick(AppState& state) { state.engine_tick(); }

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
        state.pending_span_session_ = session_id;
        state.pending_span_opens_ = true;
        state.pending_span_secs_ago_ = 0;
    }

    // The session a pending span decision names, if any.
    static std::optional<std::string> pending_span_session(AppState& state) {
        std::lock_guard lock(state.mutex_);
        return state.pending_span_session_;
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

    // Moves the activity boundary the way a delete does, and nothing else. AUD-07's failure
    // needs the epoch to change *between* the tick latching it and the tick checking it --
    // a window with no lock held, which is why reproducing it with a second thread would be
    // a race dressed as a test. Called from a clock the tick reads inside that window, this
    // makes the same interleave deterministic.
    static void bump_activity_epoch(AppState& state) {
        state.activity_epoch_.fetch_add(1, std::memory_order_release);
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
