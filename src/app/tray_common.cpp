// Platform-neutral tray helper (compiled into snapback_core so tests can reach it).
#include "app/tray.hpp"

namespace snapback {

TrayAction tray_action_for(unsigned int menu_id) {
    switch (menu_id) {
        case kTrayCmdShow: return TrayAction::Show;
        case kTrayCmdQuit: return TrayAction::Quit;
        default: return TrayAction::None;
    }
}

}  // namespace snapback
