// Where time comes from.
//
// ROADMAP 11.4. `AppState` read the clock directly in two static helpers, which forced every
// time-dependent test to either sleep or go through a `_for_test` method that takes `now_ms`
// as an argument. Those methods are 7.14's complaint — test-only API compiled into the
// shipping binary — and the roadmap is explicit that they exist *because* the tick reads a
// real clock. This is the seam that removes the reason for them.
//
// The cost of not having it is not just ugly API. Sleep-based tests can only exercise
// durations short enough to wait for, so the idle threshold, the pomodoro's 25 minutes, and
// the one-prediction-per-second throttle have never been tested at the scales they actually
// run at. A test that advances an hour instantly can.
//
// **Two clocks, deliberately.** A monotonic one for durations and a wall clock for
// timestamps, because they answer different questions and must not be conflated: wall time
// can jump backwards across a DST change or an NTP correction, which would make an idle
// timer conclude the user has been away for -1 hour. `steady_ms()` is the only one used for
// elapsed time. How a wall reading is *represented* once taken is ADR-0007's question, not
// this file's; `util/time.hpp` holds the conversions to and from the display format.
#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>

namespace snapback {

class Clock {
public:
    virtual ~Clock() = default;

    // Monotonic milliseconds since an arbitrary epoch. Only differences are meaningful. Never
    // goes backwards, which is the whole reason it is separate from wall time.
    virtual std::int64_t steady_ms() const = 0;

    // Wall-clock time as UTC milliseconds since the Unix epoch — ADR-0007's one
    // representation of a point in time. May jump in either direction.
    //
    // Milliseconds rather than seconds because ordering has to be defined at the resolution
    // the app writes: two sessions started in the same wall-clock second tie under
    // `ORDER BY started_at DESC`, and which one the history list shows first is then
    // arbitrary. That is a real user-visible symptom, not only a test annoyance.
    virtual std::int64_t wall_ms() const = 0;

    // Whole seconds, for the callers that have not moved to milliseconds yet.
    //
    // Deliberately **not** virtual: derived from `wall_ms()` so an implementation cannot let
    // the two readings disagree, which is exactly the drift a second representation invites.
    // Floor division rather than truncation, so the second a timestamp falls in does not
    // shift for a pre-epoch value.
    std::time_t wall_time() const {
        const std::int64_t ms = wall_ms();
        const std::int64_t secs = ms >= 0 ? ms / 1000 : (ms - 999) / 1000;
        return static_cast<std::time_t>(secs);
    }
};

// The production clock: the real one.
class SystemClock final : public Clock {
public:
    std::int64_t steady_ms() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::int64_t wall_ms() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
};

}  // namespace snapback
