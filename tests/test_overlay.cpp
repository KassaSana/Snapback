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
