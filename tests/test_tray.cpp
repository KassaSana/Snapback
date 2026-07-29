// Tests the tray's pure menu model and its menu-command -> action mapping. The
// Shell_NotifyIcon / NSStatusItem plumbing is OS glue verified by running the app.
#include "doctest_wrapper.hpp"

#include <string_view>

#include "app/tray.hpp"

using namespace snapback;

TEST_CASE("tray_action_for maps known menu ids to actions") {
    CHECK(tray_action_for(kTrayCmdShow) == TrayAction::Show);
    CHECK(tray_action_for(kTrayCmdQuit) == TrayAction::Quit);
}

TEST_CASE("tray_action_for returns None for unknown ids") {
    CHECK(tray_action_for(kTrayCmdNone) == TrayAction::None);
    CHECK(tray_action_for(9999) == TrayAction::None);
}

TEST_CASE("tray_menu_entries lists Show and Quit, separated") {
    const auto entries = tray_menu_entries();
    REQUIRE(entries.size() == 3);

    CHECK(std::string_view(entries[0].label) == "Show Snapback");
    CHECK(entries[0].command_id == kTrayCmdShow);

    CHECK(tray_menu_entry_is_separator(entries[1]));
    CHECK(entries[1].label == nullptr);

    CHECK(std::string_view(entries[2].label) == "Quit");
    CHECK(entries[2].command_id == kTrayCmdQuit);
}

// The invariant that actually matters once two platforms translate this list: every row
// either draws a separator or resolves to something that happens. A command id with no
// TrayAction produces a menu item the user can click that does nothing, which is
// indistinguishable from the app having hung.
TEST_CASE("every tray menu entry is a separator or maps to a real action") {
    for (const TrayMenuEntry& entry : tray_menu_entries()) {
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
    for (const TrayMenuEntry& entry : tray_menu_entries()) {
        if (!tray_menu_entry_is_separator(entry)) continue;
        CHECK(tray_action_for(entry.command_id) == TrayAction::None);
    }
}
