#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "types.hpp"

namespace snapback::detail {

// Input captured while the foreground window differs from the context snapshot must be
// dropped. Guessing with stale context can bypass per-app privacy exclusions.
inline bool context_matches_foreground(const void* foreground, const void* captured_window,
                                       bool has_context) {
    return has_context && foreground != nullptr && foreground == captured_window;
}

// Convert a mouse translation into the uint32_t speed stored on CaptureEvent. The hook's
// monotonic event timestamp is millisecond-resolution on Windows, so a pair of events can
// share a timestamp even though the callback ran over a real, shorter interval. Keep that
// interval bounded away from zero, widen coordinates before subtracting them, and validate the
// floating-point result before narrowing it to the event representation.
inline std::uint32_t mouse_speed_for_points(std::int32_t current_x, std::int32_t current_y,
                                             std::int32_t previous_x, std::int32_t previous_y,
                                             double elapsed_secs) {
    constexpr double kMinimumElapsedSecs = 1e-6;
    constexpr double kMaxSpeed =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());

    const double safe_elapsed_secs =
        std::isfinite(elapsed_secs) && elapsed_secs > kMinimumElapsedSecs
            ? elapsed_secs
            : kMinimumElapsedSecs;
    const double dx = static_cast<double>(current_x) - static_cast<double>(previous_x);
    const double dy = static_cast<double>(current_y) - static_cast<double>(previous_y);
    const double speed = std::hypot(dx, dy) / safe_elapsed_secs;
    if (!std::isfinite(speed) || speed >= kMaxSpeed) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(speed);
}

#if defined(_WIN32)
// Maps a WH_MOUSE_LL message id to the capture event it stands for, or nullopt for a
// message that produces no event. Only button *presses* are clicks: WM_*BUTTONUP used to fall
// into the click bucket too, so every physical click counted twice, and so did each wheel
// notch. The set emitted here matches the macOS tap mask (button-down, move, drag) so the
// mouse_click_count and mouse_speed features mean the same thing on both platforms.
//
// Wheel and button-release are dropped rather than recorded as a move: a MouseMove with no
// displacement would feed a zero into the speed mean/std the extractor computes over moves.
// The cost is that wheel-only scrolling does not count as input for idle detection, which is
// already the case on macOS; a dedicated scroll event is the fix for both, not a mislabel.
// Takes the raw WPARAM as an integer and is defined in input_hook_windows.cpp so this header
// stays free of <windows.h> and its macros.
std::optional<EventType> classify_mouse_message(unsigned message);
#endif

}  // namespace snapback::detail
