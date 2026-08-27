#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace snapback {

// Roadmap 10.13. How long a gap between consecutive predictions may be before it stops being
// "still the same unbroken stretch of focus".
//
// Predictions arrive only when input produces a reading, throttled to about one a second, and
// **not at all** while the user is idle or in private mode. So a gap is not a pause in the
// data — it is a pause in the user, and there is no honest way to fill it. Two minutes is well
// above the throttle and well below 7.23's five-minute idle default, so an ordinary pause to
// read a paragraph stays inside a run while walking away ends it.
//
// This is also the whole interpolation policy, stated rather than implied: an interval counts
// at face value up to this bound, and beyond it counts as nothing and breaks the run. Nothing
// is inferred about what happened during a gap.
inline constexpr std::int64_t kFocusRunGapSecs = 120;

// Whether a string has the exact shape this app writes: `YYYY-MM-DDTHH:MM:SSZ`, 20 characters,
// UTC, no offset and no fractional part.
//
// Checked explicitly rather than left to `std::get_time`, because `get_time` does not agree
// with itself across the four toolchains CI builds on. MSVC accepts `"2023-11-14"` against the
// full format above without setting `failbit` -- it runs out of input after `%d` and leaves the
// hour, minute, and second at zero -- so a date-only value silently becomes midnight UTC. That
// is the ADR-0007 hazard exactly: not a value that fails to parse loudly, but one that parses
// into a plausible wrong instant. While every row came from this app's own writer it could not
// happen; 9.14's import path is what made foreign values reachable.
//
// A shape check is also cheaper than a parse for the rejection case, which is the common one
// on an import of a file that was never this app's to begin with.
inline bool has_rfc3339_utc_shape(const std::string& timestamp) {
    if (timestamp.size() != 20) return false;
    // Positions are fixed by the format, so this is a table rather than a scan. `'\0'` marks a
    // digit; anything else is the literal that must appear there. A NUL rather than `-1`
    // because plain `char` is unsigned on some of the platforms this builds for, where a
    // negative initializer is a narrowing error rather than the sentinel it looks like.
    constexpr char kLiterals[20] = {0,   0, 0, 0, '-', 0, 0, '-', 0, 0,
                                    'T', 0, 0, ':', 0, 0, ':', 0, 0, 'Z'};
    for (std::size_t i = 0; i < timestamp.size(); ++i) {
        const char c = timestamp[i];
        if (kLiterals[i] == '\0') {
            if (c < '0' || c > '9') return false;
        } else if (c != kLiterals[i]) {
            return false;
        }
    }
    return true;
}

// Epoch seconds from an RFC3339 UTC timestamp, or nullopt if it will not parse. Used to turn
// prediction timestamps into durations; `local_hour_from_rfc3339` below answers a different
// question (which local hour) and deliberately keeps its own conversion.
inline std::optional<std::int64_t> epoch_secs_from_rfc3339(const std::string& timestamp) {
    if (!has_rfc3339_utc_shape(timestamp)) return std::nullopt;
    std::tm utc{};
    std::istringstream input(timestamp);
    input >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) return std::nullopt;
#if defined(_WIN32)
    const std::time_t epoch = _mkgmtime(&utc);
#else
    const std::time_t epoch = timegm(&utc);
#endif
    if (epoch == static_cast<std::time_t>(-1)) return std::nullopt;
    return static_cast<std::int64_t>(epoch);
}

// ADR-0007's two edge conversions.
//
// A point in time is UTC milliseconds since the epoch. RFC3339 is an **output format**
// produced when a human or an external file has to read one, and an **input format** accepted
// from the same places — never a storage format and never a thing to compare. These two
// functions are where each crossing happens, so that a comparison against a parsed string
// stays impossible to write by accident.
//
// Whole-second RFC3339, matching the shape every other timestamp in this app is displayed in.
// Milliseconds are dropped on the way out rather than rendered, because the sub-second field
// exists to make *ordering* well-defined, not to be shown; adding `.000` here would change the
// width of every timestamp the UI and the CSV exports already emit.
inline std::string rfc3339_from_unix_ms(std::int64_t unix_ms) {
    // Floor, not truncate: -1500 ms is 1.5 s before the epoch, which falls in second -2. C++
    // integer division rounds toward zero and would put it in second -1, one second late.
    const std::int64_t secs = unix_ms >= 0 ? unix_ms / 1000 : (unix_ms - 999) / 1000;
    const std::time_t when = static_cast<std::time_t>(secs);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &when) != 0) return {};
#else
    if (gmtime_r(&when, &utc) == nullptr) return {};
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

// The inverse, for the values that arrive as text: 9.14's import path and the fixture data.
// nullopt rather than a sentinel, so an unparseable value cannot be mistaken for a real
// instant — the whole class of defect ADR-0007 exists to close is a malformed timestamp that
// silently compares as something.
inline std::optional<std::int64_t> unix_ms_from_rfc3339(const std::string& timestamp) {
    const auto secs = epoch_secs_from_rfc3339(timestamp);
    if (!secs) return std::nullopt;
    return *secs * 1000;
}

inline int local_hour_from_rfc3339(const std::string& timestamp) {
    if (!has_rfc3339_utc_shape(timestamp)) return -1;
    std::tm utc{};
    std::istringstream input(timestamp);
    input >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) return -1;

#if defined(_WIN32)
    const std::time_t epoch = _mkgmtime(&utc);
#else
    const std::time_t epoch = timegm(&utc);
#endif
    if (epoch == static_cast<std::time_t>(-1)) return -1;

    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &epoch) != 0) return -1;
#else
    if (localtime_r(&epoch, &local) == nullptr) return -1;
#endif
    return local.tm_hour;
}

// Roadmap 2.16. Local minutes since midnight for a UTC instant, or nullopt if the platform's
// local-time conversion fails.
//
// A sibling of `local_hour_from_rfc3339` rather than a caller of it, for two reasons that are
// both correctness rather than taste. It takes epoch milliseconds, so asking "what time is it
// locally" does not have to round-trip an instant through a display format and reparse it --
// the crossing ADR-0007 exists to make impossible to write by accident. And it answers in
// minutes, because 22:30 is an ordinary bedtime and an hour-granularity answer cannot say it.
//
// This is the *only* place a quiet-hours range touches local time, and it is deliberately not
// cached anywhere. A quiet range is not an instant: it is a predicate over the local reading,
// re-evaluated every time. Storing "quiet ends at 07:00 local" as a UTC instant would be the
// tempting shortcut and the bug -- a DST change or a flight moves the offset, the stored
// instant now means 06:00 or 08:00, and the user's quiet hours have silently shifted with
// nothing anywhere saying so. Recomputing costs a `localtime` call and can never drift.
//
// The honest consequences of that choice, stated so nobody later "fixes" them: on a fall-back
// day a range covering 01:00-02:00 is quiet twice, and on a spring-forward day a range wholly
// inside the skipped hour never fires at all. Both are what "quiet between 1 and 2 AM local"
// actually means.
//
// nullopt rather than -1, for the same reason the parsers above return nullopt: a sentinel
// that compares as a real reading is how a failed conversion quietly becomes "it is midnight,
// everything is quiet".
//
// One platform difference the callers must not assume away: the Windows CRT's `localtime_s`
// rejects a negative `time_t`, so a pre-epoch instant answers nullopt there while POSIX
// converts it. No caller in this app can reach that -- a quiet-hours check reads the clock,
// not history -- but it is why the nullopt path is a real branch rather than a formality, and
// why the callers fail *open* on it.
inline std::optional<int> local_minute_of_day_from_unix_ms(std::int64_t unix_ms) {
    // Floor, not truncate -- same reasoning as `rfc3339_from_unix_ms` above.
    const std::int64_t secs = unix_ms >= 0 ? unix_ms / 1000 : (unix_ms - 999) / 1000;
    const std::time_t when = static_cast<std::time_t>(secs);

    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &when) != 0) return std::nullopt;
#else
    if (localtime_r(&when, &local) == nullptr) return std::nullopt;
#endif
    return local.tm_hour * 60 + local.tm_min;
}

}  // namespace snapback
