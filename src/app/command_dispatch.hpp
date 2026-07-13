// Pure, webview-free core of the IPC command bridge. Split out from commands.hpp so it
// can be unit-tested without pulling in <webview/webview.h> (and thus WebView2 / Win32).
//
// This holds: input validation ported from Rust commands.rs, limit clamping, and the
// arg-unwrap + serialize + error-envelope wrapper that every bound command runs through.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace snapback::detail {

// Limits + validation ported from commands.rs (blank goal rejected, history capped, etc.).
// Length uses Unicode scalar count like Rust's `.chars().count()`, not byte length.
constexpr std::size_t kMaxHistoryLimit = 500;
constexpr std::size_t kMaxSessionGoalLen = 280;
constexpr std::size_t kMaxLabelNotesLen = 2000;
constexpr std::size_t kMaxSessionIdLen = 128;
constexpr std::size_t kMaxAppRulePatternLen = 200;
constexpr std::size_t kMaxAppRuleNoteLen = 500;
constexpr std::size_t kMaxRepoPathLen = 4096;

inline std::string trim(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

inline std::size_t utf8_scalar_count(std::string_view s) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0x80) == 0) {
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
        } else {
            i += 1;
        }
        ++count;
    }
    return count;
}

inline std::string validate_required_text(const char* name, const std::string& value,
                                          std::size_t max_len) {
    std::string t = trim(value);
    if (t.empty()) throw std::runtime_error(std::string(name) + " is required.");
    if (utf8_scalar_count(t) > max_len) {
        throw std::runtime_error(std::string(name) + " must be at most " +
                                 std::to_string(max_len) + " characters.");
    }
    return t;
}

inline std::optional<std::string> validate_optional_text(const char* name,
                                                         std::optional<std::string> value,
                                                         std::size_t max_len) {
    if (!value) return std::nullopt;
    std::string t = trim(*value);
    if (t.empty()) return std::nullopt;
    if (utf8_scalar_count(t) > max_len) {
        throw std::runtime_error(std::string(name) + " must be at most " +
                                 std::to_string(max_len) + " characters.");
    }
    return t;
}

inline std::size_t clamp_limit(const nlohmann::json& args, std::size_t def) {
    std::size_t limit = def;
    if (args.contains("limit") && !args.at("limit").is_null()) {
        limit = args.at("limit").get<std::size_t>();
    }
    // Parenthesized to dodge the windows.h min() macro (pulled in via webview/WebView2).
    return (std::min)(limit, kMaxHistoryLimit);
}

inline std::optional<std::string> opt_string(const nlohmann::json& args, const char* key) {
    if (args.contains(key) && !args.at(key).is_null()) {
        return args.at(key).get<std::string>();
    }
    return std::nullopt;
}

using JsonHandler = std::function<nlohmann::json(const nlohmann::json&)>;

// The contract every command runs through: `req` is the JSON *array* string webview.bind
// delivers; we take element [0] (the args object the shim forwarded), run the handler,
// and dump the result. Any thrown exception becomes the {__snapback_error} envelope the
// JS shim turns into a rejected Promise — reproducing Rust's Result::Err.
inline std::string run_json_command(const JsonHandler& handler, const std::string& req) {
    try {
        auto arr = nlohmann::json::parse(req);
        nlohmann::json args = (arr.is_array() && !arr.empty()) ? arr.at(0)
                                                               : nlohmann::json::object();
        if (!args.is_object()) args = nlohmann::json::object();
        return handler(args).dump();
    } catch (const std::exception& e) {
        return nlohmann::json{{"__snapback_error", e.what()}}.dump();
    }
}

}  // namespace snapback::detail
