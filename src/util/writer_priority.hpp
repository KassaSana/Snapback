// Let a waiting writer go before the next reader. std::mutex is unfair: a Review load of five
// commands on one thread can reacquire between commands before the blocked engine thread runs,
// so a persist waited out the whole load. A writer announces itself while waiting; readers
// about to lock wait for announced writers. The writer still waits for the current holder.
//
// Only yield while holding no lock the writer needs, or it deadlocks.
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace snapback {

class WriterPriority {
public:
    WriterPriority() = default;
    WriterPriority(const WriterPriority&) = delete;
    WriterPriority& operator=(const WriterPriority&) = delete;

    // Held by a writer from before it asks for the lock until it has it. Release as soon as
    // the lock is taken: a reader that yielded would otherwise go on yielding for the whole
    // write, when the mutex already makes it wait for exactly that long.
    class Announce {
    public:
        explicit Announce(WriterPriority& gate) : gate_(&gate) {
            gate_->waiting_.fetch_add(1, std::memory_order_acq_rel);
        }
        ~Announce() { release(); }
        Announce(const Announce&) = delete;
        Announce& operator=(const Announce&) = delete;

        void release() {
            if (gate_ == nullptr) return;
            gate_->waiting_.fetch_sub(1, std::memory_order_acq_rel);
            gate_ = nullptr;
        }

    private:
        WriterPriority* gate_;
    };

    // Returns once no writer is waiting. A writer's wait for the mutex is at most one reader
    // hold, so this spins briefly and then sleeps in short slices rather than burning a core.
    void yield_to_writers() const {
        for (int spin = 0; waiting_.load(std::memory_order_acquire) > 0; ++spin) {
            if (spin < 64) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        }
    }

    int waiting() const { return waiting_.load(std::memory_order_acquire); }

private:
    std::atomic<int> waiting_{0};
};

}  // namespace snapback
