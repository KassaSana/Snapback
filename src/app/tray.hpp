// System tray icon. On Windows we build it from Shell_NotifyIcon + a hidden message
// window + a popup menu; on macOS from an NSStatusItem + an NSMenu.
//
// The menu *model* and the menu-command -> action mapping are pure and shared, so they can
// be unit-tested and so two platforms cannot drift into offering different menus; the
// icon/window/menu plumbing is OS glue verified by running the app.
#pragma once

#include <functional>
#include <span>
#include <vector>

#include "app/alert_routing.hpp"
#include "app/notification.hpp"
#include "types.hpp"

namespace snapback {

// What a clicked tray menu item means.
enum class TrayAction { None, Show, PauseRecording, ResumeRecording, SnoozeAlerts,
                        ResumeAlerts, Quit };

// Popup-menu command IDs (also the WM_COMMAND ids the Win32 menu posts and the NSMenuItem
// tags the Cocoa menu carries). Zero is reserved for "not a command" — see kTrayCmdNone.
constexpr unsigned int kTrayCmdNone = 0;
constexpr unsigned int kTrayCmdShow = 1001;
constexpr unsigned int kTrayCmdQuit = 1002;
constexpr unsigned int kTrayCmdPauseRecording = 1003;
constexpr unsigned int kTrayCmdResumeRecording = 1004;
// Roadmap 2.16. Silencing alerts, which is not the same as pausing recording -- the menu
// keeps them as separate rows for exactly that reason.
constexpr unsigned int kTrayCmdSnoozeAlerts = 1005;
constexpr unsigned int kTrayCmdResumeAlerts = 1006;

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
std::vector<TrayMenuEntry> tray_menu_entries(const RecordingStatus& status);

inline bool tray_menu_entry_is_separator(const TrayMenuEntry& entry) {
    return entry.command_id == kTrayCmdNone;
}

// What the tray can ask the app to do.
//
// A struct rather than a positional parameter list. The list had already reached five
// std::functions across four platform implementations, three of which take the same
// `void()` type -- so a transposition would compile everywhere and only show up as the wrong
// menu item doing the wrong thing on one OS. Named fields make that mistake unspellable, and
// adding an action stops being a five-file signature change.
//
// Every field is optional: a platform fires what is set and ignores what is not, which is
// what lets the stub store two of them and drop the rest.
struct TrayCallbacks {
    std::function<void()> on_show;              // bring the main window forward
    std::function<void()> on_quit;              // end the app's run loop
    std::function<RecordingStatus()> recording_status;
    std::function<void()> on_pause_recording;
    std::function<void()> on_resume_recording;
    // Roadmap 2.16. Silence delivery for kDefaultAlertSnoozeMins, and undo it.
    std::function<void()> on_snooze_alerts;
    std::function<void()> on_resume_alerts;
    // Roadmap 2.16's action-routing half. The user clicked the notification body.
    //
    // The arguments are what the *tray* remembered from show_notification, not anything the
    // click carried: a Win32 balloon click arrives as a bare NIN_BALLOONUSERCLICK with no
    // payload, so the only way to know which alert was clicked is to have written it down when
    // it was raised.
    std::function<void(AlertEvent, std::int64_t)> on_notification_click;
};

// The tray icon. install() must be called on the UI thread (its hidden window is pumped
// by the main webview run loop). instance() returns the per-platform implementation
// (a no-op where unimplemented, so the build stays green cross-platform).
class Tray {
public:
    virtual ~Tray() = default;

    // Reports whether an icon actually reached the notification area. Roadmap 9.15: the
    // caller has to know, because close-to-tray hides the only window the user has and a
    // hidden window with no tray icon is an app they cannot get back. A void return made that
    // a promise nobody could check, which is how it shipped unchecked.
    virtual bool install(TrayCallbacks callbacks) = 0;

    // Show a native notification using the icon registered by install(). The return value
    // reports whether the OS accepted the request; callers can safely ignore it when a
    // notification is only a best-effort nudge.
    //
    // Roadmap 2.16. `event` and `alert_id` are remembered so a later click can say which alert
    // it was: see TrayCallbacks::on_notification_click. An `alert_id` of 0 means this
    // notification is not actionable, and a click on it will only raise the window.
    virtual bool show_notification(const NotificationPayload& payload, AlertEvent event,
                                   std::int64_t alert_id) = 0;

    // The unactionable form, for notifications that are not routed alerts -- 9.15's
    // close-to-tray explanation is the one that exists today.
    bool show_notification(const NotificationPayload& payload) {
        return show_notification(payload, AlertEvent::Snapback, /*alert_id=*/0);
    }

    static Tray& instance();
};

}  // namespace snapback
