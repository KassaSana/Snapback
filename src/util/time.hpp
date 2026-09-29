#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace snapback {

// Longest gap between consecutive predictions that still counts as one unbroken stretch.
// Predictions stop entirely while idle or private, so a longer gap ends the run and counts as
// nothing. Well above the ~1/s throttle, well below the 5-minute idle default.
inline constexpr std::int64_t kFocusRunGapSecs = 120;

// Whether a string is exactly `YYYY-MM-DDTHH:MM:SSZ`. Checked explicitly because std::get_time
// differs across toolchains (MSVC accepts a date-only value as midnight), and imported files
// can carry foreign values.
inline bool has_rfc3339_utc_shape(const std::string& timestamp) {
    if (timestamp.size() != 20) return false;
    // '\0' marks a digit position; anything else is a required literal. NUL rather than -1
    // because plain char is unsigned on some platforms.
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

// Epoch seconds from an RFC3339 UTC timestamp, or nullopt.
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

// ADR-0007 edge conversions: time is UTC epoch ms; RFC3339 exists only for display and external
// input, never storage or comparison. Output is whole-second RFC3339.
inline std::string rfc3339_from_unix_ms(std::int64_t unix_ms) {
    // Floor, not truncate, for pre-epoch values.
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

// The inverse, for text input (imports, fixtures). nullopt rather than a sentinel.
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

// Local minutes since midnight for a UTC instant, or nullopt if conversion fails. Recomputed on
// every check rather than cached as an instant, so a DST change or travel cannot shift quiet
// hours. Consequence: on fall-back day 01:00-02:00 is quiet twice, and a range inside the
// skipped spring-forward hour never fires. Windows' localtime_s rejects pre-epoch instants
// (nullopt there); callers fail open.
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
