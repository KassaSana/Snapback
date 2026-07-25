// Foreground app + window title, implemented by per-OS backends.
#pragma once

#include <optional>
#include <string>

namespace snapback {

struct ActiveWindow {
    std::string app_name;      // e.g. "Code.exe"
    std::string window_title;  // e.g. "types.hpp — snapbackCplusplus"
};

// Returns nullopt if permissions are missing or no window is focused.
std::optional<ActiveWindow> query_active_window();

}  // namespace snapback
