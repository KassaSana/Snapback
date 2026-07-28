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

#if defined(_WIN32)
TEST_CASE("requesting permissions succeeds on Windows without a dialog") {
    // Windows hooks need no consent, so the request is a no-op that reports success.
    CHECK(request_capture_permissions());
}
#endif
