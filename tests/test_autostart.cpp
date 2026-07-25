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

#else

// No backend yet on this platform (Roadmap 1.3 follow-up: launchd/systemd) — must
// degrade to a documented no-op, never throw or silently claim success.
TEST_CASE("autostart is a documented no-op on platforms without a backend yet") {
    CHECK_FALSE(autostart_supported());
    CHECK_FALSE(autostart_enabled());
    CHECK_FALSE(set_autostart_enabled(true));
    CHECK_FALSE(autostart_enabled());
}

#endif
