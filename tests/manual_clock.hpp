// A clock a test drives by hand. ROADMAP 11.4.
//
// Lives under tests/ rather than src/ deliberately: 7.14's complaint is test-only API
// compiled into the shipping binary, and it would be a poor answer to that to ship a fake
// clock in the production tree.
#pragma once

#include <cstdint>
#include <ctime>

#include "util/clock.hpp"

namespace snapback {

class ManualClock final : public Clock {
public:
    // Defaults are arbitrary but fixed: a nonzero steady origin catches code that assumes
    // "monotonic time starts at 0", and a fixed wall time makes stamped records comparable
    // across runs. 1'700'000'000 is 2023-11-14T22:13:20Z.
    //
    // The wall parameter stays in *seconds* even though the clock now reads milliseconds:
    // every existing caller passes a second-resolution constant, and a unit change that
    // silently compiles would move each of them a thousandfold. `set_wall_ms` is the
    // millisecond seam for the cases that need one.
    explicit ManualClock(std::int64_t steady_ms = 1'000'000, std::time_t wall = 1'700'000'000)
        : steady_ms_(steady_ms), wall_ms_(static_cast<std::int64_t>(wall) * 1000) {}

    std::int64_t steady_ms() const override { return steady_ms_; }
    std::int64_t wall_ms() const override { return wall_ms_; }

    // Moves both clocks forward together, which is what real elapsed time does. Tests that
    // need them to diverge (an NTP correction, a DST jump) set them separately.
    //
    // The wall side advances by the full delta rather than by `delta / 1000` seconds, so a
    // sub-second advance is no longer silently rounded away to nothing.
    void advance_ms(std::int64_t delta) {
        steady_ms_ += delta;
        wall_ms_ += delta;
    }

    void advance_seconds(std::int64_t seconds) { advance_ms(seconds * 1000); }
    void advance_minutes(std::int64_t minutes) { advance_seconds(minutes * 60); }

    void set_steady_ms(std::int64_t value) { steady_ms_ = value; }
    void set_wall_time(std::time_t value) { wall_ms_ = static_cast<std::int64_t>(value) * 1000; }
    void set_wall_ms(std::int64_t value) { wall_ms_ = value; }

private:
    std::int64_t steady_ms_;
    std::int64_t wall_ms_;
};

}  // namespace snapback
