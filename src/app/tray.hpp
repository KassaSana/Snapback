// System tray icon: Shell_NotifyIcon + hidden window + popup menu on Windows, NSStatusItem +
// NSMenu on macOS. The menu model and command mapping are pure and shared, so the platforms
// cannot drift.
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
// Silencing alerts, deliberately separate from pausing recording.
constexpr unsigned int kTrayCmdSnoozeAlerts = 1005;
constexpr unsigned int kTrayCmdResumeAlerts = 1006;

TrayAction tray_action_for(unsigned int menu_id);

// One menu row. A separator is kTrayCmdNone with a null label, which maps to TrayAction::None,
// so a click on one is inert.
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

// What the tray can ask the app to do. Named fields rather than positional std::functions of
// the same type; every field is optional.
struct TrayCallbacks {
    std::function<void()> on_show;              // bring the main window forward
    std::function<void()> on_quit;              // end the app's run loop
    std::function<RecordingStatus()> recording_status;
    std::function<void()> on_pause_recording;
    std::function<void()> on_resume_recording;
    std::function<void()> on_snooze_alerts;
    std::function<void()> on_resume_alerts;
    // The user clicked the notification body. The arguments are what show_notification
    // remembered: a Win32 balloon click carries no payload.
    std::function<void(AlertEvent, std::int64_t)> on_notification_click;
};

// The tray icon. install() must be called on the UI thread (its hidden window is pumped
// by the main webview run loop). instance() returns the per-platform implementation
// (a no-op where unimplemented, so the build stays green cross-platform).
class Tray {
public:
    virtual ~Tray() = default;

    // Whether an icon reached the notification area. Close-to-tray depends on it: a hidden
    // window with no icon is unreachable.
    virtual bool install(TrayCallbacks callbacks) = 0;

    // Show a native notification via the icon from install(); returns whether the OS accepted
    // it. `event` and `alert_id` are remembered for a later click; alert_id 0 is not
    // actionable.
    virtual bool show_notification(const NotificationPayload& payload, AlertEvent event,
                                   std::int64_t alert_id) = 0;

    // The unactionable form (e.g. the close-to-tray explanation).
    bool show_notification(const NotificationPayload& payload) {
        return show_notification(payload, AlertEvent::Snapback, /*alert_id=*/0);
    }

    static Tray& instance();
};

}  // namespace snapback
