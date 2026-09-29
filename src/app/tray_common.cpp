// Platform-neutral tray helpers (compiled into snapback_core so tests can reach them).
#include "app/tray.hpp"

namespace snapback {
namespace {

// The single definition of what the tray menu offers. Windows (AppendMenuW) and macOS
// (NSMenuItem) both translate this; neither owns a list of its own, so a menu item added
// for one platform cannot go missing on the other.
}  // namespace

std::vector<TrayMenuEntry> tray_menu_entries(const RecordingStatus& status) {
    const char* label = "Status: Blocked";
    switch (status.state) {
        case RecordingState::Recording: label = "Status: Recording"; break;
        case RecordingState::PausedIdle: label = "Status: Paused for idle"; break;
        case RecordingState::PausedPrivate: label = "Status: Paused privately"; break;
        case RecordingState::NoSession: label = "Status: No session"; break;
        case RecordingState::Blocked: break;
    }
    std::vector<TrayMenuEntry> entries{{label, kTrayCmdShow}};
    entries.push_back(status.state == RecordingState::PausedPrivate
                          ? TrayMenuEntry{"Resume recording", kTrayCmdResumeRecording}
                          : TrayMenuEntry{"Pause recording", kTrayCmdPauseRecording});
    // Its own row: pausing recording and silencing alerts are different promises. No countdown
    // in the label (labels are static strings); the app window shows the time left.
    entries.push_back(status.alert_snooze_remaining_ms > 0
                          ? TrayMenuEntry{"Resume alerts", kTrayCmdResumeAlerts}
                          : TrayMenuEntry{"Snooze alerts for 30 minutes", kTrayCmdSnoozeAlerts});
    entries.push_back({nullptr, kTrayCmdNone});
    entries.push_back({"Show Snapback", kTrayCmdShow});
    entries.push_back({nullptr, kTrayCmdNone});
    entries.push_back({"Quit", kTrayCmdQuit});
    return entries;
}

TrayAction tray_action_for(unsigned int menu_id) {
    switch (menu_id) {
        case kTrayCmdShow: return TrayAction::Show;
        case kTrayCmdPauseRecording: return TrayAction::PauseRecording;
        case kTrayCmdResumeRecording: return TrayAction::ResumeRecording;
        case kTrayCmdSnoozeAlerts: return TrayAction::SnoozeAlerts;
        case kTrayCmdResumeAlerts: return TrayAction::ResumeAlerts;
        case kTrayCmdQuit: return TrayAction::Quit;
        default: return TrayAction::None;
    }
}

}  // namespace snapback
