// Pure, webview-free core of the IPC command bridge. Split out from commands.hpp so it
// can be unit-tested without pulling in <webview/webview.h> (and thus WebView2 / Win32).
//
// This holds input validation, limit clamping, and the
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

#include "types.hpp"

namespace snapback::detail {

// Limits and validation for command input (blank goals rejected, history capped, etc.).
// Length uses Unicode scalar count, not byte length.
constexpr std::size_t kMaxHistoryLimit = 500;
constexpr std::size_t kMaxSessionGoalLen = 280;
constexpr std::size_t kMaxLabelNotesLen = 2000;
constexpr std::size_t kMaxSessionIdLen = 128;
constexpr std::size_t kMaxAppRulePatternLen = 200;
constexpr std::size_t kMaxAppRuleNoteLen = 500;
constexpr std::size_t kMaxRepoPathLen = 4096;
// Roadmap 2.14. A reflection is a sentence or two for tomorrow's restart, not a journal
// entry. Capped well below label notes because these render into the readable personal
// export for *every* session, where an unbounded paste would drown the file it belongs to.
constexpr std::size_t kMaxReflectionLen = 1000;

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
// JS shim turns into a rejected Promise.
// Roadmap 8.14. The key every native command must present.
//
// The shim gives it to the page's bridge only after checking that its own document is the
// trusted one, and it makes that check before the page has run a line — `webview.init()`
// scripts run first on every navigation. A page reached by a redirect therefore has the bound
// functions sitting on its global object and no way to use them, because it never saw this.
//
// It is deliberately stripped from the args before the handler runs. A command should not be
// able to read it, log it, or write it into an export by accident.
inline constexpr const char* kCapabilityTokenKey = "__snapbackToken";

// Constant-time-ish comparison. The token is 256 bits of CSPRNG output and an attacker gets no
// oracle to time against here, but a length-independent compare costs nothing and removes the
// question.
inline bool token_matches(const std::string& expected, const std::string& supplied) {
    if (expected.empty()) return false;
    if (expected.size() != supplied.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(expected[i] ^ supplied[i]);
    }
    return difference == 0;
}

// Unwraps args, checks the capability token, runs the handler, and serializes the result or
// the error envelope.
//
// An empty `expected_token` disables the check, which is what the pure command tests and any
// non-webview caller use. Production always supplies one.
inline std::string run_json_command(const JsonHandler& handler, const std::string& req,
                                    const std::string& expected_token = {}) {
    try {
        auto arr = nlohmann::json::parse(req);
        nlohmann::json args = (arr.is_array() && !arr.empty()) ? arr.at(0)
                                                               : nlohmann::json::object();
        if (!args.is_object()) args = nlohmann::json::object();

        if (!expected_token.empty()) {
            const auto supplied = args.value(kCapabilityTokenKey, std::string{});
            if (!token_matches(expected_token, supplied)) {
                // Deliberately uninformative. "wrong token" and "no token" are the same
                // answer, and neither says what the right one would look like.
                throw std::runtime_error(
                    "this page is not allowed to use Snapback's native commands");
            }
            args.erase(kCapabilityTokenKey);
        }

        // Responses carry OS-derived strings too (window titles in snapback and recap
        // payloads), so the same lossy dump applies -- a malformed title must degrade the
        // one field, not fail the whole command.
        return dump_json(handler(args));
    } catch (const std::exception& e) {
        return nlohmann::json{{"__snapback_error", e.what()}}.dump();
    }
}

// Builds the one JavaScript expression the host evaluates to deliver an event. Both values
// cross as escaped string literals, and the payload is reconstructed through JSON.parse
// inside the webview. `ensure_ascii=true` keeps U+2028/U+2029 out of JavaScript source even
// on engines that still treat those valid JSON characters as line terminators.
inline std::string event_dispatch_script(std::string_view event,
                                         std::string_view json_payload) {
    // ensure_ascii keeps U+2028/U+2029 out of the JavaScript source; replace keeps an
    // invalid byte that reached this far from throwing where there is no longer anywhere to
    // report it -- this runs on the way to the webview, past every handler.
    const auto js_event = nlohmann::json(std::string(event))
                              .dump(-1, ' ', /*ensure_ascii=*/true,
                                    nlohmann::json::error_handler_t::replace);
    const auto js_payload = nlohmann::json(std::string(json_payload))
                                .dump(-1, ' ', /*ensure_ascii=*/true,
                                      nlohmann::json::error_handler_t::replace);
    return "window.__snapback && window.__snapback.emit(" + js_event +
           ", JSON.parse(" + js_payload + "))";
}

}  // namespace snapback::detail
