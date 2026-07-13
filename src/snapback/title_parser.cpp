#include "snapback/title_parser.hpp"

#include <array>

namespace snapback {

// Editors separate segments with " — " (em dash, VS Code) or " - " (hyphen).
// Split on whichever appears and treat the first segment as the file, the second
// as the project. This is a faithful port sketch; extend to match title_parser.rs.
TitleHints parse_title(const std::string& window_title) {
    TitleHints hints;
    static const std::array<std::string, 2> seps = {" \xE2\x80\x94 ", " - "};  // " — ", " - "

    for (const auto& sep : seps) {
        const auto first = window_title.find(sep);
        if (first == std::string::npos) continue;

        hints.file_hint = window_title.substr(0, first);
        const auto rest_start = first + sep.size();
        const auto second = window_title.find(sep, rest_start);
        hints.project_hint = window_title.substr(
            rest_start, second == std::string::npos ? std::string::npos : second - rest_start);
        break;
    }
    if (hints.file_hint.empty()) hints.file_hint = window_title;  // no separator
    return hints;
}

}  // namespace snapback
