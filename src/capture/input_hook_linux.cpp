#if defined(__linux__)

#include "capture/input_hook.hpp"
#include "capture/input_context.hpp"

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
    void run(InputCallback on_event,
             const std::atomic<bool>& stop_requested) override {
        callback_ = std::move(on_event);
        if (stop_requested.load(std::memory_order_acquire)) return;
        fds_ = open_input_devices();
        if (fds_.empty()) {
            run_polling_fallback(stop_requested);
            return;
        }

        detail::CaptureContextProvider context([] { return query_active_window(); });
        context.refresh(now_secs(), wall_clock_secs_now());
        while (!stop_requested.load(std::memory_order_acquire)) {
            if (auto change = context.refresh(now_secs(), wall_clock_secs_now()))
                callback_(std::move(*change));
            bool got_event = false;
            for (int fd : fds_) {
                input_event ev{};
                // Bound each device slice so a busy device cannot starve context refresh or stop.
                for (unsigned drained = 0; drained < 256 &&
                    !stop_requested.load(std::memory_order_acquire) &&
                    read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev)); ++drained) {
                    const auto kind = detail::classify_linux_input(ev.type, ev.code, ev.value);
                    if (!kind) continue;
                    CaptureEvent out;
                    out.event_type = *kind;
                    out.timestamp_secs = now_secs();
                    out.wall_clock_secs = wall_clock_secs_now();
                    out.captured_context = context.value();
                    callback_(std::move(out));
                    got_event = true;
                }
            }
            if (!got_event) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        for (int fd : fds_) close(fd);
        fds_.clear();
    }

    void stop() noexcept override {}

private:
    void run_polling_fallback(const std::atomic<bool>& stop_requested) {
        detail::CaptureContextProvider context([] { return query_active_window(); });
        context.refresh(now_secs(), wall_clock_secs_now());
        while (!stop_requested.load(std::memory_order_acquire)) {
            if (auto change = context.refresh(now_secs(), wall_clock_secs_now()))
                callback_(std::move(*change));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    InputCallback callback_;
    std::vector<int> fds_;
};

}  // namespace

InputHook& InputHook::instance() {
    static LinuxInputHook hook;
    return hook;
}

}  // namespace snapback

#endif  // __linux__
