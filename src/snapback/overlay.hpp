// The snapback overlay window. Rust: snapback/overlay.rs (a second Tauri window).
//
// In Tauri this is another WebviewWindow loading snapback.html. webview/webview can't
// easily run a second WebView2 loop on the same thread, so the C++ port uses a native,
// borderless, always-on-top Win32 window. Kept as an interface so the platform choice is
// swappable; the placement math + text formatting are pure free functions so they can be
// unit-tested without a real window (as overlay.rs did for positioning).
#pragma once

#include <functional>
#include <string>

#include "types.hpp"

namespace snapback {

// Overlay geometry, ported from overlay.rs constants.
constexpr int kOverlayWidth = 420;
constexpr int kOverlayHeight = 250;
constexpr int kScreenMargin = 20;

struct ScreenPoint {
    int x{};
    int y{};
};

// Top-right placement within a monitor's work area, with a margin. Ported 1:1 from
// overlay.rs::top_right_position — this is what hugs the corner instead of centering.
ScreenPoint top_right_position(ScreenPoint monitor_pos, ScreenPoint monitor_size,
                               int window_width, int margin);

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
