#pragma once

namespace snapback::detail {

// Input captured while the foreground window differs from the context snapshot must be
// dropped. Guessing with stale context can bypass per-app privacy exclusions.
inline bool context_matches_foreground(const void* foreground, const void* captured_window,
                                       bool has_context) {
    return has_context && foreground != nullptr && foreground == captured_window;
}

}  // namespace snapback::detail
