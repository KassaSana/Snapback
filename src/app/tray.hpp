// System tray icon. Rust: tray.rs (Tauri's tray API). Tauri gave us the tray for free;
// on Windows we build it from Shell_NotifyIcon + a hidden message window + a popup menu.
//
// The menu-command -> action mapping is a pure free function so it can be unit-tested;
// the icon/window/menu plumbing is OS glue verified by running the app.
#pragma once

#include <functional>

#include "app/notification.hpp"

namespace snapback {

// What a clicked tray menu item means.
enum class TrayAction { None, Show, Quit };

// Popup-menu command IDs (also the WM_COMMAND ids the Win32 menu posts).
constexpr unsigned int kTrayCmdShow = 1001;
constexpr unsigned int kTrayCmdQuit = 1002;

TrayAction tray_action_for(unsigned int menu_id);

// The tray icon. install() must be called on the UI thread (its hidden window is pumped
// by the main webview run loop). instance() returns the per-platform implementation
// (a no-op where unimplemented, so the build stays green cross-platform).
class Tray {
public:
    virtual ~Tray() = default;

    // on_show: bring the main window forward. on_quit: end the app's run loop.
    virtual void install(std::function<void()> on_show, std::function<void()> on_quit) = 0;

    // Show a native notification using the icon registered by install(). The return value
    // reports whether the OS accepted the request; callers can safely ignore it when a
    // notification is only a best-effort nudge.
    virtual bool show_notification(const NotificationPayload& payload) = 0;

    static Tray& instance();
};

}  // namespace snapback
