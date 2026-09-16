#include "doctest_wrapper.hpp"

#include "capture/permissions.hpp"

using namespace snapback;

TEST_CASE("permission status reports a non-empty platform message") {
    auto status = check_capture_permissions(true, true);

    CHECK(status.capture_probe_confirmed ==
          (status.capture_available && status.active_window_available));
    CHECK_FALSE(status.message.empty());
#if defined(_WIN32)
    CHECK(status.capture_available);
    CHECK(status.active_window_available);
#endif
}

TEST_CASE("permission probe requires a running usable capture backend") {
    const auto stopped = check_capture_permissions(false, true);
    CHECK_FALSE(stopped.capture_probe_confirmed);

    const auto unobserved = check_capture_permissions(true, false);
    CHECK_FALSE(unobserved.capture_probe_confirmed);

    const auto observed = check_capture_permissions(true, true);
    CHECK(observed.capture_probe_confirmed ==
          (observed.capture_available && observed.active_window_available));
}

TEST_CASE("checking permissions never changes the reported state") {
    // check_capture_permissions is called on every health poll, so it has to be a pure
    // probe. If it ever started prompting, this would be the test that caught the
    // resulting dialog spam — repeated calls must agree.
    const auto first = check_capture_permissions(true, true);
    const auto second = check_capture_permissions(true, true);

    CHECK(first.capture_available == second.capture_available);
    CHECK(first.active_window_available == second.active_window_available);
    CHECK(first.message == second.message);
}

TEST_CASE("invalidating the probe cache does not change the answer") {
    // The cache only amortises; it must never be the thing that decides. A probe after
    // invalidation has to agree with the one before it, or the "check again" button in
    // onboarding would show a different status from the health poll for no reason.
    const auto before = check_capture_permissions(true, true);
    invalidate_permission_probe_cache();
    const auto after = check_capture_permissions(true, true);

    CHECK(before.capture_available == after.capture_available);
    CHECK(before.active_window_available == after.active_window_available);
    CHECK(before.setup_steps == after.setup_steps);
}

#if defined(_WIN32)
TEST_CASE("requesting permissions succeeds on Windows without a dialog") {
    // Windows hooks need no consent, so the request is a no-op that reports success.
    CHECK(request_capture_permissions());
}
#endif
