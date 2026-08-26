// Tests the pure IPC-bridge contract (command_dispatch.hpp): argument unwrapping,
// result serialization, the {__snapback_error} envelope, and the ported validation
// helpers. These are the seams the frontend depends on, minus the webview transport.
#include "doctest_wrapper.hpp"

#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "app/command_dispatch.hpp"
#include "app/state.hpp"

using namespace snapback;
using nlohmann::json;

namespace {

std::unique_ptr<AppState> make_state() {
    auto storage = Storage::open_memory();
    if (!storage) throw std::runtime_error("failed to open in-memory storage");
    return std::make_unique<AppState>(std::move(*storage));
}

}  // namespace

TEST_CASE("run_json_command unwraps the [args] array and dumps the handler result") {
    // webview delivers arguments as a JSON array; the handler sees element [0].
    auto out = detail::run_json_command(
        [](const json& a) { return json{{"echoed", a.at("limit").get<int>()}}; },
        R"([{"limit":5}])");
    CHECK(json::parse(out).at("echoed") == 5);
}

TEST_CASE("run_json_command tolerates empty/absent args as an empty object") {
    auto empty_array = detail::run_json_command(
        [](const json& a) { return json{{"isObject", a.is_object()}, {"size", a.size()}}; },
        "[]");
    auto parsed = json::parse(empty_array);
    CHECK(parsed.at("isObject") == true);
    CHECK(parsed.at("size") == 0);
}

TEST_CASE("run_json_command converts a thrown exception into the error envelope") {
    auto out = detail::run_json_command(
        [](const json&) -> json { throw std::runtime_error("boom"); }, "[{}]");
    auto parsed = json::parse(out);
    REQUIRE(parsed.contains("__snapback_error"));
    CHECK(parsed.at("__snapback_error") == "boom");
}

TEST_CASE("run_json_command keeps the error envelope serializable on invalid UTF-8") {
    // The escape this closes. The success path one line above uses `dump_json` (lossy,
    // error_handler_t::replace) precisely because responses carry OS-derived strings; the
    // error envelope was still using strict `.dump()`, which throws type_error.316 on the
    // first invalid byte -- *from inside the catch block*, so it escapes `run_json_command`
    // entirely and lands in the webview binding. That is the exact failure mode the lossy
    // dump was introduced to remove, surviving in the one path that runs when something has
    // already gone wrong.
    //
    // Not hypothetical: exception messages here concatenate filesystem paths and OS strings,
    // and `nlohmann::json::parse` failures quote the offending input back verbatim.
    const std::string invalid_utf8 = "bad path: \xff\xfe";
    std::string out;
    CHECK_NOTHROW(out = detail::run_json_command(
                      [&](const json&) -> json { throw std::runtime_error(invalid_utf8); },
                      "[{}]"));

    // Degraded, not dropped: the envelope still parses and still carries the readable part of
    // the message, so the user is told what failed instead of nothing reaching them at all.
    auto parsed = json::parse(out);
    REQUIRE(parsed.contains("__snapback_error"));
    const auto message = parsed.at("__snapback_error").get<std::string>();
    CHECK(message.rfind("bad path: ", 0) == 0);
    CHECK(message.find("\xef\xbf\xbd") != std::string::npos);  // U+FFFD replaced the bad bytes
}

TEST_CASE("run_json_command reports malformed request JSON without throwing") {
    // The other reachable path to the same escape: nlohmann's parse error message quotes the
    // raw token it choked on, so invalid bytes in `req` reach `e.what()` directly.
    std::string out;
    CHECK_NOTHROW(out = detail::run_json_command(
                      [](const json&) -> json { return json::object(); }, "[{\"a\": \xff}]"));
    CHECK_NOTHROW(json::parse(out).at("__snapback_error"));
}

TEST_CASE("run_json_command requires the capability token when one is configured") {
    const std::string token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    detail::JsonHandler handler = [](const json& a) { return json{{"ok", a.size()}}; };

    const auto authorized = detail::run_json_command(
        handler,
        R"([{"__snapbackToken":")" + token + R"(","limit":3}])",
        token);
    CHECK(json::parse(authorized).at("ok") == 1);

    const auto missing = detail::run_json_command(handler, R"([{"limit":3}])", token);
    CHECK(json::parse(missing).at("__snapback_error") ==
          "this page is not allowed to use Snapback's native commands");

    const auto wrong = detail::run_json_command(
        handler,
        R"([{"__snapbackToken":"wrong","limit":3}])",
        token);
    CHECK(json::parse(wrong).at("__snapback_error") ==
          "this page is not allowed to use Snapback's native commands");
}

TEST_CASE("run_json_command strips the capability token before the handler runs") {
    const std::string token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    detail::JsonHandler handler = [](const json& a) {
        CHECK_FALSE(a.contains(detail::kCapabilityTokenKey));
        return json(nullptr);
    };
    CHECK_NOTHROW(detail::run_json_command(
        handler,
        R"([{"__snapbackToken":")" + token + R"("}])",
        token));
}

TEST_CASE("token_matches rejects length mismatches without scanning") {
    CHECK(detail::token_matches("abcd", "abcd"));
    CHECK_FALSE(detail::token_matches("abcd", "abc"));
    CHECK_FALSE(detail::token_matches("", "abc"));
}

TEST_CASE("event_dispatch_script crosses an escaped JSON parse boundary") {
    const std::string line_separator = "\xE2\x80\xA8";
    const std::string payload =
        std::string(R"({"title":"before)") + line_separator + R"(after","quote":"\""})";

    const std::string event = "predic\"tion";
    const auto script = detail::event_dispatch_script(event, payload);
    const std::string prefix = "window.__snapback && window.__snapback.emit(";
    const std::string separator = ", JSON.parse(";
    REQUIRE(script.starts_with(prefix));
    REQUIRE(script.ends_with("))"));
    const auto separator_at = script.find(separator, prefix.size());
    REQUIRE(separator_at != std::string::npos);

    // The generated JavaScript literals use JSON's string grammar, so decoding each one
    // with the same JSON parser proves the exact event and payload make a round trip. Exact
    // prefix/suffix checks also reject appended executable source.
    const auto event_literal =
        script.substr(prefix.size(), separator_at - prefix.size());
    const auto payload_begin = separator_at + separator.size();
    const auto payload_literal =
        script.substr(payload_begin, script.size() - payload_begin - 2);
    CHECK(json::parse(event_literal).get<std::string>() == event);
    const auto decoded_payload = json::parse(payload_literal).get<std::string>();
    CHECK(json::parse(decoded_payload) == json::parse(payload));
    CHECK(script.find(line_separator) == std::string::npos);
    CHECK(script.find("\\u2028") != std::string::npos);
}

TEST_CASE("validation helpers trim, reject blanks, and cap length") {
    CHECK(detail::validate_required_text("Goal", "  ship it  ", 280) == "ship it");
    CHECK_THROWS_AS(detail::validate_required_text("Goal", "   ", 280), std::runtime_error);
    CHECK_THROWS_AS(detail::validate_required_text("Goal", std::string(281, 'a'), 280),
                    std::runtime_error);

    CHECK(detail::validate_optional_text("Notes", std::nullopt, 100) == std::nullopt);
    CHECK(detail::validate_optional_text("Notes", std::optional<std::string>("  "), 100) ==
          std::nullopt);
    CHECK(detail::validate_optional_text("Notes", std::optional<std::string>(" hi "), 100) ==
          std::optional<std::string>("hi"));
}

TEST_CASE("validation helpers count Unicode scalars") {
    CHECK(detail::utf8_scalar_count("hello") == 5);
    CHECK(detail::utf8_scalar_count("éé") == 2);
    const std::string two_scalars = "éé";
    CHECK(two_scalars.size() == 4);
    CHECK_NOTHROW(detail::validate_required_text("Goal", two_scalars, 2));
    CHECK_THROWS_AS(detail::validate_required_text("Goal", two_scalars + "é", 2),
                    std::runtime_error);
}

TEST_CASE("clamp_limit applies default and caps at the history limit") {
    CHECK(detail::clamp_limit(json::object(), 8) == 8);
    CHECK(detail::clamp_limit(json{{"limit", 20}}, 8) == 20);
    CHECK(detail::clamp_limit(json{{"limit", 10000}}, 8) == detail::kMaxHistoryLimit);
    CHECK(detail::clamp_limit(json{{"limit", nullptr}}, 8) == 8);
}

TEST_CASE("a start_session handler round-trips through the bridge with camelCase keys") {
    auto state = make_state();
    // Mirror the real bind: validate the goal, parse focusMode, return the record.
    detail::JsonHandler start_session = [&](const json& a) {
        auto goal = detail::validate_required_text("Session goal",
                                                   a.at("goal").get<std::string>(),
                                                   detail::kMaxSessionGoalLen);
        auto mode = focus_mode_from_string(a.value("focusMode", std::string("normal")));
        return json(state->start_session(goal, mode));
    };

    auto out = detail::run_json_command(start_session, R"([{"goal":"  ship phase six  ","focusMode":"deep"}])");
    auto rec = json::parse(out);
    CHECK(rec.at("goal") == "ship phase six");   // trimmed
    CHECK(rec.at("focusMode") == "deep");        // camelCase wire key + parsed enum
    CHECK(rec.at("status") == "ACTIVE");
    CHECK(rec.contains("sessionId"));

    // A blank goal comes back as the error envelope, not a record.
    auto err = detail::run_json_command(start_session, R"([{"goal":"   "}])");
    CHECK(json::parse(err).at("__snapback_error") == "Session goal is required.");
}

TEST_CASE("Pomodoro handlers return the stable status envelope through the bridge") {
    auto state = make_state();
    state->start_session("Ship the timer", FocusMode::Normal);

    detail::JsonHandler start = [&](const json&) { return json(state->start_pomodoro()); };
    detail::JsonHandler stop = [&](const json&) { return json(state->stop_pomodoro()); };

    const auto started = json::parse(detail::run_json_command(start, "[{}]"));
    CHECK(started.at("running") == true);
    CHECK(started.at("phase") == "work");
    CHECK(started.at("completedWorkIntervals") == 0);
    CHECK(started.at("remainingMs").get<std::int64_t>() > 0);

    const auto stopped = json::parse(detail::run_json_command(stop, "[{}]"));
    CHECK(stopped.at("running") == false);
    CHECK(stopped.at("remainingMs") == 0);
}

TEST_CASE("a reflection answer is trimmed, blank-rejected, and length-capped") {
    // Roadmap 2.14. The command runs both answers through validate_optional_text, so Skip, a
    // whitespace-only submission, and clearing an answer all collapse to the same nullopt the
    // schema stores as NULL -- an empty string would be a fourth, wrong state.
    CHECK_FALSE(detail::validate_optional_text("What got done", std::string("   \t \n "),
                                               detail::kMaxReflectionLen)
                    .has_value());
    CHECK_FALSE(
        detail::validate_optional_text("What got done", std::nullopt, detail::kMaxReflectionLen)
            .has_value());
    CHECK(detail::validate_optional_text("Next step", std::string("  ship it  "),
                                         detail::kMaxReflectionLen) == "ship it");

    const std::string at_limit(detail::kMaxReflectionLen, 'x');
    CHECK(detail::validate_optional_text("Next step", at_limit, detail::kMaxReflectionLen) ==
          at_limit);
    CHECK_THROWS_AS(detail::validate_optional_text("Next step", at_limit + "x",
                                                   detail::kMaxReflectionLen),
                    std::runtime_error);
}
