// Window activation helper for context recovery. Roadmap 2.8 ("Take me back").
//
// When a Snapback fires, the app knows the previous focused context (app_name and
// window_title). This interface raises that window to the foreground on the user's
// request.
//
// Like reveal_path.hpp, this follows strict rules:
//   1. Never start a shell.
//   2. Refuse empty or nonsensical input with an honest result rather than guessing.
//   3. Window activation is non-throwing; OS-level focus-stealing restrictions are
//      treated as an ordinary result.
#pragma once

#include <string>

#include "types.hpp"

namespace snapback {


// Check whether window activation is supported on the current platform build.
bool focus_window_supported();

// Raise the target application or window to the foreground.
// Matching prioritizes window_title when non-empty, falling back to app_name.
FocusTargetResult focus_window(const std::string& app_name, const std::string& window_title);

namespace detail {

// Platform-specific activation implementation.
FocusTargetResult focus_window_native(const std::string& app_name,
                                      const std::string& window_title);

}  // namespace detail

}  // namespace snapback
