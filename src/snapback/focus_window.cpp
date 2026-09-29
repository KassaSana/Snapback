#include "snapback/focus_window.hpp"

#include <string>

#include "util/text.hpp"

// Input validation (rule 2 in focus_window.hpp) lives here once, so platforms agree. Platform
// files implement only focus_window_supported() and detail::focus_window_native(), which
// receive trimmed, non-empty input.

namespace snapback {

FocusTargetResult focus_window(const std::string& app_name, const std::string& window_title) {
    const std::string clean_app = trim(app_name);
    const std::string clean_title = trim(window_title);

    if (clean_app.empty() && clean_title.empty()) {
        return FocusTargetResult{false, "No target application or window specified"};
    }

    return detail::focus_window_native(clean_app, clean_title);
}

}  // namespace snapback
