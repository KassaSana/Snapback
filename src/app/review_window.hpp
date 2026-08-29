#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "util/time.hpp"

namespace snapback {

// Roadmap 10.11. Maps the Review surface's shared range to the epoch-millisecond cutoff every
// bounded query understands. These presets are rolling windows ("the last 7 x 24 hours"), not
// calendar ranges — 7.16 (integer-ms time) made calendar semantics *possible*, and
// Storage::daily_summary is the one consumer that snaps its cutoff to a local midnight; the
// scalar queries deliberately stay rolling so "7d" means the same thing it always has.
//
// ADR-0007 moved this from RFC3339 text to milliseconds, which removes a conversion at each of
// the eight query call sites rather than adding one here. `since` is the exception and stays a
// string on the way in: it is the one value that arrives from outside -- the Review range the
// user picked, crossing the IPC boundary -- so it is parsed here, at the edge, exactly once.
//
// A `since` that will not parse is now an error instead of a silent whole-history query. That
// is a deliberate behaviour change: `std::optional` has no room to say "the caller asked for a
// window and I could not work out which", and answering with every row ever recorded is the
// least honest way to not know.
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
