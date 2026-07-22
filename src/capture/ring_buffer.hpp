// Lock-free single-producer / single-consumer ring buffer.
//
// In Rust, rdev hands events to a bounded channel and the borrow checker + the
// channel type guarantee no data race. Here we hand-roll it: the OS hook thread
// is the sole producer, the engine thread is the sole consumer. Correctness rests
// on the two atomics below and the acquire/release ordering — this is exactly the
// kind of code Rust made safe for free and C++ makes your responsibility.
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
    // drop, mirroring HealthStatus::capture_events_dropped in the Rust engine.
    bool push(T value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        slots_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
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
    alignas(64) std::atomic<std::size_t> tail_{0};  // written by consumer only
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace snapback
