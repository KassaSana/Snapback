// Tests the tray's pure menu model and its menu-command -> action mapping. The
// Shell_NotifyIcon / NSStatusItem plumbing is OS glue verified by running the app.
#include "doctest_wrapper.hpp"

#include <string_view>

#include "app/tray.hpp"

using namespace snapback;

TEST_CASE("tray_action_for maps known menu ids to actions") {
    CHECK(tray_action_for(kTrayCmdShow) == TrayAction::Show);
    CHECK(tray_action_for(kTrayCmdPauseRecording) == TrayAction::PauseRecording);
    CHECK(tray_action_for(kTrayCmdResumeRecording) == TrayAction::ResumeRecording);
    CHECK(tray_action_for(kTrayCmdQuit) == TrayAction::Quit);
}

TEST_CASE("tray_action_for returns None for unknown ids") {
    CHECK(tray_action_for(kTrayCmdNone) == TrayAction::None);
    CHECK(tray_action_for(9999) == TrayAction::None);
}

TEST_CASE("tray_menu_entries shows recording state and the matching privacy action") {
    RecordingStatus status{RecordingState::Recording, 0};
    const auto entries = tray_menu_entries(status);
    REQUIRE(entries.size() == 7);  // 6 -> 7 with Roadmap 2.16's alert row

    CHECK(std::string_view(entries[0].label) == "Status: Recording");
    CHECK(entries[0].command_id == kTrayCmdShow);
    CHECK(std::string_view(entries[1].label) == "Pause recording");
    CHECK(entries[1].command_id == kTrayCmdPauseRecording);
    CHECK(std::string_view(entries[6].label) == "Quit");

    status.state = RecordingState::PausedPrivate;
    const auto paused = tray_menu_entries(status);
    CHECK(std::string_view(paused[0].label) == "Status: Paused privately");
    CHECK(std::string_view(paused[1].label) == "Resume recording");
}

// The invariant that actually matters once two platforms translate this list: every row
// either draws a separator or resolves to something that happens. A command id with no
// TrayAction produces a menu item the user can click that does nothing, which is
// indistinguishable from the app having hung.
TEST_CASE("every tray menu entry is a separator or maps to a real action") {
    for (const TrayMenuEntry& entry : tray_menu_entries(RecordingStatus{})) {
        if (tray_menu_entry_is_separator(entry)) {
            CHECK(entry.label == nullptr);
            continue;
        }
        REQUIRE(entry.label != nullptr);
        CHECK(std::string_view(entry.label).empty() == false);
        CHECK(tray_action_for(entry.command_id) != TrayAction::None);
    }
}

// A separator carries kTrayCmdNone, so the "unknown id" rule is what makes a stray click
// on one inert. Pin that the two constants agree — this is the assumption the single-loop
// menu translation in tray_windows.cpp and tray_macos.mm is built on.
TEST_CASE("a separator's command id is inert") {
    for (const TrayMenuEntry& entry : tray_menu_entries(RecordingStatus{})) {
        if (!tray_menu_entry_is_separator(entry)) continue;
        CHECK(tray_action_for(entry.command_id) == TrayAction::None);
    }
}

// Roadmap 2.16. The alert row, which is deliberately not the recording row.

TEST_CASE("tray_action_for maps the alert snooze ids") {
    CHECK(tray_action_for(kTrayCmdSnoozeAlerts) == TrayAction::SnoozeAlerts);
    CHECK(tray_action_for(kTrayCmdResumeAlerts) == TrayAction::ResumeAlerts);
}

TEST_CASE("the tray offers to snooze alerts when they are live") {
    RecordingStatus status{RecordingState::Recording, 0};
    const auto entries = tray_menu_entries(status);
    CHECK(std::string_view(entries[2].label) == "Snooze alerts for 30 minutes");
    CHECK(entries[2].command_id == kTrayCmdSnoozeAlerts);
}

TEST_CASE("the tray offers to resume alerts while snoozed") {
    RecordingStatus status{RecordingState::Recording, 0};
    status.alert_snooze_remaining_ms = 18 * 60 * 1000;
    const auto entries = tray_menu_entries(status);
    CHECK(std::string_view(entries[2].label) == "Resume alerts");
    CHECK(entries[2].command_id == kTrayCmdResumeAlerts);
}

TEST_CASE("a snooze does not change the tray status line or the recording row") {
    // The surface the user actually looks at has to agree with RecordingStatus: silencing an
    // intervention is not privacy mode, so the status line still says Recording and the
    // pause-recording row is untouched. If this ever flips, the tray is telling the user they
    // stopped being recorded when they only asked for quiet.
    RecordingStatus status{RecordingState::Recording, 0};
    status.alert_snooze_remaining_ms = 18 * 60 * 1000;
    const auto entries = tray_menu_entries(status);

    CHECK(std::string_view(entries[0].label) == "Status: Recording");
    CHECK(std::string_view(entries[1].label) == "Pause recording");
    CHECK(entries[1].command_id == kTrayCmdPauseRecording);
}

TEST_CASE("the alert row carries no countdown") {
    // TrayMenuEntry::label is a const char* into a string literal, so a live "18 min left"
    // would need owned storage on every row for the benefit of one. The remaining time is
    // shown in the app window instead, which is where a number that changes belongs.
    RecordingStatus status{RecordingState::Recording, 0};
    status.alert_snooze_remaining_ms = 18 * 60 * 1000;
    const std::string_view label = tray_menu_entries(status)[2].label;
    CHECK(label.find("18") == std::string_view::npos);
    CHECK(label.find("min left") == std::string_view::npos);
}

// Roadmap 9.15 note. `Tray::install` now returns whether an icon really reached the
// notification area, and main.cpp turns close-to-tray on only when it says yes. There is no
// case for it here on purpose: `Tray::instance()` is defined only in the `snapback` app target
// (see CMakeLists.txt), so asserting on it would mean linking an OS singleton into every test
// process to check one bool. That plumbing stays in the category this file's header already
// names -- OS glue verified by running the app.
