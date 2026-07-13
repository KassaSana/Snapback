#include <doctest/doctest.h>

#include "capture/permissions.hpp"

using namespace snapback;

TEST_CASE("permission status reports a non-empty platform message") {
    auto status = check_capture_permissions(true);

    CHECK(status.capture_probe_confirmed);
    CHECK_FALSE(status.message.empty());
#if defined(_WIN32)
    CHECK(status.capture_available);
    CHECK(status.active_window_available);
#endif
}
