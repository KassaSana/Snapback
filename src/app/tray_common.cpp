// Platform-neutral tray helpers (compiled into snapback_core so tests can reach them).
#include "app/tray.hpp"

namespace snapback {
namespace {

// The single definition of what the tray menu offers. Windows (AppendMenuW) and macOS
// (NSMenuItem) both translate this; neither owns a list of its own, so a menu item added
// for one platform cannot go missing on the other.
constexpr TrayMenuEntry kMenuEntries[] = {
    {"Show Snapback", kTrayCmdShow},
    {nullptr, kTrayCmdNone},  // separator
    {"Quit", kTrayCmdQuit},
};

}  // namespace

std::span<const TrayMenuEntry> tray_menu_entries() { return kMenuEntries; }

TrayAction tray_action_for(unsigned int menu_id) {
    switch (menu_id) {
        case kTrayCmdShow: return TrayAction::Show;
        case kTrayCmdQuit: return TrayAction::Quit;
        default: return TrayAction::None;
    }
}

}  // namespace snapback
