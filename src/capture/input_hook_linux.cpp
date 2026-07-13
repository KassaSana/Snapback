#if defined(__linux__)

#include "capture/input_hook.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

#include "capture/active_window.hpp"

namespace snapback {
namespace {

double now_secs() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

std::vector<int> open_input_devices() {
    std::vector<int> fds;
    const std::filesystem::path input_dir = "/dev/input";
    if (!std::filesystem::exists(input_dir)) return fds;
    for (const auto& entry : std::filesystem::directory_iterator(input_dir)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0) continue;
        const int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) fds.push_back(fd);
    }
    return fds;
}

class LinuxInputHook final : public InputHook {
public:
    void run(InputCallback on_event) override {
        callback_ = std::move(on_event);
        running_.store(true, std::memory_order_relaxed);
        fds_ = open_input_devices();
        if (fds_.empty()) {
            run_polling_fallback();
            return;
        }

        std::string last_app;
        std::string last_title;
        while (running_.load(std::memory_order_relaxed)) {
            bool got_event = false;
            for (int fd : fds_) {
                input_event ev{};
                while (read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
                    if (ev.type != EV_KEY && ev.type != EV_REL && ev.type != EV_ABS) continue;
                    CaptureEvent out;
                    out.timestamp_secs = now_secs();
                    if (ev.type == EV_KEY && ev.value == 1) {
                        out.event_type = EventType::KeyPress;
                    } else if (ev.type == EV_REL || ev.type == EV_ABS) {
                        out.event_type = EventType::MouseMove;
                    } else {
                        continue;
                    }
                    if (auto active = query_active_window()) {
                        out.app_name = active->app_name;
                        out.window_title = active->window_title;
                    }
                    callback_(out);
                    got_event = true;
                }
            }

            if (auto active = query_active_window()) {
                if (active->app_name != last_app || active->window_title != last_title) {
                    CaptureEvent ev;
                    ev.event_type = active->app_name != last_app ? EventType::WindowFocusChange
                                                                 : EventType::WindowTitleChange;
                    ev.timestamp_secs = now_secs();
                    ev.app_name = active->app_name;
                    ev.window_title = active->window_title;
                    callback_(ev);
                    last_app = active->app_name;
                    last_title = active->window_title;
                    got_event = true;
                }
            }

            if (!got_event) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        for (int fd : fds_) close(fd);
        fds_.clear();
    }

    void stop() override {
        running_.store(false, std::memory_order_relaxed);
    }

private:
    void run_polling_fallback() {
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
                    callback_(ev);
                    last_app = active->app_name;
                    last_title = active->window_title;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    InputCallback callback_;
    std::atomic<bool> running_{false};
    std::vector<int> fds_;
};

}  // namespace

InputHook& InputHook::instance() {
    static LinuxInputHook hook;
    return hook;
}

}  // namespace snapback

#endif  // __linux__
