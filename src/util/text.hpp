#pragma once

#include <string>
#include <string_view>

namespace snapback {

// The same trim was written three times -- once in command_dispatch.hpp for IPC strings, once
// in state.cpp for goal keywords and privacy exclusions, once in focus_window.cpp for app and
// window names -- and the copies had already drifted: two of them left form feeds and vertical
// tabs in place. They are whitespace in every context this app trims for, so the union wins;
// a name or keyword that is nothing but control whitespace now reads as empty everywhere
// rather than in two places out of three.
inline std::string trim(std::string_view s) {
    constexpr std::string_view kWhitespace = " \t\n\r\f\v";
    const auto begin = s.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos) return {};
    const auto end = s.find_last_not_of(kWhitespace);
    return std::string(s.substr(begin, end - begin + 1));
}

}  // namespace snapback
