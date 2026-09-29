// The snapback overlay window: native, borderless, always on top (a second WebView2 loop on the
// UI thread is not practical). Placement math and text formatting are pure functions.
#pragma once

#include <functional>
#include <string>

#include "types.hpp"

namespace snapback {

// Overlay geometry, in design units at 96 DPI (not pixels).
constexpr int kOverlayWidth = 420;
constexpr int kOverlayHeight = 250;
constexpr int kScreenMargin = 20;

// The "Take me back" region, bottom-left inside the card's padding. The rest dismisses.
constexpr int kOverlayActionWidth = 140;
constexpr int kOverlayActionHeight = 34;
constexpr int kOverlayActionInset = 20;

// The DPI the constants above are written against. Windows' own baseline (USER_DEFAULT_SCREEN_DPI).
constexpr int kOverlayBaseDpi = 96;

struct ScreenPoint {
    int x{};
    int y{};
};

// Which display the card goes on: the foreground window's (where the user was looking), else
// the cursor's, else the primary. A monitor unplugged in between also degrades to primary.
enum class OverlayMonitorSource { kForegroundWindow, kCursor, kPrimary };

OverlayMonitorSource choose_overlay_monitor(bool foreground_monitor_valid,
                                            bool cursor_monitor_valid);

// Scale design units to physical pixels for a DPI. Rounds to nearest; a nonsensical DPI (0 or
// negative, i.e. a failed Win32 query) falls back to the base.
int scale_for_dpi(int design_units, int dpi);

struct OverlayRect {
    int x{};
    int y{};
    int width{};
    int height{};
};

// The card's physical rectangle: top-right of the target monitor's work area, scaled for `dpi`.
// Always inside the work area (including negative-origin monitors); shrunk rather than clipped
// if it does not fit.
OverlayRect overlay_rect(ScreenPoint work_pos, ScreenPoint work_size, int dpi);

// Top-right placement within a work area, with a margin. Top-left origin, y down (Win32); Cocoa
// needs cocoa_origin_y().
ScreenPoint top_right_position(ScreenPoint monitor_pos, ScreenPoint monitor_size,
                               int window_width, int margin);

// Convert a top-down y into Cocoa's bottom-left, y-up coordinates. `work_area_top` is
// NSMaxY(visibleFrame) of the target screen. Legitimately negative below the primary screen; do
// not clamp.
int cocoa_origin_y(int work_area_top, int top_down_y, int window_height);

// Where "Take me back" sits inside a card of `card_size` at `dpi`, relative to the card's
// top-left (the space WM_PAINT and WM_LBUTTONUP use). DPI-scaled like everything else.
OverlayRect overlay_action_rect(ScreenPoint card_size, int dpi);

// Whether a client-space click hit that region. Half-open on the right and bottom edges.
bool overlay_action_hit(ScreenPoint card_size, int dpi, ScreenPoint click);

// The label drawn in that region. One definition so the painter and any future platform
// cannot disagree about what the button says.
const char* overlay_action_label();

// What happens after "Take me back" is clicked and the card is hidden. If the action runs it
// owns the alert's lifecycle (a failed restore keeps its target for a retry); the dismiss
// callback is the fallback for a click nothing acted on.
void settle_overlay_action(const std::function<bool()>& on_action,
                           const std::function<void()>& on_dismiss);

// The multi-line text drawn in the card, built from the snapback payload.
std::string overlay_text(const SnapbackPayload& payload);

// The overlay window. All methods must be called on the UI thread (main.cpp marshals via
// webview.dispatch). instance() returns the per-platform implementation (a no-op where
// unimplemented, so the build stays green cross-platform).
class Overlay {
public:
    virtual ~Overlay() = default;

    // Show the "here's where you left off" card, then auto-dismiss.
    virtual void show(const SnapbackPayload& payload) = 0;
    virtual void dismiss() = 0;

    // Fired on every dismiss (timer, click, dismiss()), so state is cleared even for native
    // dismisses. ContextTracker's Recovering state has no other exit.
    virtual void set_dismiss_callback(std::function<void()> on_dismiss) = 0;

    // Fired only for a click on the "Take me back" region. Returns true when state already
    // settled the alert, in which case the dismiss callback must not run; false means nothing
    // claimed the click and dismissing is the only way to unlatch the tracker.
    virtual void set_action_callback(std::function<bool()> on_action) = 0;

    static Overlay& instance();
};

}  // namespace snapback
