#include "doctest_wrapper.hpp"

#include "app/autostart.hpp"

using namespace snapback;

TEST_CASE("autostart_command_line quotes the path so spaces parse correctly") {
    CHECK(autostart_command_line("C:\\Program Files\\Snapback\\snapback.exe") ==
          "\"C:\\Program Files\\Snapback\\snapback.exe\"");
    CHECK(autostart_command_line("/usr/local/bin/snapback") ==
          "\"/usr/local/bin/snapback\"");
}

#if defined(_WIN32)

TEST_CASE("autostart reports a Windows backend") {
    CHECK(autostart_supported());
}

// Real round-trip against HKCU\...\Run — per-user, no elevation, safe to touch from a
// test (this is the same key any consumer app would write). Always restore the prior
// state so the test binary doesn't leave itself registered to autostart.
//
// The write is *not* asserted, deliberately (Roadmap 11.7). Writing the Run key is a
// textbook persistence technique, so a hardened environment may refuse it — and 2 of the
// first 6 observed Windows CI job runs did, alternating between the two Windows jobs on
// identical code. "The OS declined to let anything register for autostart" is not a defect
// in our code, so it must not read as one. Everything after the write is still asserted, so
// a genuine regression in the round-trip still fails loudly wherever the write is permitted.
// The real fix is to stop touching the shared key from tests at all — see 11.7.
TEST_CASE("autostart_enabled round-trips through the real Windows registry") {
    const bool had_prior_state = autostart_enabled();

    if (!set_autostart_enabled(true)) {
        MESSAGE("skipped: this environment refused the HKCU Run-key write (Roadmap 11.7)");
        return;
    }
    CHECK(autostart_enabled());

    REQUIRE(set_autostart_enabled(false));
    CHECK_FALSE(autostart_enabled());

    // Disabling an already-disabled entry is idempotent, not a failure.
    REQUIRE(set_autostart_enabled(false));
    CHECK_FALSE(autostart_enabled());

    if (had_prior_state) set_autostart_enabled(true);
}

#elif defined(__APPLE__)

// Roadmap 3.0 gave macOS a launchd backend. This test case deliberately does NOT call
// set_autostart_enabled: that writes ~/Library/LaunchAgents, and when this file still had the
// "no backend off Windows" case below, the first run after the backend landed registered the
// *test binary* to launch at every login. That is Roadmap 11.7's complaint reproduced exactly
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

// Roadmap 3.0's second half gave Linux a systemd user unit. Same rule as macOS above: this
// case must not call set_autostart_enabled, because on Linux that now writes a real unit
// into ~/.config/systemd/user plus the graphical-session.target.wants symlink. The
// install/remove round trip is covered hermetically in test_autostart_systemd.cpp, which
// takes its target directory as an argument.
TEST_CASE("autostart reports a systemd backend on Linux") {
    CHECK(autostart_supported());
    (void)autostart_enabled();
}

#endif

// The no-op contract, driven by what the code reports rather than by a duplicated platform
// list.
//
// This used to be the `#else` arm of the platform chain above, and it is how CI run
// 30607879815 came to **write a real systemd user unit onto a GitHub runner**: the chain
// guarded only _WIN32 and __APPLE__, so when commit 2c89f8c gave Linux a backend, Linux kept
// falling into the "no backend" arm and its `CHECK_FALSE(set_autostart_enabled(true))`
// started performing the install it was asserting could not happen.
//
// The macOS fix for the identical incident a day earlier did not generalise, because **the
// stale thing was the guard, not the assertion.** A `#else` arm is a claim about every
// platform that does not have a backend *yet*, and it silently shrinks every time one lands
// — while the one line inside it that mutates the machine keeps running.
//
// So the branch is now the runtime answer. `set_autostart_enabled(true)` can only execute
// where `autostart_supported()` is false, which is precisely where it is defined to do
// nothing. Adding a fourth backend cannot reintroduce this, with or without anyone
// remembering to update this file.
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
