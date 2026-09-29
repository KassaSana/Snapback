#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "util/time.hpp"

namespace snapback {

// Maps the Review range to an epoch-ms cutoff. Presets are rolling windows, not calendar ranges
// (only Storage::daily_summary snaps to local midnight). `since` arrives as text from the UI
// and is parsed here once; an unparseable `since` is an error, not a whole-history query.
inline std::optional<std::int64_t> review_window_cutoff(
    const std::string& window, const std::optional<std::string>& since,
    std::int64_t (*cutoff_days)(int)) {
    if (window == "all") return std::nullopt;
    if (window == "custom") {
        if (!since || since->empty()) {
            throw std::runtime_error("custom review window requires since");
        }
        const auto parsed = unix_ms_from_rfc3339(*since);
        if (!parsed) throw std::runtime_error("custom review window since is not a timestamp");
        return parsed;
    }
    if (window == "day" || window == "today") return cutoff_days(1);
    if (window == "week" || window == "7d") return cutoff_days(7);
    if (window == "30d") return cutoff_days(30);
    throw std::runtime_error("unknown review window");
}

}  // namespace snapback
