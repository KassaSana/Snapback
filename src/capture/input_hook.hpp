// Global input capture, one backend per OS. The callback runs on an OS-owned thread. See
// input_hook_windows.cpp / input_hook_macos.mm / input_hook_x11.cpp.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>

#include "types.hpp"

namespace snapback {

// Wall-clock epoch seconds for CaptureEvent::wall_clock_secs, stamped by every backend beside
// the uptime-based timestamp_secs so calendar features use real time. Defined once so backends
// cannot drift.
inline double wall_clock_secs_now() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Called from the OS hook thread for every keyboard/mouse event. Keep it fast and
// allocation-free: on Windows this runs inside the low-level hook and blocks the
// whole input queue while it executes.
using InputCallback = std::function<void(CaptureEvent)>;

class InputHook {
public:
    virtual ~InputHook() = default;

    // Installs OS hooks and blocks running the OS event loop (WH_KEYBOARD_LL needs
    // a message pump on Windows; CGEventTap needs a CFRunLoop on macOS). Run on its
    // own std::thread. The shared stop flag remains authoritative when stop() arrives
    // before the platform event loop has finished starting. stop() wakes any
    // blocking OS loop so it can observe the flag promptly.
    virtual void run(InputCallback on_event,
                     const std::atomic<bool>& stop_requested) = 0;
    virtual void stop() noexcept = 0;

    // The implementation selects the platform backend.
    static InputHook& instance();
};

}  // namespace snapback
