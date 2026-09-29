// Where time comes from, injectable so tests can advance hours instantly. Two clocks: a
// monotonic one for durations (steady_ms) and a wall clock for timestamps (wall time can jump
// across DST or NTP corrections). Formatting lives in util/time.hpp.
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

    // UTC epoch milliseconds (ADR-0007). May jump in either direction.
    virtual std::int64_t wall_ms() const = 0;

    // Whole seconds, derived from wall_ms() (not virtual, so they cannot disagree). Floor
    // division for pre-epoch values.
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
