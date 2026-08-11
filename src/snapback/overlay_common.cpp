// Platform-neutral overlay helpers (compiled into snapback_core so tests can reach them
// without pulling in Win32 or AppKit). The only concrete Overlay window today is
// overlay_windows.cpp; the macOS panel that consumes cocoa_origin_y() is Roadmap 3.1.
//
// cocoa_origin_y() is deliberately *not* guarded on __APPLE__: keeping it unconditional
// means the Windows and Linux CI hosts run its tests too, so a mistake in the one piece of
// macOS placement logic that can be tested at all cannot hide until someone opens a Mac.
#include "snapback/overlay.hpp"

#include <algorithm>

namespace snapback {

OverlayMonitorSource choose_overlay_monitor(bool foreground_monitor_valid,
                                            bool cursor_monitor_valid) {
    if (foreground_monitor_valid) return OverlayMonitorSource::kForegroundWindow;
    if (cursor_monitor_valid) return OverlayMonitorSource::kCursor;
    return OverlayMonitorSource::kPrimary;
}

int scale_for_dpi(int design_units, int dpi) {
    if (dpi <= 0) dpi = kOverlayBaseDpi;
    if (dpi == kOverlayBaseDpi) return design_units;
    // +half denominator before the integer divide, so 150% of 20 is 30 and not 29. Negative
    // inputs are not meaningful here, but rounding them toward zero keeps the function total.
    const long long scaled = static_cast<long long>(design_units) * dpi;
    const long long half = kOverlayBaseDpi / 2;
    return static_cast<int>((scaled + (scaled < 0 ? -half : half)) / kOverlayBaseDpi);
}

OverlayRect overlay_rect(ScreenPoint work_pos, ScreenPoint work_size, int dpi) {
    const int margin = scale_for_dpi(kScreenMargin, dpi);

    // Shrink to fit before placing. A card wider than the screen cannot be positioned into
    // view, so clamping the *size* first is what keeps the placement below meaningful.
    // `std::max(1, ...)` keeps a degenerate work area from producing a zero or negative extent,
    // which SetWindowPos would take literally.
    const int max_width = std::max(1, work_size.x - 2 * margin);
    const int max_height = std::max(1, work_size.y - 2 * margin);
    const int width = std::min(scale_for_dpi(kOverlayWidth, dpi), max_width);
    const int height = std::min(scale_for_dpi(kOverlayHeight, dpi), max_height);

    // Written against the monitor's own origin, which is why a display mounted above or to the
    // left of the primary one — and therefore at negative coordinates — lands correctly. The
    // clamps catch the remaining pathological case where the margin alone exceeds the monitor.
    const int x = std::max(work_pos.x, work_pos.x + work_size.x - width - margin);
    const int y = std::min(work_pos.y + margin, work_pos.y + std::max(0, work_size.y - height));

    return OverlayRect{x, y, width, height};
}

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
