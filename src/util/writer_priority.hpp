// Let a waiting writer go before the next reader. Roadmap 14.1.
//
// `storage_mutex_` is a `RankedMutex` over a `std::mutex`, and `std::mutex` promises no
// fairness. A Review load is five commands on one thread, each taking and releasing the lock;
// between two of them the releasing thread is already running and reacquires before a blocked
// engine thread is even scheduled. `benchmarks/bench_budgets.cpp` measured the result three
// times on two fixtures: the persist that arrives during a load waits out the *whole* load
// (2.55 s at 7 days on the ceiling fixture), not the one command that was running. And
// because `AppState::engine_tick` persists before it emits, the prediction and any snapback
// alert that tick produced wait with it.
//
// This is a gate beside the mutex, not a replacement for it. A writer announces itself while
// it waits for the lock; a reader about to take the lock first waits for announced writers to
// get it. The writer still waits for whatever command holds the lock now -- that is the bound
// this buys, one hold instead of one load -- and the mutex, its rank check, and its metrics
// are untouched.
//
// **Only yield while holding no lock the writer needs.** A reader that yields while holding
// the writer's lock waits for a writer that is waiting for it. The call sites in `AppState`
// hold nothing at that point; keep it that way.
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
