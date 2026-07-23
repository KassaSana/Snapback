#include "doctest_wrapper.hpp"

#include <cstdlib>

#include "util/time.hpp"

using namespace snapback;

namespace {

class TimezoneGuard {
public:
    explicit TimezoneGuard(const char* timezone) {
#if defined(_WIN32)
        _putenv_s("TZ", timezone);
        _tzset();
#else
        setenv("TZ", timezone, 1);
        tzset();
#endif
    }

    ~TimezoneGuard() {
#if defined(_WIN32)
        _putenv_s("TZ", "");
        _tzset();
#else
        unsetenv("TZ");
        tzset();
#endif
    }
};

}  // namespace

#if defined(_WIN32)
TEST_CASE("local_hour_from_rfc3339 rejects malformed timestamps on Windows") {
    CHECK(local_hour_from_rfc3339("not a timestamp") == -1);
}
#else
TEST_CASE("local_hour_from_rfc3339 converts UTC timestamps to local time") {
    TimezoneGuard timezone("UTC-2");
    CHECK(local_hour_from_rfc3339("2026-07-11T19:00:00Z") == 21);
    CHECK(local_hour_from_rfc3339("not a timestamp") == -1);
}
#endif
