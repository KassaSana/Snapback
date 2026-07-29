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

// Overlay geometry constants.
constexpr int kOverlayWidth = 420;
constexpr int kOverlayHeight = 250;
constexpr int kScreenMargin = 20;

struct ScreenPoint {
    int x{};
    int y{};
};

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
