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

// ADR-0007. RFC3339 is the format a timestamp is shown in, not the one it lives in, so these
// two functions are the only crossings between the two — and a crossing that loses or invents
// an instant is the failure the whole decision exists to prevent.

TEST_CASE("rfc3339_from_unix_ms renders the epoch and a known instant") {
    CHECK(rfc3339_from_unix_ms(0) == "1970-01-01T00:00:00Z");
    // 1'700'000'000 s is ManualClock's default wall origin, so this pins the value every
    // clock-seam test stamps records with.
    CHECK(rfc3339_from_unix_ms(1'700'000'000'000) == "2023-11-14T22:13:20Z");
}

TEST_CASE("rfc3339_from_unix_ms floors to the containing second rather than truncating") {
    // Truncation is right for positive values and wrong for negative ones: C++ integer
    // division rounds toward zero, so -1'500 ms would render as second -1 -- one second
    // *after* the instant it names. Both directions are checked so the sign handling cannot
    // be removed as redundant.
    CHECK(rfc3339_from_unix_ms(999) == "1970-01-01T00:00:00Z");
    CHECK(rfc3339_from_unix_ms(1'999) == "1970-01-01T00:00:01Z");
    CHECK(rfc3339_from_unix_ms(-1) == "1969-12-31T23:59:59Z");
    CHECK(rfc3339_from_unix_ms(-1'500) == "1969-12-31T23:59:58Z");
    CHECK(rfc3339_from_unix_ms(-1'000) == "1969-12-31T23:59:59Z");
}

TEST_CASE("unix_ms_from_rfc3339 parses a well-formed UTC timestamp") {
    const auto parsed = unix_ms_from_rfc3339("2023-11-14T22:13:20Z");
    REQUIRE(parsed.has_value());
    CHECK(*parsed == 1'700'000'000'000);
}

TEST_CASE("unix_ms_from_rfc3339 returns nullopt for input that does not parse") {
    // The defect ADR-0007 closes: `datetime()` yields NULL for an unparseable value and
    // `NULL < x` is NULL, so such a row outlives every retention pass with nothing surfaced.
    // 9.14's import path is what made those rows reachable. nullopt rather than a sentinel
    // instant means a caller has to decide what to do instead of comparing against garbage.
    CHECK_FALSE(unix_ms_from_rfc3339("not a timestamp").has_value());
    CHECK_FALSE(unix_ms_from_rfc3339("").has_value());
    CHECK_FALSE(unix_ms_from_rfc3339("2023-11-14").has_value());
}

TEST_CASE("an RFC3339 timestamp survives a round trip through epoch milliseconds") {
    const std::string original = "2026-08-23T09:41:07Z";
    const auto parsed = unix_ms_from_rfc3339(original);
    REQUIRE(parsed.has_value());
    CHECK(rfc3339_from_unix_ms(*parsed) == original);
}

TEST_CASE("has_rfc3339_utc_shape accepts only the exact format this app writes") {
    CHECK(has_rfc3339_utc_shape("2023-11-14T22:13:20Z"));

    // Each of these is a shape a real external file plausibly carries, and each would have
    // reached `std::get_time` before the check existed.
    CHECK_FALSE(has_rfc3339_utc_shape("2023-11-14"));            // date only -- MSVC's midnight
    CHECK_FALSE(has_rfc3339_utc_shape("2023-11-14T22:13:20"));   // no zone marker
    CHECK_FALSE(has_rfc3339_utc_shape("2023-11-14T22:13:20+00:00"));  // offset instead of Z
    CHECK_FALSE(has_rfc3339_utc_shape("2023-11-14T22:13:20.5Z"));     // fractional seconds
    CHECK_FALSE(has_rfc3339_utc_shape("2023-11-14 22:13:20Z"));  // space separator
    CHECK_FALSE(has_rfc3339_utc_shape("2023-11-14T22:13:2zZ"));  // right width, wrong content
}
