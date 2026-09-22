// Owns the capture thread and the event ring buffer.
//
// Producer: the InputHook callback (OS thread) pushes into the ring buffer.
// Consumer: the engine drains it on its own tick. This class is the seam between
// them and the place drop-counting lives.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "capture/input_hook.hpp"
#include "capture/ring_buffer.hpp"
#include "types.hpp"

namespace snapback {

class CaptureThread {
public:
    // 65,536 events ≈ 1.3s at 50k/s.
    static constexpr std::size_t kCapacity = 1u << 16;

    // `hook` defaults to the platform singleton (InputHook::instance()). Tests inject a
    // fake to drive the producer side deterministically without installing real OS hooks —
    // this layer had no coverage at all before that seam existed.
    //
    // Calling start() twice without stop() is a no-op, not a crash: assigning over a
    // joinable std::thread calls std::terminate. That invariant used to live in the caller
    // (AppState::start_engine's CAS); it belongs here.
    ~CaptureThread() noexcept { stop(); }
    void start(InputHook* hook = nullptr);
    void stop() noexcept;

    // Engine side: drain one event, or nullopt if the buffer is empty.
    std::optional<CaptureEvent> next_event() {
        auto event = buffer_.pop();
        if (event) event->materialize_captured_context();
        return event;
    }

    // Engine side: whether a further next_event() would return an event right now.
    bool has_pending_events() const { return buffer_.has_pending(); }

    // Engine side: throw away everything queued, returning how many events went.
    //
    // The ring is single-producer/single-consumer, so this must not run while the engine is
    // draining. AppState's callers satisfy that by holding the same state lock the drain holds
    // -- the two pop sites are mutually exclusive, never concurrent.
    //
    // "Delete my activity" has to erase what was captured before it, including what is still
    // in this queue -- otherwise the tick after the deletion files pre-deletion window titles
    // into whatever session exists by then. Bounded by kCapacity rather than by "until empty"
    // so a producer that keeps pushing cannot hold the caller here indefinitely; anything it
    // pushes after the boundary is, correctly, post-deletion activity.
    std::size_t discard_pending_events() {
        std::size_t discarded = 0;
        while (discarded < kCapacity && buffer_.pop()) ++discarded;
        return discarded;
    }

    std::uint64_t events_dropped() const { return dropped_.load(std::memory_order_relaxed); }
    // The ring's deepest occupancy so far. `events_dropped()` says the buffer overflowed;
    // this says how close it came before it did, which is the half a capacity decision needs.
    std::size_t ring_high_water() const { return buffer_.high_water(); }
    bool running() const { return running_.load(std::memory_order_relaxed); }
    bool failed() const { return failed_.load(std::memory_order_acquire); }
    bool input_observed() const {
        return input_observed_.load(std::memory_order_acquire);
    }
    std::optional<std::string> failure_reason() const;
    std::optional<std::int64_t> last_event_age_ms() const;

private:
    static std::int64_t steady_now_ms();
    void record_failure(const char* reason) noexcept;

    RingBuffer<CaptureEvent, kCapacity> buffer_;
    InputHook* hook_ = nullptr;  // borrowed; owned by the singleton or the test
    std::thread hook_thread_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> has_event_{false};
    std::atomic<bool> input_observed_{false};
    std::atomic<std::int64_t> last_event_ms_{0};
    mutable std::mutex failure_mutex_;
    std::string failure_reason_;
};

}  // namespace snapback
