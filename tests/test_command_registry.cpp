// The command registry (Roadmap 14.3): the real handler table, built against a real
// AppState, invoked by name through the same envelope the webview bridge uses. This is the
// seam the bridge tests could not reach -- they re-created handler lambdas by hand, so a
// wrongly wired real command passed them.
#include "doctest_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/async_command_runner.hpp"
#include "app/command_handlers.hpp"
#include "app/command_registry.hpp"
#include "app/state.hpp"

using namespace snapback;
using nlohmann::json;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_registry_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

struct App {
    TempDir data_dir;
    std::unique_ptr<AppState> state;
    detail::AsyncCommandRunner runner;
    CommandRegistry registry;
    int overlay_dismissals = 0;
    json acceptance_verdict;
    bool acceptance_enabled = false;

    App() {
        auto storage = Storage::open_memory();
        if (!storage) throw std::runtime_error("failed to open in-memory storage");
        state = std::make_unique<AppState>(std::move(*storage));
        NativeUiHooks ui{[this] { ++overlay_dismissals; }, {}};
        if (acceptance_enabled) {
            ui.report_acceptance_verdict =
                [this](const json& verdict) { acceptance_verdict = verdict; };
        }
        register_command_handlers(registry, *state, data_dir.path, runner, std::move(ui));
    }
    ~App() { runner.shutdown(); }
};

std::set<std::string> keys(const json& value) {
    std::set<std::string> result;
    for (auto it = value.begin(); it != value.end(); ++it) result.insert(it.key());
    return result;
}

void check_keys(const json& value, std::initializer_list<const char*> expected) {
    REQUIRE(value.is_object());
    const std::set<std::string> wanted(expected.begin(), expected.end());
    CHECK(keys(value) == wanted);
}

}  // namespace

TEST_CASE("CommandRegistry refuses a name registered twice") {
    CommandRegistry registry;
    registry.add("ping", [](const json&) { return json(1); });
    CHECK_THROWS_AS(registry.add("ping", [](const json&) { return json(2); }), std::logic_error);
    CHECK_THROWS_AS(registry.add_async("ping", [](const json&) { return json(2); },
                                       std::make_shared<std::atomic<bool>>(false), "busy"),
                    std::logic_error);
    CHECK(registry.size() == 1);
}

TEST_CASE("CommandRegistry::invoke frames a call exactly as the bridge does") {
    CommandRegistry registry;
    registry.add("echo", [](const json& a) { return json{{"echo", a.value("x", 0)}}; });

    // The request is the JSON array the shim sends; the reply is what it receives.
    CHECK(json::parse(registry.invoke("echo", R"([{"x": 7}])")) == json{{"echo", 7}});
    // An unknown name is an error envelope, not an exception: that is what a stale
    // frontend call sees.
    const auto unknown = json::parse(registry.invoke("nope", "[{}]"));
    CHECK(unknown.contains(detail::kErrorKey));
    CHECK(unknown.at(detail::kErrorKey).get<std::string>().find("nope") != std::string::npos);
    // The token check applies here too; the seam must not be a way around Roadmap 8.14.
    const auto refused = json::parse(registry.invoke("echo", R"([{"x": 7}])", "secret"));
    CHECK(refused.contains(detail::kErrorKey));
    CHECK(json::parse(registry.invoke("echo", R"([{"x": 7, "__snapbackToken": "secret"}])",
                                      "secret")) == json{{"echo", 7}});
}

TEST_CASE("CommandRegistry::call unwraps the reply and throws the envelope's message") {
    CommandRegistry registry;
    registry.add("fail", [](const json&) -> json { throw std::runtime_error("no such thing"); });
    registry.add("ok", [](const json& a) { return json{{"n", a.value("n", 0) + 1}}; });
    CHECK(registry.call("ok", {{"n", 41}}) == json{{"n", 42}});
    CHECK_THROWS_WITH(registry.call("fail"), "no such thing");
}

TEST_CASE("the real handler table answers get_health with the health shape") {
    App app;
    const auto health = app.registry.call("get_health");
    CHECK(health.contains("status"));
    CHECK(health.contains("classifier"));
    CHECK(health.contains("permissions"));
}

TEST_CASE("acceptance verdict reporting is disabled unless the native smoke opted in") {
    App app;
    CHECK_THROWS_WITH(app.registry.call("report_acceptance_verdict",
                                        {{"verdict", {{"passed", true}}}}),
                      "desktop acceptance harness is disabled in this build");
}

TEST_CASE("acceptance verdict reporting reaches the injected native sink") {
    App app;
    // Rebuild the table with the hook enabled; ordinary production construction above keeps
    // it absent, which is the boundary this case is pinning.
    app.registry = CommandRegistry{};
    app.acceptance_enabled = true;
    NativeUiHooks ui{[&app] { ++app.overlay_dismissals; },
                     [&app](const json& verdict) { app.acceptance_verdict = verdict; }};
    register_command_handlers(app.registry, *app.state, app.data_dir.path, app.runner,
                              std::move(ui));

    const auto reply = app.registry.call("report_acceptance_verdict",
                                         {{"verdict", {{"passed", true}, {"checks", 5}}}});
    CHECK(reply.at("accepted") == true);
    CHECK(app.acceptance_verdict.at("passed") == true);
    CHECK(app.acceptance_verdict.at("checks") == 5);
}

TEST_CASE("the real handler table runs a session through start, get, and stop") {
    App app;
    const auto started = app.registry.call("start_session", {{"goal", "Write the ADR"}});
    const auto id = started.at("sessionId").get<std::string>();
    CHECK_FALSE(id.empty());

    const auto active = app.registry.call("get_active_session");
    CHECK(active.at("sessionId") == id);

    app.registry.call("stop_session", {{"sessionId", id}});
    CHECK(app.registry.call("get_active_session").is_null());
}

TEST_CASE("real command results keep the frontend's camelCase contract") {
    App app;
    check_keys(app.registry.call("get_recording_status"),
               {"state", "privatePauseRemainingMs", "alertSnoozeRemainingMs"});

    const auto settings = app.registry.call("get_settings");
    CHECK(settings.contains("defaultFocusMode"));
    CHECK(settings.contains("idleThresholdSecs"));
    check_keys(settings.at("pomodoro"),
               {"workMs", "shortBreakMs", "longBreakMs", "intervalsBeforeLongBreak",
                "autoStartNextPhase"});

    check_keys(app.registry.call("get_analytics"),
               {"sampleCount", "avgFocusScore", "productiveSessionStreak", "hourly",
                "topApps"});
    const auto report = app.registry.call("get_summary_report");
    CHECK(report.at("window") == "day");  // absent argument uses the Review default
    check_keys(report,
               {"window", "generatedAtMs", "sessionCount", "completedSessionCount",
                "focusSeconds", "sessionLimit", "sessionsTruncated", "sampleCount",
                "avgFocusScore", "distractedFraction", "longestFocusSecs", "topContextApp",
                "attendedSeconds", "plannedMins"});
    CHECK(app.registry.call("get_daily_summary").at("window") == "7d");

    const auto started = app.registry.call("start_session", {{"goal", "Contract check"}});
    const auto id = started.at("sessionId").get<std::string>();
    check_keys(started, {"sessionId", "goal", "status", "focusMode", "startedAtMs",
                         "endedAtMs", "reflectionDone", "reflectionNextStep"});
    CHECK(started.at("focusMode") == "normal");  // absent argument uses the native default
    const auto stopped = app.registry.call("stop_session", {{"sessionId", id}});
    CHECK(stopped.at("status") == "COMPLETED");
    const auto history = app.registry.call("get_session_history");
    REQUIRE(history.is_array());
    REQUIRE_FALSE(history.empty());
    check_keys(history.at(0), {"record", "recap"});
    check_keys(history.at(0).at("recap"),
               {"sessionId", "goal", "durationSecs", "activeSecs", "avgFocusScore",
                "avgDistractionRisk", "snapbackCount", "thrashSpikes", "deepFocusPct"});
}

TEST_CASE("dismiss_snapback dismisses the native card through the injected hook") {
    App app;
    CHECK(app.overlay_dismissals == 0);
    app.registry.call("dismiss_snapback");
    CHECK(app.overlay_dismissals == 1);
    // A restore with nothing to restore still clears the card, as the overlay path relies on.
    app.registry.call("restore_snapback_target");
    CHECK(app.overlay_dismissals == 2);
}

TEST_CASE("the slow commands are registered with a worker policy and the fast ones are not") {
    App app;
    for (const char* slow : {"export_training_data", "export_my_data", "train_from_export",
                             "export_summary_report", "export_support_bundle",
                             "stage_data_import"}) {
        CAPTURE(slow);
        const auto* command = app.registry.find(slow);
        REQUIRE(command != nullptr);
        REQUIRE(command->async.has_value());
        CHECK(command->async->gate != nullptr);
        CHECK_FALSE(command->async->busy_message.empty());
    }
    for (const char* fast : {"get_health", "start_session", "pick_open_file",
                             "reload_classifier_model", "cancel_training"}) {
        CAPTURE(fast);
        const auto* command = app.registry.find(fast);
        REQUIRE(command != nullptr);
        CHECK_FALSE(command->async.has_value());
    }
    // The training run and the training export must not share a gate: one running must not
    // report the other as busy (see dispatch_single_flight).
    CHECK(app.registry.find("train_from_export")->async->gate !=
          app.registry.find("export_training_data")->async->gate);
}

TEST_CASE("personal export reserves deletion exclusion before it reaches the worker") {
    App app;
    // Deletion remains available before an export is accepted.
    (void)app.registry.call("delete_all_activity_data");

    const auto* export_command = app.registry.find("export_my_data");
    REQUIRE(export_command != nullptr);
    REQUIRE(export_command->async.has_value());

    std::promise<void> entered;
    std::promise<void> release;
    auto release_future = release.get_future().share();
    REQUIRE(app.runner.submit([&] {
        entered.set_value();
        release_future.wait();
    }));
    REQUIRE(entered.get_future().wait_for(std::chrono::seconds(1)) ==
            std::future_status::ready);

    std::promise<std::string> resolved;
    auto resolved_future = resolved.get_future();
    const auto& policy = *export_command->async;
    detail::dispatch_single_flight(
        [&](std::function<void()> job) { return app.runner.submit(std::move(job)); },
        [&](std::string result) { resolved.set_value(std::move(result)); },
        export_command->handler, "[{}]", "", policy.gate, policy.busy_message,
        policy.on_claimed);

    CHECK_THROWS_WITH(
        app.registry.call("delete_all_activity_data"),
        "personal data export is in progress; wait for it to finish before deleting activity");

    release.set_value();
    REQUIRE(resolved_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    const auto reply = json::parse(resolved_future.get());
    CHECK_FALSE(reply.contains(detail::kErrorKey));
    CHECK(reply.at("sessionCount") == 0);
    CHECK(std::filesystem::exists(reply.at("outputPath").get<std::string>()));
}

TEST_CASE("model file commands refuse while training owns deployment files") {
    App app;
    const auto* train_command = app.registry.find("train_from_export");
    REQUIRE(train_command != nullptr);
    REQUIRE(train_command->async.has_value());

    std::promise<void> entered;
    std::promise<void> release;
    auto release_future = release.get_future().share();
    REQUIRE(app.runner.submit([&] {
        entered.set_value();
        release_future.wait();
    }));
    REQUIRE(entered.get_future().wait_for(std::chrono::seconds(1)) ==
            std::future_status::ready);

    std::promise<std::string> resolved;
    auto resolved_future = resolved.get_future();
    const auto& policy = *train_command->async;
    detail::dispatch_single_flight(
        [&](std::function<void()> job) { return app.runner.submit(std::move(job)); },
        [&](std::string result) { resolved.set_value(std::move(result)); },
        train_command->handler, "[{}]", "", policy.gate, policy.busy_message,
        policy.on_claimed);

    for (const char* command : {"reload_classifier_model", "rollback_classifier_model",
                                "retry_model_deployment_cleanup"}) {
        CAPTURE(command);
        CHECK_THROWS_WITH(app.registry.call(command),
                          "model deployment is in progress; wait for it to finish");
    }

    release.set_value();
    REQUIRE(resolved_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
}

TEST_CASE("cancel_training is developer-gated and, when open, reports nothing to cancel") {
    // ADR-0006: Release builds refuse unless SNAPBACK_DEV_TRAINING is set; Debug builds
    // allow. The test binary can be either, so it accepts the gate's refusal as one of the
    // two correct answers rather than assuming a build type.
    App app;
    try {
        const auto reply = app.registry.call("cancel_training");
        CHECK(reply.at("requested") == false);
    } catch (const std::runtime_error& err) {
        CHECK(std::string(err.what()).find("developer-only") != std::string::npos);
    }
}
