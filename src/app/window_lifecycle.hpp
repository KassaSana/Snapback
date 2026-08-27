#pragma once

#include <functional>

namespace snapback {

// Installs close-to-tray behavior on a native window handle (HWND on Windows, NSWindow* on macOS).
// When active, closing the window hides it instead of destroying it, allowing background tracking
// and tray interactions to continue.
void enable_close_to_tray(void* native_window);

// The same, plus a hook fired on the UI thread each time a close hides the window.
//
// Roadmap 9.15 wants a one-time explanation the first time closing leaves Snapback in the
// tray, and the only place that knows a close just happened is the platform handler. `on_hidden`
// is therefore told about *every* hide; deciding that only the first one is worth saying
// anything about belongs to the caller, which is the half that can persist the answer.
//
// The hook is stored per translation unit rather than per window: this app has one main window
// for the life of the process, and threading a context pointer through a WNDPROC subclass and
// an NSWindowDelegate to support a second one would be plumbing for a case that does not exist.
void enable_close_to_tray(void* native_window, std::function<void()> on_hidden);

// Prepares the window for explicit application termination (e.g. from the tray's Quit menu),
// removing subclassing/delegation so the window can close normally.
void prepare_app_exit(void* native_window);

// Checks if close-to-tray is currently active on the given native window handle.
bool is_close_to_tray_enabled(void* native_window);

}  // namespace snapback
