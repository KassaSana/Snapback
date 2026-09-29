// Raise the previously focused window ("Take me back").
//   1. Never start a shell.
//   2. Refuse empty or nonsensical input with a clear result.
//   3. Non-throwing; OS focus-stealing restrictions are an ordinary result.
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
