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

    // The *live* focus mode driving the classifier right now (Roadmap 7.25). Not the same
    // thing as `settings().default_focus_mode`, which is only what the next session starts
    // with — the difference is precisely the bug: a restarted Deep session used to come back
    // being classified under Normal's threshold with nothing on screen saying so.
    static FocusMode focus_mode(AppState& state) {
        std::lock_guard lock(state.mutex_);
        return state.focus_mode_;
    }
};

}  // namespace snapback
