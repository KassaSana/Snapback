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

// Roadmap 2.16. The quiet-hours reading. These cases are written to hold in *any* timezone,
// because CI runs them on four toolchains and `TZ` is a process global — a case that sets it
// is a case that can interfere with another running beside it. The one case that does set it
// is POSIX-only below, matching what `local_hour_from_rfc3339` already does here.

TEST_CASE("local_minute_of_day_from_unix_ms agrees with local_hour_from_rfc3339") {
    // Whatever this machine's offset is, the two must describe the same local reading, or one
    // of them is lying about what time it is.
    const std::int64_t when = 1'700'000'000'000;
    const auto minutes = local_minute_of_day_from_unix_ms(when);
    REQUIRE(minutes.has_value());
    const int hour = local_hour_from_rfc3339(rfc3339_from_unix_ms(when));
    REQUIRE(hour >= 0);
    CHECK(*minutes / 60 == hour);
}

TEST_CASE("local_minute_of_day_from_unix_ms answers within a single day") {
    for (const std::int64_t when : {std::int64_t{0}, std::int64_t{1'700'000'000'000}}) {
        const auto minutes = local_minute_of_day_from_unix_ms(when);
        REQUIRE(minutes.has_value());
        CHECK(*minutes >= 0);
        CHECK(*minutes < 1440);
    }
}

TEST_CASE("local_minute_of_day_from_unix_ms reports the same minute across one second") {
    // Sub-second precision must not leak into the reading: every instant inside a second is
    // the same wall-clock minute.
    const std::int64_t base = 1'700'000'000'000;
    CHECK(local_minute_of_day_from_unix_ms(base) == local_minute_of_day_from_unix_ms(base + 999));
}

TEST_CASE("local_minute_of_day_from_unix_ms resolves minutes, not just the hour") {
    // The reason this function exists rather than a call to the hour-granularity one: 22:30 is
    // an ordinary bedtime, and a quiet range that can only start on the hour cannot express it.
    const std::int64_t base = 1'700'000'000'000;
    const auto first = local_minute_of_day_from_unix_ms(base);
    const auto later = local_minute_of_day_from_unix_ms(base + 30 * 60 * 1000);
    REQUIRE(first.has_value());
    REQUIRE(later.has_value());
    CHECK((*later - *first + 1440) % 1440 == 30);
}

#if defined(_WIN32)
TEST_CASE("local_minute_of_day_from_unix_ms reports nullopt for a pre-epoch instant on Windows") {
    // The Windows CRT's `localtime_s` rejects a negative time_t. Pinned rather than worked
    // around: no caller in this app reads local time for a historical instant, and inventing
    // a value here would be the sentinel the nullopt contract exists to avoid.
    CHECK_FALSE(local_minute_of_day_from_unix_ms(-86'400'000).has_value());
}
#else
TEST_CASE("local_minute_of_day_from_unix_ms floors a pre-epoch instant") {
    // Same hazard `rfc3339_from_unix_ms` guards: -1500 ms is 1.5 s before the epoch and falls
    // in second -2, so truncating toward zero would report the wrong minute for any instant in
    // the second before midnight.
    const auto floored = local_minute_of_day_from_unix_ms(-1500);
    REQUIRE(floored.has_value());
    CHECK(floored == local_minute_of_day_from_unix_ms(-2000));
}

TEST_CASE("local_minute_of_day_from_unix_ms converts into the local offset") {
    TimezoneGuard timezone("UTC-2");
    // 19:00 UTC is 21:00 in UTC-2, and the minute field survives the conversion.
    const auto when = unix_ms_from_rfc3339("2026-07-11T19:30:00Z");
    REQUIRE(when.has_value());
    CHECK(local_minute_of_day_from_unix_ms(*when) == 21 * 60 + 30);
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
