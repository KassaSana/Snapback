// Focus-mode guardrails, incl. hyperfocus detection. Rust: engine/focus_modes.rs.
// Thresholds themselves live on FocusMode in types.hpp (ported verbatim).
#pragma once

#include <cstdint>

#include "types.hpp"

namespace snapback {

// Rust: evaluate_hyperfocus — true once a session has run past the mode's
// hyperfocus_minutes without a break, so the UI can nudge the user to pause.
inline bool evaluate_hyperfocus(FocusMode mode, std::uint64_t continuous_minutes) {
    return continuous_minutes >= hyperfocus_minutes(mode);
}

}  // namespace snapback
