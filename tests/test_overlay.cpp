// Tests the overlay's pure helpers: top-right placement math (ported from overlay.rs's
// #[cfg(test)] cases) and the card text formatting. The Win32 window itself is verified
// by running the app.
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
