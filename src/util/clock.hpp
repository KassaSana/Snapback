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
// elapsed time. See ROADMAP 7.16 for the separate question of how a timestamp is *represented*
// once it has been read — this file is about where the reading comes from, not its format.
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

    // Wall-clock time, for stamping records a human will read. May jump in either direction.
    virtual std::time_t wall_time() const = 0;
};

// The production clock: the real one.
class SystemClock final : public Clock {
public:
    std::int64_t steady_ms() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::time_t wall_time() const override { return std::time(nullptr); }
};

}  // namespace snapback
