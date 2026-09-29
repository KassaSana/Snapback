#include "doctest_wrapper.hpp"

#include "app/autostart.hpp"

using namespace snapback;

#if defined(_WIN32)

// Read-only on purpose: the write round trip lives in test_autostart_run_key.cpp against a
// scratch key, so the suite never touches the real Run key.
TEST_CASE("autostart reports a Run-key backend on Windows") {
    CHECK(autostart_supported());
    // Reads the registry and answers either way without throwing; which answer depends on
    // whether the developer running the suite has Snapback set to start at login.
    (void)autostart_enabled();
}

#elif defined(__APPLE__)

// Gave macOS a launchd backend. This test case deliberately does NOT call
// set_autostart_enabled: that writes ~/Library/LaunchAgents, and when this file still had the
// "no backend off Windows" case below, the first run after the backend landed registered the
// *test binary* to launch at every login. That is complaint reproduced exactly
// — a test asserting a no-op stops being harmless the moment the no-op becomes an
// implementation.
//
// The install/remove round trip is covered hermetically in test_autostart_launchd.cpp, which
// passes a temp directory. Nothing here may touch the developer's real login items.
TEST_CASE("autostart reports a launchd backend on macOS") {
    CHECK(autostart_supported());
    // Reads the filesystem and answers either way without throwing; which answer depends on
    // whether the developer running the suite has Snapback set to start at login.
    (void)autostart_enabled();
}

#elif defined(__linux__)

// Second half gave Linux a systemd user unit. Same rule as macOS above: this
// case must not call set_autostart_enabled, because on Linux that now writes a real unit
// into ~/.config/systemd/user plus the graphical-session.target.wants symlink. The
// install/remove round trip is covered hermetically in test_autostart_systemd.cpp, which
// takes its target directory as an argument.
TEST_CASE("autostart reports a systemd backend on Linux") {
    CHECK(autostart_supported());
    (void)autostart_enabled();
}

#endif

// The no-op contract, driven by autostart_supported() rather than a platform #if list: a stale
// #else arm once performed a real install on a CI runner after a backend landed.
// set_autostart_enabled(true) only runs where no backend exists.
TEST_CASE("autostart never claims a success it cannot deliver") {
    if (autostart_supported()) {
        // A backend exists; the round trip belongs in the hermetic per-backend tests, which
        // write to a temp directory instead of the login session. Reading must still be
        // total: answer either way, never throw.
        (void)autostart_enabled();
        return;
    }

    CHECK_FALSE(autostart_enabled());
    CHECK_FALSE(set_autostart_enabled(true));
    CHECK_FALSE(autostart_enabled());
}
