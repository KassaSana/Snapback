#pragma once

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

// Epoch seconds from an RFC3339 UTC timestamp, or nullopt if it will not parse. Used to turn
// prediction timestamps into durations; `local_hour_from_rfc3339` below answers a different
// question (which local hour) and deliberately keeps its own conversion.
inline std::optional<std::int64_t> epoch_secs_from_rfc3339(const std::string& timestamp) {
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

inline int local_hour_from_rfc3339(const std::string& timestamp) {
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

}  // namespace snapback
