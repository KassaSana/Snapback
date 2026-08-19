#include "doctest_wrapper.hpp"

#include <string>

#include "app/state.hpp"
#include "snapback/focus_window.hpp"
#include "storage/storage.hpp"

TEST_SUITE("focus_window") {

TEST_CASE("focus_window refuses empty app_name and window_title") {
    const auto result = snapback::focus_window("", "");
    CHECK_FALSE(result.ok);
    CHECK(result.message == "No target application or window specified");
}

TEST_CASE("focus_window refuses whitespace-only targets") {
    const auto result = snapback::focus_window("   ", "\t\n");
    CHECK_FALSE(result.ok);
    CHECK(result.message == "No target application or window specified");
}

// Both halves of this are the same promise -- never claim success -- but the honest
// *reason* differs by platform. Where activation is implemented, the search ran and found
// nothing. Where it is not, saying "could not find" would imply a search that never
// happened, so the stub reports the real reason instead.
TEST_CASE("focus_window reports honest failure for nonexistent window") {
    const auto result = snapback::focus_window("NonExistentFakeProcess999999.exe", "NonExistentFakeTitle999999");
    CHECK_FALSE(result.ok);
#if defined(_WIN32) || defined(__APPLE__)
    CHECK(result.message.find("Could not find") != std::string::npos);
#else
    CHECK(result.message == "Window activation is not supported on this platform");
#endif
}

TEST_CASE("focus_window_supported reflects platform capabilities") {
#if defined(_WIN32) || defined(__APPLE__)
    CHECK(snapback::focus_window_supported() == true);
#else
    CHECK(snapback::focus_window_supported() == false);
#endif
}

TEST_CASE("AppState restore_snapback_target returns not-ok when no snapback is active") {
    auto storage = snapback::Storage::open_memory();
    REQUIRE(storage.has_value());
    snapback::AppState state(std::move(*storage));

    const auto result = state.restore_snapback_target();
    CHECK_FALSE(result.ok);
    CHECK(result.message == "No active snapback context to restore");
}


}  // TEST_SUITE
