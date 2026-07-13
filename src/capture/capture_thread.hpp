// Owns the capture thread + the event ring buffer. Rust: capture/thread.rs.
//
// Producer: the InputHook callback (OS thread) pushes into the ring buffer.
// Consumer: the engine drains it on its own tick. This class is the seam between
// them and the place drop-counting lives.
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>

#include "capture/input_hook.hpp"
#include "capture/ring_buffer.hpp"
#include "types.hpp"

namespace snapback {

class CaptureThread {
public:
    // 65,536 events ≈ 1.3s at 50k/s, matching the Rust ring buffer sizing note.
    static constexpr std::size_t kCapacity = 1u << 16;

    void start();
    void stop();

    // Engine side: drain one event, or nullopt if the buffer is empty.
    std::optional<CaptureEvent> next_event() { return buffer_.pop(); }

    std::uint64_t events_dropped() const { return dropped_.load(std::memory_order_relaxed); }
    bool running() const { return running_.load(std::memory_order_relaxed); }

private:
    RingBuffer<CaptureEvent, kCapacity> buffer_;
    std::thread hook_thread_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<bool> running_{false};
};

}  // namespace snapback
