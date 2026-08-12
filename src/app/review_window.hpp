#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace snapback {

// Roadmap 10.11. Maps the Review surface's shared range to the RFC3339 cutoff every bounded
// query already understands. Rolling windows until 7.16 lands calendar-day semantics.
inline std::optional<std::string> review_window_cutoff(const std::string& window,
                                                       const std::optional<std::string>& since,
                                                       const std::string& (*cutoff_days)(int)) {
    if (window == "all") return std::nullopt;
    if (window == "custom") {
        if (!since || since->empty()) {
            throw std::runtime_error("custom review window requires since");
        }
        return since;
    }
    if (window == "day" || window == "today") return cutoff_days(1);
    if (window == "week" || window == "7d") return cutoff_days(7);
    if (window == "30d") return cutoff_days(30);
    throw std::runtime_error("unknown review window");
}

}  // namespace snapback
