// Global input capture. Rust gets this from the `rdev` crate; here you hand-write
// one backend per OS. This is the single biggest chunk of work in the port and the
// most footgun-prone (a global hook callback runs on an OS-owned thread).
//
// See input_hook_windows.cpp / input_hook_macos.cpp / input_hook_x11.cpp.
#pragma once

#include <functional>

#include "types.hpp"

namespace snapback {

// Called from the OS hook thread for every keyboard/mouse event. Keep it fast and
// allocation-free: on Windows this runs inside the low-level hook and blocks the
// whole input queue while it executes.
using InputCallback = std::function<void(const CaptureEvent&)>;

class InputHook {
public:
    virtual ~InputHook() = default;

    // Installs OS hooks and blocks running the OS event loop (WH_KEYBOARD_LL needs
    // a message pump on Windows; CGEventTap needs a CFRunLoop on macOS). Run on its
    // own std::thread. Returns when stop() is called.
    virtual void run(InputCallback on_event) = 0;
    virtual void stop() = 0;

    // Rust: capture/mod.rs::create() picks the platform backend.
    static InputHook& instance();
};

}  // namespace snapback
