// Concurrency stress test for the hand-rolled lock-free SPSC ring buffer. The
// single-threaded ordering/full-state case lives in test_engine.cpp; this one runs a real
// producer thread against a real consumer thread to exercise the acquire/release memory
// ordering (the part CLAUDE.md calls the riskiest — Rust's Send/Sync gave it to us free).
#include <doctest/doctest.h>

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
