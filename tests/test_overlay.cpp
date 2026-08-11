// Tests the overlay's pure helpers: top-right placement math, the Cocoa coordinate flip,
// and card text formatting. The Win32/AppKit windows themselves are verified by running
// the app.
#include "doctest_wrapper.hpp"

#include "snapback/overlay.hpp"

using namespace snapback;

TEST_CASE("top_right_position hugs the top-right corner with a margin") {
    auto p = top_right_position({0, 0}, {1920, 1080}, 420, 20);
    CHECK(p.x == 1920 - 420 - 20);
    CHECK(p.y == 20);
}

TEST_CASE("top_right_position accounts for a non-origin monitor") {
    // A monitor to the right of the primary one, positioned at x=1920.
    auto p = top_right_position({1920, 0}, {2560, 1440}, 420, 20);
    CHECK(p.x == 1920 + 2560 - 420 - 20);
    CHECK(p.y == 20);
}

TEST_CASE("top_right_position uses the configured width and margin") {
    auto p = top_right_position({0, 0}, {1000, 800}, 300, 50);
    CHECK(p.x == 1000 - 300 - 50);
    CHECK(p.y == 50);
}

// --- Cocoa coordinate flip -------------------------------------------------------------
//
// These run on every CI host, not just macOS, because the flip is the only part of the
// NSPanel placement that can be tested without a screen — and it is the part most likely
// to be wrong.

TEST_CASE("cocoa_origin_y drops a full window height below the work area's top edge") {
    // A 1920x1080 primary screen with a 25pt menu bar: visibleFrame tops out at y=1055.
    // The card should sit its own height plus the margin below that.
    CHECK(cocoa_origin_y(1055, 20, 250) == 1055 - 20 - 250);
}

TEST_CASE("cocoa_origin_y leaves exactly the requested gap above the window") {
    // The invariant the formula exists for, stated as a round trip: the distance from the
    // work area's top edge down to the window's *top* edge is the top-down y we asked for.
    // Forgetting the window height still satisfies "y is below the top edge", so only this
    // check distinguishes the correct answer from the classic off-by-one-window bug.
    constexpr int kWorkAreaTop = 1055;
    constexpr int kTopDownY = 20;
    constexpr int kHeight = 250;

    const int origin_y = cocoa_origin_y(kWorkAreaTop, kTopDownY, kHeight);
    CHECK(kWorkAreaTop - (origin_y + kHeight) == kTopDownY);
}

TEST_CASE("cocoa_origin_y follows the placement policy from top_right_position") {
    // How the .mm file actually calls the pair: monitor_pos.y is passed as 0 so that
    // top_right_position's y comes back as a pure distance below the work area's top edge,
    // which is the input cocoa_origin_y expects. x needs no conversion — both spaces grow
    // rightward from the same origin.
    const auto p = top_right_position({0, 0}, {1920, 1055}, kOverlayWidth, kScreenMargin);
    CHECK(p.x == 1920 - kOverlayWidth - kScreenMargin);
    CHECK(cocoa_origin_y(1055, p.y, kOverlayHeight) == 1055 - kScreenMargin - kOverlayHeight);
}

TEST_CASE("cocoa_origin_y handles a screen stacked above the primary") {
    // Cocoa's global space is unbounded upward: a second display above the primary puts
    // the work area's top edge well past the primary screen's height.
    CHECK(cocoa_origin_y(2135, 20, 250) == 1865);
}

TEST_CASE("cocoa_origin_y stays negative for a screen below the primary") {
    // A display mounted below the primary lives at negative y. The answer is meant to be
    // negative and must not be clamped — clamping would drag the card onto the primary
    // screen, which is the bug this case exists to prevent.
    CHECK(cocoa_origin_y(-100, 20, 250) == -370);
}

TEST_CASE("overlay_text leads with the summary and includes file hint + duration") {
    SnapbackPayload payload;
    payload.summary = "Return to auth.ts";
    payload.app_name = "Cursor";
    payload.file_hint = "auth.ts";
    payload.distraction_duration_secs = 45;

    auto text = overlay_text(payload);
    CHECK(text.find("Return to auth.ts") != std::string::npos);
    CHECK(text.find("auth.ts") != std::string::npos);
    CHECK(text.find("Away 45s") != std::string::npos);
    CHECK(text.find("Cursor") != std::string::npos);
}

TEST_CASE("overlay_text falls back to the app name when there is no summary") {
    SnapbackPayload payload;
    payload.app_name = "Terminal";
    payload.distraction_duration_secs = 12;

    auto text = overlay_text(payload);
    CHECK(text.find("Return to Terminal") != std::string::npos);
    CHECK(text.find("Away 12s") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Roadmap 10.12 — monitor and DPI awareness.
// ---------------------------------------------------------------------------

namespace {

// Every case below asserts this, because it is the property that actually matters: whatever
// the monitor's origin, size, taskbar edge, or scale, the card is fully inside the work area.
bool inside(const OverlayRect& r, ScreenPoint pos, ScreenPoint size) {
    return r.x >= pos.x && r.y >= pos.y && r.x + r.width <= pos.x + size.x &&
           r.y + r.height <= pos.y + size.y;
}

}  // namespace

TEST_CASE("choose_overlay_monitor prefers where the user was looking") {
    // The foreground window is the display the distraction happened on. Anything else puts the
    // nudge on a screen the user is not looking at, which is 10.12's headline symptom.
    CHECK(choose_overlay_monitor(true, true) == OverlayMonitorSource::kForegroundWindow);
    CHECK(choose_overlay_monitor(true, false) == OverlayMonitorSource::kForegroundWindow);
    // The item's explicit fallback: there may be no foreground window at all.
    CHECK(choose_overlay_monitor(false, true) == OverlayMonitorSource::kCursor);
    // Last resort — also what an unplugged target monitor degrades to.
    CHECK(choose_overlay_monitor(false, false) == OverlayMonitorSource::kPrimary);
}

TEST_CASE("scale_for_dpi covers the standard Windows scale factors") {
    CHECK(scale_for_dpi(420, 96) == 420);    // 100%
    CHECK(scale_for_dpi(420, 120) == 525);   // 125%
    CHECK(scale_for_dpi(420, 144) == 630);   // 150%
    CHECK(scale_for_dpi(420, 192) == 840);   // 200%
    CHECK(scale_for_dpi(20, 144) == 30);

    // Rounds to nearest instead of truncating: at 150% a hairline border must not stay
    // hairline, which is how a "scaled" UI ends up looking unscaled in its details.
    CHECK(scale_for_dpi(1, 144) == 2);
    CHECK(scale_for_dpi(3, 120) == 4);

    // A failed Win32 DPI query returns 0. Falling through to the base keeps the card its
    // design size rather than collapsing it to nothing.
    CHECK(scale_for_dpi(420, 0) == 420);
    CHECK(scale_for_dpi(420, -1) == 420);
}

TEST_CASE("overlay_rect places the card top-right at 100%") {
    const ScreenPoint pos{0, 0};
    const ScreenPoint size{1920, 1040};  // taskbar along the bottom
    const auto r = overlay_rect(pos, size, 96);

    CHECK(r.width == kOverlayWidth);
    CHECK(r.height == kOverlayHeight);
    CHECK(r.x == 1920 - kOverlayWidth - kScreenMargin);
    CHECK(r.y == kScreenMargin);
    CHECK(inside(r, pos, size));
}

TEST_CASE("overlay_rect scales the card and its margin with the monitor") {
    const ScreenPoint pos{0, 0};
    const ScreenPoint size{3840, 2160};

    for (const int dpi : {96, 120, 144, 192}) {
        CAPTURE(dpi);
        const auto r = overlay_rect(pos, size, dpi);
        CHECK(r.width == scale_for_dpi(kOverlayWidth, dpi));
        CHECK(r.height == scale_for_dpi(kOverlayHeight, dpi));
        // The gap scales too. A card that grows while its margin does not drifts into the
        // corner and reads as misaligned at high scale.
        CHECK(r.y == scale_for_dpi(kScreenMargin, dpi));
        CHECK(inside(r, pos, size));
    }
}

TEST_CASE("overlay_rect lands on monitors above and to the left of the primary one") {
    // The case SPI_GETWORKAREA could never express: these origins are negative, and a
    // placement written against the primary work area puts the card on the wrong screen or
    // off every screen.
    struct Layout {
        ScreenPoint pos;
        ScreenPoint size;
    };
    const Layout layouts[] = {
        {{-1920, 0}, {1920, 1040}},        // left of primary
        {{0, -1080}, {1920, 1040}},        // above primary
        {{-2560, -1440}, {2560, 1400}},    // up and to the left
        {{1920, -300}, {2560, 1400}},      // right and higher
    };

    for (const auto& layout : layouts) {
        CAPTURE(layout.pos.x);
        CAPTURE(layout.pos.y);
        const auto r = overlay_rect(layout.pos, layout.size, 96);
        CHECK(r.x == layout.pos.x + layout.size.x - kOverlayWidth - kScreenMargin);
        CHECK(r.y == layout.pos.y + kScreenMargin);
        CHECK(inside(r, layout.pos, layout.size));
        // Explicitly: the answer is allowed to be negative and must not be clamped to zero.
        if (layout.pos.y < 0) CHECK(r.y < 0);
    }
}

TEST_CASE("overlay_rect respects a taskbar on any edge") {
    // A work area already excludes the taskbar; what matters is that the card is placed
    // against *that* rectangle rather than the full monitor bounds.
    struct Edge {
        const char* name;
        ScreenPoint pos;
        ScreenPoint size;
    };
    const Edge edges[] = {
        {"bottom", {0, 0}, {1920, 1040}},
        {"top", {0, 40}, {1920, 1040}},
        {"left", {60, 0}, {1860, 1080}},
        {"right", {0, 0}, {1860, 1080}},
    };

    for (const auto& edge : edges) {
        CAPTURE(edge.name);
        const auto r = overlay_rect(edge.pos, edge.size, 96);
        CHECK(inside(r, edge.pos, edge.size));
        // A top-docked taskbar is the one that catches a hard-coded y of 20.
        CHECK(r.y >= edge.pos.y);
    }
}

TEST_CASE("overlay_rect shrinks rather than clips when the card cannot fit") {
    // 200% on a small panel. At 192 DPI the card wants 840x500 physical pixels and the margin
    // wants 40 on each side, so an 800x460 work area cannot hold it in either axis. Clipping
    // loses the controls at the card's edge, so the size gives way instead.
    const ScreenPoint pos{0, 0};
    const ScreenPoint size{800, 460};
    const auto r = overlay_rect(pos, size, 192);

    // Both axes are genuinely constrained here — the first draft of this case used a work area
    // the scaled card still fitted inside, so it asserted nothing and passed with the clamp
    // deleted.
    CHECK(scale_for_dpi(kOverlayWidth, 192) > size.x);
    CHECK(scale_for_dpi(kOverlayHeight, 192) > size.y);

    CHECK(r.width < scale_for_dpi(kOverlayWidth, 192));
    CHECK(r.height < scale_for_dpi(kOverlayHeight, 192));
    CHECK(r.width > 0);
    CHECK(r.height > 0);
    CHECK(inside(r, pos, size));
}

TEST_CASE("overlay_rect stays on screen for a degenerate work area") {
    // An unplugged or mid-reconfiguration monitor can report something absurd. The card must
    // still get a positive extent inside whatever it was given rather than a negative width
    // that SetWindowPos would take literally.
    for (const auto& size : {ScreenPoint{10, 10}, ScreenPoint{1, 1}, ScreenPoint{0, 0}}) {
        CAPTURE(size.x);
        const auto r = overlay_rect({0, 0}, size, 192);
        CHECK(r.width >= 1);
        CHECK(r.height >= 1);
        CHECK(r.x >= 0);
        CHECK(r.y >= 0);
    }
}
