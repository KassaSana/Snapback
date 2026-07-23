#include "capture/capture_thread.hpp"

#include <algorithm>
#include <exception>

namespace snapback {

std::int64_t CaptureThread::steady_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void CaptureThread::record_failure(const char* reason) noexcept {
    failed_.store(true, std::memory_order_release);
    try {
        std::lock_guard lock(failure_mutex_);
        failure_reason_ = reason;
    } catch (...) {
        // The boolean still reports failure if allocating the diagnostic text fails.
    }
}

void CaptureThread::start(InputHook* hook) {
    // Guard first: exchange returns the previous value, so a second start() without an
    // intervening stop() bails out here. Without it, the assignment to hook_thread_ below
    // would overwrite a joinable std::thread, which calls std::terminate.
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    // A hook can return on its own. Its thread is finished but remains joinable until
    // somebody joins it; reclaim that thread before assigning a new one on restart.
    if (hook_thread_.joinable()) hook_thread_.join();

    stop_requested_.store(false, std::memory_order_release);
    failed_.store(false, std::memory_order_release);
    has_event_.store(false, std::memory_order_release);
    last_event_ms_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(failure_mutex_);
        failure_reason_.clear();
    }
    hook_ = hook ? hook : &InputHook::instance();
    hook_thread_ = std::thread([this] {
        try {
            hook_->run([this](const CaptureEvent& ev) noexcept {
                // This runs inside the OS low-level hook callback; a C++ exception unwinding
                // through that boundary is undefined behavior, so swallow everything. A copy
                // that throws (e.g. OOM on the string members) is counted as a drop.
                try {
                    // If the engine isn't draining fast enough the buffer fills and we drop,
                    // exactly like the Rust bounded channel bumping capture_events_dropped.
                    if (!buffer_.push(ev)) {
                        dropped_.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        last_event_ms_.store(steady_now_ms(), std::memory_order_relaxed);
                        has_event_.store(true, std::memory_order_release);
                    }
                } catch (...) {
                    dropped_.fetch_add(1, std::memory_order_relaxed);
                }
            });
            if (!stop_requested_.load(std::memory_order_acquire)) {
                record_failure("input hook stopped unexpectedly");
            }
        } catch (const std::exception&) {
            record_failure("input hook threw an exception");
        } catch (...) {
            record_failure("input hook threw an unknown exception");
        }
        running_.store(false, std::memory_order_release);
    });
}

void CaptureThread::stop() {
    // Stop the same hook start() ran, not the singleton — otherwise an injected fake would
    // never be told to return and the join below would hang forever.
    stop_requested_.store(true, std::memory_order_release);
    if (hook_) hook_->stop();
    if (hook_thread_.joinable()) hook_thread_.join();
    hook_ = nullptr;
    // Cleared last, after the thread is definitely gone, so a concurrent start() can't
    // slip past the guard and race the assignment to hook_thread_.
    running_.store(false, std::memory_order_release);
}

std::optional<std::string> CaptureThread::failure_reason() const {
    if (!failed()) return std::nullopt;
    std::lock_guard lock(failure_mutex_);
    return failure_reason_;
}

std::optional<std::int64_t> CaptureThread::last_event_age_ms() const {
    if (!has_event_.load(std::memory_order_acquire)) return std::nullopt;
    return std::max<std::int64_t>(0, steady_now_ms() -
                                         last_event_ms_.load(std::memory_order_relaxed));
}

}  // namespace snapback
