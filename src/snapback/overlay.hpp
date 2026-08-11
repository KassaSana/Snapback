// The snapback overlay window.
//
// The C++ app uses a native, borderless, always-on-top Win32 window because webview
// can't easily run a second WebView2 loop on the same thread, so it uses a native,
// borderless, always-on-top Win32 window. Kept as an interface so the platform choice is
// swappable; the placement math + text formatting are pure free functions and can be
// unit-tested without a real window.
#pragma once

#include <functional>
#include <string>

#include "types.hpp"

namespace snapback {

// Overlay geometry constants, in design units at 96 DPI. Roadmap 10.12: these are no longer
// pixel counts. On a 200% display, 420 design units is 840 physical pixels, and treating them
// as pixels is what makes the card render physically tiny on a high-DPI monitor.
constexpr int kOverlayWidth = 420;
constexpr int kOverlayHeight = 250;
constexpr int kScreenMargin = 20;

// The DPI the constants above are written against. Windows' own baseline (USER_DEFAULT_SCREEN_DPI).
constexpr int kOverlayBaseDpi = 96;

struct ScreenPoint {
    int x{};
    int y{};
};

// Roadmap 10.12. Which display the card belongs on, decided as a policy rather than inline at
// the call site so the fallback order is testable without a second monitor.
//
// The foreground window is the right answer because it is *where the user was looking* — a
// nudge caused by what happened on one screen appearing on another is the bug this replaces.
// The cursor is the explicit fallback for the case the item names: there may be no foreground
// window at all (it was just closed, or the desktop has focus). Primary is the last resort, and
// is also what a monitor unplugged between the snapback firing and the card showing degrades
// to — an overlay on a display that no longer exists is worse than one in the wrong corner.
enum class OverlayMonitorSource { kForegroundWindow, kCursor, kPrimary };

OverlayMonitorSource choose_overlay_monitor(bool foreground_monitor_valid,
                                            bool cursor_monitor_valid);

// Scale a design-unit length to physical pixels for a monitor's effective DPI.
//
// Rounds to nearest rather than truncating: at 150% a 1-unit border truncates to 1 pixel and
// stays hairline, which is how "scaled" UIs end up looking unscaled in their details. A
// nonsensical DPI (0 or negative, which is what the Win32 query returns on failure) falls back
// to the base rather than collapsing every dimension to zero.
int scale_for_dpi(int design_units, int dpi);

struct OverlayRect {
    int x{};
    int y{};
    int width{};
    int height{};
};

// The card's physical rectangle: top-right of `work_pos`/`work_size`, scaled for `dpi`.
//
// The work area is passed in rather than queried so this stays pure. It is the *target
// monitor's* work area, which is the other half of 10.12's fix — Windows was asking
// SPI_GETWORKAREA, which always answers for the primary display no matter where the user was.
//
// Two properties the tests pin, because both fail silently rather than loudly:
//   - the result always lies inside the work area, including on monitors whose origin is
//     negative (mounted above or to the left of the primary one)
//   - a card that would not fit is shrunk to the work area rather than allowed to clip, which
//     is the realistic outcome at 200% on a small laptop panel
OverlayRect overlay_rect(ScreenPoint work_pos, ScreenPoint work_size, int dpi);

// Top-right placement within a monitor's work area, with a margin.
//
// Answers in the top-left-origin, y-grows-downward convention: y is the distance measured
// *downward* from the work area's top edge. That is what Win32 wants directly; Cocoa needs
// cocoa_origin_y() below.
ScreenPoint top_right_position(ScreenPoint monitor_pos, ScreenPoint monitor_size,
                               int window_width, int margin);

// Convert a top-down y into the y Cocoa wants.
//
// Cocoa positions a window by its BOTTOM-left corner on an axis that grows *upward* from
// the bottom of the primary screen, so both the direction and the reference corner differ
// from what top_right_position() returns. `work_area_top` is the top edge of the target
// screen's visible frame in that same Cocoa space (NSMaxY of NSScreen.visibleFrame).
//
// This is a pure function with tests rather than three lines inside the .mm file because
// getting it wrong fails quietly: the card lands at the bottom of the screen, or off it
// entirely on a display that is not the primary one. The result is legitimately negative
// for a screen mounted below the primary, so callers must not clamp it to zero.
int cocoa_origin_y(int work_area_top, int top_down_y, int window_height);

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

    // Fired whenever the card is dismissed — by the auto-dismiss timer, a click, or an
    // explicit dismiss() call — so a caller can clear app state (ContextTracker's
    // Recovering state has no other exit) even when the user dismisses natively instead
    // of through the IPC `dismiss_snapback` command. main.cpp wires this once at startup.
    virtual void set_dismiss_callback(std::function<void()> on_dismiss) = 0;

    static Overlay& instance();
};

}  // namespace snapback
