// Platform-neutral overlay helpers (compiled into snapback_core so tests can reach them
// without pulling in Win32). The concrete Overlay window lives in overlay_windows.cpp.
#include "snapback/overlay.hpp"

namespace snapback {

ScreenPoint top_right_position(ScreenPoint monitor_pos, ScreenPoint monitor_size,
                               int window_width, int margin) {
    // Mirror overlay.rs: x hugs the right edge (minus width + margin), y sits margin
    // below the top. Written against monitor origin so multi-monitor layouts land right.
    return ScreenPoint{monitor_pos.x + monitor_size.x - window_width - margin,
                       monitor_pos.y + margin};
}

std::string overlay_text(const SnapbackPayload& payload) {
    std::string out = "Here's where you left off\n\n";
    out += payload.summary.empty() ? ("Return to " + payload.app_name) : payload.summary;
    // Only add the file hint on its own line if the summary doesn't already name it
    // (the tracker's summary is usually "Return to <file_hint>", so avoid repeating it).
    if (!payload.file_hint.empty() &&
        payload.summary.find(payload.file_hint) == std::string::npos) {
        out += "\n" + payload.file_hint;
    }
    out += "\n\nAway " + std::to_string(payload.distraction_duration_secs) + "s";
    if (!payload.app_name.empty()) out += " \xC2\xB7 " + payload.app_name;  // UTF-8 middot
    return out;
}

}  // namespace snapback
