// Tests the pure IPC-bridge contract (command_dispatch.hpp): argument unwrapping,
// result serialization, the {__snapback_error} envelope, and the ported validation
// helpers. These are the seams the frontend depends on, minus the webview transport.
#include <doctest/doctest.h>

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

TEST_CASE("validation helpers count Unicode scalars like Rust chars()") {
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
