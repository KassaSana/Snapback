// Lock-free single-producer / single-consumer ring buffer.
//
// The OS hook thread is the sole producer, the engine thread is the sole consumer.
// Correctness rests on the two atomics below and their acquire/release ordering.
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace snapback {

#if defined(_MSC_VER)
#pragma warning(push)
// C4324: the alignas(64) on head_/tail_ pads the struct — that padding is the whole point
// (cache-line isolation to avoid false sharing), so silence the "was padded" warning.
#pragma warning(disable : 4324)
#endif

template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    // Producer side (OS hook thread). Returns false if full -> caller counts a
    // drop, exposed through HealthStatus::capture_events_dropped.
    bool push(T value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (next == tail) {
            return false;  // full
        }
        slots_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
        // ROADMAP 14.11. `capture_events_dropped` only moves once the ring has already
        // overflowed, which makes it a report of damage rather than of headroom: a run that
        // peaked at 65,000 of 65,536 slots and one that never passed ten look identical.
        // The occupancy is free here -- both ends were just loaded -- and this is the
        // producer, the only thread that writes it, so a plain load/store is enough.
        const std::size_t occupancy = (next - tail) & kMask;
        if (occupancy > high_water_.load(std::memory_order_relaxed)) {
            high_water_.store(occupancy, std::memory_order_relaxed);
        }
        return true;
    }

    // Consumer side (engine thread).
    std::optional<T> pop() {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        T value = std::move(slots_[tail]);
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return value;
    }

    // Consumer side. Non-destructive "is there anything to pop", so a bounded drain can tell
    // "I stopped because my budget ran out" from "I stopped because I emptied the ring"
    // without consuming the event that would answer it. Only meaningful on the consumer
    // thread: the producer can make a false reading true a moment later, which is harmless
    // here (the next tick sees it) and is why this is not used for correctness decisions.
    // The deepest this ring has ever been, in events. Read from any thread; it can be one
    // push stale, which is immaterial for a high-water mark.
    std::size_t high_water() const { return high_water_.load(std::memory_order_relaxed); }

    bool has_pending() const {
        return tail_.load(std::memory_order_relaxed) != head_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    // Heap, not std::array: 65,536 CaptureEvents is ~6 MB, which silently lived in
    // whatever storage the *owner* chose. A stack-allocated CaptureThread (or AppState,
    // which holds one by value) blew Windows' 1 MB default thread stack — see Roadmap 6.1.
    // Allocated once here, never resized; the hot-path push/pop never touch the pointer.
    std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
    // alignas(64): keep head_ (producer-written) and tail_ (consumer-written) on separate
    // cache lines. Adjacent atomics would share a line and ping-pong it between the two
    // threads on every push/pop (false sharing); 64 bytes is the common x86/ARM line size.
    alignas(64) std::atomic<std::size_t> head_{0};  // written by producer only
    // Producer-written like head_, so it shares that cache line on purpose: putting it on
    // its own would cost a third line to no benefit, and pairing it with tail_ would
    // reintroduce exactly the false sharing the alignment above exists to prevent.
    std::atomic<std::size_t> high_water_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};  // written by consumer only
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace snapback
