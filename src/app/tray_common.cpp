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
    // Roadmap 2.16. Its own row, next to the recording one and deliberately not merged with
    // it: pausing recording and silencing alerts are different promises, and a single row that
    // did both would be the confusion the item exists to prevent.
    //
    // No countdown in the label. TrayMenuEntry::label is a `const char*` pointing at a string
    // literal, so a live "18 min left" would need owned storage on every row for the benefit of
    // one; the remaining time is shown in the app window, which is where a number that changes
    // belongs anyway.
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
