#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace snapback::detail {

// One owned worker for native commands that must not monopolize the webview event loop.
// Jobs are copied into the queue, and shutdown drains and joins the worker so their borrowed
// AppState/webview references cannot outlive either owner.
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
            stopping_ = true;
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

private:
    void run() noexcept {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) {
                    if (stopping_) return;
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
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace snapback::detail
