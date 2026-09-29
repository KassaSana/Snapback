// Pure, webview-free core of the IPC bridge (testable without WebView2): input validation,
// limit clamping, and the arg-unwrap / serialize / error-envelope wrapper.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "types.hpp"
#include "util/text.hpp"

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
// A reflection is a sentence or two; capped because it renders into every session of the
// personal export.
constexpr std::size_t kMaxReflectionLen = 1000;

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

// The capability token every native command must present. The shim hands it to the page only
// after verifying its own document, before any page script runs, so a redirected page sees the
// bound functions but cannot use them. Stripped from args before the handler runs.
inline constexpr const char* kCapabilityTokenKey = "__snapbackToken";
// The one key of an error envelope; a reply is either a handler result or this.
inline constexpr const char* kErrorKey = "__snapback_error";

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

// Unwraps args, checks the token, runs the handler, and serializes the result or error
// envelope. An empty `expected_token` disables the check (tests); production always sets one.
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
        // dump_json here too: exception messages often quote OS strings, and a throwing dump
        // inside this catch would escape to the webview binding.
        return dump_json(nlohmann::json{{kErrorKey, e.what()}});
    }
}

// Runs a slow command off the caller's thread, one at a time per gate. `submit`/`resolve` are
// parameters so tests can pass plain lambdas. Each call resolves exactly once: busy (gate
// held), shutting down, or the handler's result. Gates are per command family.
//
// `on_claimed` runs on the calling thread after the gate is taken and before the job is queued,
// which orders per-run resets (e.g. clearing a cancel flag) between old and new requests.
inline void dispatch_single_flight(const std::function<bool(std::function<void()>)>& submit,
                                   std::function<void(std::string)> resolve,
                                   JsonHandler handler, std::string req,
                                   std::string expected_token,
                                   std::shared_ptr<std::atomic<bool>> active,
                                   std::string busy_message,
                                   const std::function<void()>& on_claimed = {}) {
    bool expected = false;
    if (!active->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        const JsonHandler busy = [message = std::move(busy_message)](const nlohmann::json&)
            -> nlohmann::json { throw std::runtime_error(message); };
        resolve(run_json_command(busy, req, expected_token));
        return;
    }
    if (on_claimed) on_claimed();

    const bool queued = submit([resolve, handler = std::move(handler), req, expected_token,
                                active] {
        const auto result = run_json_command(handler, req, expected_token);
        active->store(false, std::memory_order_release);
        resolve(result);
    });
    if (!queued) {
        active->store(false, std::memory_order_release);
        const JsonHandler stopping = [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("Snapback is shutting down");
        };
        resolve(run_json_command(stopping, req, expected_token));
    }
}

// The JavaScript expression that delivers one event: both values as escaped string literals,
// payload rebuilt with JSON.parse.
inline std::string event_dispatch_script(std::string_view event,
                                         std::string_view json_payload) {
    // ensure_ascii keeps U+2028/U+2029 out of the JS source; replace keeps an invalid byte from
    // throwing this late.
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
