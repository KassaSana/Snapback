#include "snapback/focus_window.hpp"

#include <string>

#include "util/text.hpp"

// Rule 2 in focus_window.hpp -- "refuse empty or nonsensical input with an honest result"
// -- is a property of the *interface*, not of any one window system. It used to be
// implemented three times, once per platform file, and the three copies disagreed: Windows
// trimmed before checking, macOS and the stub did not. A caller passing "   " therefore got
// a refusal on Windows and a search for a whitespace-named window everywhere else.
//
// So validation lives here, once. Platform files implement only what is genuinely
// platform-specific: focus_window_supported() and detail::focus_window_native(), which is
// reached only with already-trimmed, already-non-empty input.

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
