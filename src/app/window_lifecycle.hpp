#pragma once

namespace snapback {

// Installs close-to-tray behavior on a native window handle (HWND on Windows, NSWindow* on macOS).
// When active, closing the window hides it instead of destroying it, allowing background tracking
// and tray interactions to continue.
void enable_close_to_tray(void* native_window);

// Prepares the window for explicit application termination (e.g. from the tray's Quit menu),
// removing subclassing/delegation so the window can close normally.
void prepare_app_exit(void* native_window);

// Checks if close-to-tray is currently active on the given native window handle.
bool is_close_to_tray_enabled(void* native_window);

}  // namespace snapback
