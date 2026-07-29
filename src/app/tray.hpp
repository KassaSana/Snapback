// System tray icon. On Windows we build it from Shell_NotifyIcon + a hidden message
// window + a popup menu; on macOS from an NSStatusItem + an NSMenu.
//
// The menu *model* and the menu-command -> action mapping are pure and shared, so they can
// be unit-tested and so two platforms cannot drift into offering different menus; the
// icon/window/menu plumbing is OS glue verified by running the app.
#pragma once

#include <functional>
#include <span>

#include "app/notification.hpp"

namespace snapback {

// What a clicked tray menu item means.
enum class TrayAction { None, Show, Quit };

// Popup-menu command IDs (also the WM_COMMAND ids the Win32 menu posts and the NSMenuItem
// tags the Cocoa menu carries). Zero is reserved for "not a command" — see kTrayCmdNone.
constexpr unsigned int kTrayCmdNone = 0;
constexpr unsigned int kTrayCmdShow = 1001;
constexpr unsigned int kTrayCmdQuit = 1002;

TrayAction tray_action_for(unsigned int menu_id);

// One row of the tray's popup menu.
//
// A separator is spelled as kTrayCmdNone with a null label rather than as its own type,
// which is what lets a platform translate the whole menu in one loop. It is safe by
// construction: kTrayCmdNone maps to TrayAction::None, so even if a platform somehow
// delivered a click on a separator it would be inert under the same rule that already
// guards unknown ids.
struct TrayMenuEntry {
    const char* label;        // UTF-8; nullptr for a separator
    unsigned int command_id;  // kTrayCmdNone for a separator
};

// The tray menu in display order. Backed by static storage, so the span outlives any
// caller.
std::span<const TrayMenuEntry> tray_menu_entries();

inline bool tray_menu_entry_is_separator(const TrayMenuEntry& entry) {
    return entry.command_id == kTrayCmdNone;
}

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
