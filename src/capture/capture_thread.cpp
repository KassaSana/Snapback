#include "capture/capture_thread.hpp"

namespace snapback {

void CaptureThread::start(InputHook* hook) {
    // Guard first: exchange returns the previous value, so a second start() without an
    // intervening stop() bails out here. Without it, the assignment to hook_thread_ below
    // would overwrite a joinable std::thread, which calls std::terminate.
    if (running_.exchange(true, std::memory_order_acq_rel)) return;

    hook_ = hook ? hook : &InputHook::instance();
    hook_thread_ = std::thread([this] {
        hook_->run([this](const CaptureEvent& ev) noexcept {
            // This runs inside the OS low-level hook callback; a C++ exception unwinding
            // through that boundary is undefined behavior, so swallow everything. A copy
            // that throws (e.g. OOM on the string members) is counted as a drop.
            try {
                // If the engine isn't draining fast enough the buffer fills and we drop,
                // exactly like the Rust bounded channel bumping capture_events_dropped.
                if (!buffer_.push(ev)) {
                    dropped_.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (...) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        });
    });
}

void CaptureThread::stop() {
    // Stop the same hook start() ran, not the singleton — otherwise an injected fake would
    // never be told to return and the join below would hang forever.
    if (hook_) hook_->stop();
    if (hook_thread_.joinable()) hook_thread_.join();
    hook_ = nullptr;
    // Cleared last, after the thread is definitely gone, so a concurrent start() can't
    // slip past the guard and race the assignment to hook_thread_.
    running_.store(false, std::memory_order_release);
}

}  // namespace snapback
