#include "doctest_wrapper.hpp"

#include "manual_clock.hpp"
#include "util/cached_probe.hpp"

using namespace snapback;

namespace {

struct CountingProbe {
    int calls = 0;
    bool answer = true;
    CachedProbe make(std::int64_t ttl_ms) {
        return CachedProbe([this] { ++calls; return answer; }, ttl_ms);
    }
};

}  // namespace

TEST_CASE("cached probe runs once inside the TTL") {
    CountingProbe counting;
    auto probe = counting.make(30'000);
    ManualClock clock;

    CHECK(probe.value(clock));
    clock.advance_seconds(5);
    CHECK(probe.value(clock));
    clock.advance_seconds(24);
    CHECK(probe.value(clock));
    // Six health polls' worth of reads, one process spawn.
    CHECK(counting.calls == 1);
}

TEST_CASE("cached probe re-probes once the TTL has elapsed") {
    CountingProbe counting;
    auto probe = counting.make(30'000);
    ManualClock clock;

    CHECK(probe.value(clock));
    counting.answer = false;
    clock.advance_ms(29'999);
    // Still inside the window: the stale answer is the contract, not a bug.
    CHECK(probe.value(clock));
    clock.advance_ms(1);
    CHECK_FALSE(probe.value(clock));
    CHECK(counting.calls == 2);
}

TEST_CASE("invalidating a cached probe forces the next read to re-probe") {
    CountingProbe counting;
    auto probe = counting.make(30'000);
    ManualClock clock;

    CHECK(probe.value(clock));
    counting.answer = false;
    probe.invalidate();
    // No time has passed; the user asked for a fresh look, so they get one.
    CHECK_FALSE(probe.value(clock));
    CHECK(counting.calls == 2);
}

TEST_CASE("cached probe with a zero TTL never caches") {
    CountingProbe counting;
    auto probe = counting.make(0);
    ManualClock clock;

    probe.value(clock);
    probe.value(clock);
    CHECK(counting.calls == 2);
}
