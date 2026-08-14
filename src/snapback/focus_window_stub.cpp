#include "snapback/focus_window.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)

namespace snapback {

bool focus_window_supported() {
    return false;
}

FocusTargetResult focus_window(const std::string& app_name, const std::string& window_title) {
    if (app_name.empty() && window_title.empty()) {
        return FocusTargetResult{false, "No target application or window specified"};
    }
    return detail::focus_window_native(app_name, window_title);
}

namespace detail {

FocusTargetResult focus_window_native(const std::string& /*app_name*/,
                                      const std::string& /*window_title*/) {
    return FocusTargetResult{false, "Window activation is not supported on this platform"};
}

}  // namespace detail
}  // namespace snapback

#endif  // !_WIN32 && !__APPLE__
