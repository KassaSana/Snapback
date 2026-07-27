// Foreground app + window title, implemented by per-OS backends.
#pragma once

#include <optional>
#include <string>

namespace snapback {

struct ActiveWindow {
    std::string app_name;      // e.g. "Code.exe"
    std::string window_title;  // e.g. "types.hpp — snapbackCplusplus"
};

#if defined(_WIN32)
namespace detail {
// Exposed for a Windows regression test because an off-by-one here writes the
// UTF-8 terminator beyond std::string's size.
std::string utf8_from_wide(const wchar_t* value);
}  // namespace detail
#endif

// Returns nullopt if permissions are missing or no window is focused.
std::optional<ActiveWindow> query_active_window();

}  // namespace snapback
