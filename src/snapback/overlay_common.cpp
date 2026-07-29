// Platform-neutral overlay helpers (compiled into snapback_core so tests can reach them
// without pulling in Win32 or AppKit). The only concrete Overlay window today is
// overlay_windows.cpp; the macOS panel that consumes cocoa_origin_y() is Roadmap 3.1.
//
// cocoa_origin_y() is deliberately *not* guarded on __APPLE__: keeping it unconditional
// means the Windows and Linux CI hosts run its tests too, so a mistake in the one piece of
// macOS placement logic that can be tested at all cannot hide until someone opens a Mac.
#include "snapback/overlay.hpp"

namespace snapback {

ScreenPoint top_right_position(ScreenPoint monitor_pos, ScreenPoint monitor_size,
                               int window_width, int margin) {
    // x hugs the right edge (minus width + margin), y sits at the margin
    // below the top. Written against monitor origin so multi-monitor layouts land right.
    return ScreenPoint{monitor_pos.x + monitor_size.x - window_width - margin,
                       monitor_pos.y + margin};
}

int cocoa_origin_y(int work_area_top, int top_down_y, int window_height) {
    // Walk down from the work area's top edge by the requested gap, then down again by the
    // window's own height, because Cocoa names a window by its bottom edge rather than its
    // top one. Subtracting only top_down_y is the classic version of this bug: it hangs the
    // card one window-height above where it belongs.
    return work_area_top - top_down_y - window_height;
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
