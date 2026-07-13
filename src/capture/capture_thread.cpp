#include "capture/capture_thread.hpp"

namespace snapback {

void CaptureThread::start() {
    running_.store(true, std::memory_order_relaxed);
    hook_thread_ = std::thread([this] {
        InputHook::instance().run([this](const CaptureEvent& ev) noexcept {
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
    InputHook::instance().stop();
    if (hook_thread_.joinable()) hook_thread_.join();
    running_.store(false, std::memory_order_relaxed);
}

}  // namespace snapback
