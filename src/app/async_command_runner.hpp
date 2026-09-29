#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace snapback::detail {

// One owned worker for native commands that must not block the webview event loop. Shutdown
// drains and joins it, so a long job (training) must poll `stopping()` and cut itself short.
class AsyncCommandRunner {
public:
    AsyncCommandRunner() : worker_([this] { run(); }) {}
    ~AsyncCommandRunner() { shutdown(); }

    AsyncCommandRunner(const AsyncCommandRunner&) = delete;
    AsyncCommandRunner& operator=(const AsyncCommandRunner&) = delete;

    bool submit(std::function<void()> job) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return false;
            jobs_.push_back(std::move(job));
        }
        ready_.notify_one();
        return true;
    }

    void shutdown() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopping_.store(true, std::memory_order_release);
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    // True once shutdown has begun. Safe to poll from inside a job.
    bool stopping() const noexcept { return stopping_.load(std::memory_order_acquire); }

private:
    void run() noexcept {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping() || !jobs_.empty(); });
                if (jobs_.empty()) {
                    if (stopping()) return;
                    continue;
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try {
                job();
            } catch (...) {
                // Command jobs own their error envelope. Never let one exception terminate
                // the process or strand shutdown before the join.
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    // Written under the mutex so the wait predicate cannot miss it; atomic so a running job
    // can read it without the lock.
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

}  // namespace snapback::detail
