// Global input capture behind one backend per OS. This is the single biggest chunk
// of platform-specific work and the
// most footgun-prone (a global hook callback runs on an OS-owned thread).
//
// See input_hook_windows.cpp / input_hook_macos.cpp / input_hook_x11.cpp.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>

#include "types.hpp"

namespace snapback {

// Wall-clock seconds since the Unix epoch, for CaptureEvent::wall_clock_secs.
//
// Roadmap 7.24. Every backend stamps `timestamp_secs` from an uptime clock, which is right
// for durations and ordering and catastrophic for calendar features: read as epoch time, an
// uptime of ten hours says "10:00 on 1 Jan 1970". Each backend stamps this alongside it so
// hour_of_day and day_of_week describe when the user actually worked.
//
// Defined once here rather than per backend so the three cannot drift — the uptime helpers
// they each define locally are already three subtly different things (GetTickCount64 is
// since boot; the steady_clock ones are since first call).
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
