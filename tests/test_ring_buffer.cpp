// Concurrency stress test for the hand-rolled lock-free SPSC ring buffer. The
// single-threaded ordering/full-state case lives in test_engine.cpp; this one runs a real
// producer thread against a real consumer thread to exercise the acquire/release memory
// ordering at the capture pipeline's riskiest cross-thread boundary.
#include "doctest_wrapper.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

#include "capture/ring_buffer.hpp"

using namespace snapback;

namespace {
// Two fields with the invariant b == ~a. If the consumer ever reads a slot the producer
// published before fully writing (a broken release/acquire), a and b won't be consistent.
struct Item {
    std::uint64_t a{};
    std::uint64_t b{};
};
}  // namespace

TEST_CASE("RingBuffer is FIFO-correct and race-free under concurrent producer/consumer") {
    constexpr std::uint64_t kCount = 500000;
    // Small capacity relative to the volume forces constant full/empty transitions, which
    // is where a memory-ordering bug would surface.
    RingBuffer<Item, 1024> buffer;

    std::atomic<bool> torn{false};
    std::atomic<std::uint64_t> received{0};

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        while (expected < kCount) {
            if (auto item = buffer.pop()) {
                if (item->a != expected || item->b != ~item->a) {
                    torn.store(true, std::memory_order_relaxed);
                    return;  // ordering broken or slot read before fully written
                }
                ++expected;
                received.store(expected, std::memory_order_relaxed);
            }
        }
    });

    for (std::uint64_t i = 0; i < kCount;) {
        if (buffer.push(Item{i, ~i})) {
            ++i;  // published in order; consumer must see exactly this sequence
        }
        // else: buffer full — spin until the consumer drains a slot.
    }

    consumer.join();

    CHECK_FALSE(torn.load());               // every slot was consistent (no publish race)
    CHECK(received.load() == kCount);        // every value arrived exactly once, in order
}

TEST_CASE("the ring remembers how deep it ever got, not just that it overflowed") {
    // ROADMAP 14.11. `capture_events_dropped` only moves after the ring has already
    // overflowed, so it reports damage rather than headroom -- a run that peaked one slot
    // short of full and one that never passed two look identical through it. The high-water
    // mark is what turns "we never dropped an event" into a statement about margin.
    RingBuffer<Item, 8> buffer;
    CHECK(buffer.high_water() == 0);

    for (int i = 0; i < 5; ++i) REQUIRE(buffer.push(Item{static_cast<std::uint64_t>(i), 0}));
    CHECK(buffer.high_water() == 5);

    // Draining does not lower it: the mark answers "how close did this ever come", and a
    // figure that decayed back to the current depth would report a quiet moment as headroom
    // the run never had.
    for (int i = 0; i < 5; ++i) REQUIRE(buffer.pop().has_value());
    CHECK(buffer.high_water() == 5);

    // A power-of-two ring holds Capacity - 1 items -- the slot that would make head meet
    // tail is what distinguishes full from empty -- so the mark tops out there and the
    // failed push leaves it alone rather than counting an event that was never stored.
    for (int i = 0; i < 7; ++i) REQUIRE(buffer.push(Item{static_cast<std::uint64_t>(i), 0}));
    CHECK(buffer.high_water() == 7);
    CHECK_FALSE(buffer.push(Item{99, 0}));
    CHECK(buffer.high_water() == 7);
}
