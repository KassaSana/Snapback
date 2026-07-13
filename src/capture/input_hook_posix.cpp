#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__linux__)

#include "capture/input_hook.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include "capture/active_window.hpp"

namespace snapback {
namespace {

double now_secs() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

class PollingInputHook final : public InputHook {
public:
    void run(InputCallback on_event) override {
        running_.store(true, std::memory_order_relaxed);
        std::string last_app;
        std::string last_title;

        while (running_.load(std::memory_order_relaxed)) {
            if (auto active = query_active_window()) {
                if (active->app_name != last_app || active->window_title != last_title) {
                    CaptureEvent ev;
                    ev.event_type = active->app_name != last_app ? EventType::WindowFocusChange
                                                                 : EventType::WindowTitleChange;
                    ev.timestamp_secs = now_secs();
                    ev.app_name = active->app_name;
                    ev.window_title = active->window_title;
                    on_event(ev);
                    last_app = active->app_name;
                    last_title = active->window_title;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void stop() override {
        running_.store(false, std::memory_order_relaxed);
    }

private:
    std::atomic<bool> running_{false};
};

}  // namespace

InputHook& InputHook::instance() {
    static PollingInputHook hook;
    return hook;
}

}  // namespace snapback

#endif
