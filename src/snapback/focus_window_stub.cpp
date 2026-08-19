#include "snapback/focus_window.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)

namespace snapback {

bool focus_window_supported() {
    return false;
}

namespace detail {

FocusTargetResult focus_window_native(const std::string& /*app_name*/,
                                      const std::string& /*window_title*/) {
    return FocusTargetResult{false, "Window activation is not supported on this platform"};
}

}  // namespace detail
}  // namespace snapback

#endif  // !_WIN32 && !__APPLE__
