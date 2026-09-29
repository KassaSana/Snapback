#pragma once

#include <string>
#include <string_view>

namespace snapback {

// Trims all ASCII whitespace, including form feed and vertical tab.
inline std::string trim(std::string_view s) {
    constexpr std::string_view kWhitespace = " \t\n\r\f\v";
    const auto begin = s.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos) return {};
    const auto end = s.find_last_not_of(kWhitespace);
    return std::string(s.substr(begin, end - begin + 1));
}

}  // namespace snapback
