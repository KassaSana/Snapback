#pragma once

#include <optional>

#include "types.hpp"

namespace snapback::detail {

// Input captured while the foreground window differs from the context snapshot must be
// dropped. Guessing with stale context can bypass per-app privacy exclusions.
inline bool context_matches_foreground(const void* foreground, const void* captured_window,
                                       bool has_context) {
    return has_context && foreground != nullptr && foreground == captured_window;
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
