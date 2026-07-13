// Tests the tray's pure menu-command -> action mapping. The Shell_NotifyIcon plumbing is
// OS glue verified by running the app.
#include <doctest/doctest.h>

#include "app/tray.hpp"

using namespace snapback;

TEST_CASE("tray_action_for maps known menu ids to actions") {
    CHECK(tray_action_for(kTrayCmdShow) == TrayAction::Show);
    CHECK(tray_action_for(kTrayCmdQuit) == TrayAction::Quit);
}

TEST_CASE("tray_action_for returns None for unknown ids") {
    CHECK(tray_action_for(0) == TrayAction::None);
    CHECK(tray_action_for(9999) == TrayAction::None);
}
