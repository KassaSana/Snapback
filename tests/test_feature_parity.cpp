#include "doctest_wrapper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "engine/classifier.hpp"
#include "engine/feature_parity.hpp"
#include "types.hpp"

using namespace snapback;

#ifndef SNAPBACK_FIXTURES_DIR
#define SNAPBACK_FIXTURES_DIR "fixtures"
#endif

namespace {

constexpr double kEpsilon = 1e-6;

constexpr std::array<std::string_view, kFeatureCount> kGoldenFeatureOrder = {
    "seconds_since_session_start", "hour_of_day", "day_of_week", "minutes_since_last_break",
    "keystroke_count", "keystroke_rate", "keystroke_interval_mean", "keystroke_interval_std",
    "keystroke_interval_trend", "mouse_move_count", "mouse_distance_pixels", "mouse_speed_mean",
    "mouse_speed_std", "mouse_acceleration_mean", "mouse_click_count", "context_switches_30s",
    "context_switches_5min", "time_in_current_app", "unique_apps_5min", "idle_time_30s",
    "idle_event_count_5min", "longest_active_stretch_5min", "window_title_length",
    "window_title_changed_30s", "is_browser", "is_ide", "is_communication", "is_entertainment",
    "is_productivity", "focus_momentum", "is_pseudo_productive"};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void check_feature_expectations(const std::string& name,
                                const nlohmann::json& expect,
                                const std::unordered_map<std::string, double>& features) {
    for (auto it = expect.begin(); it != expect.end(); ++it) {
        const std::string key = it.key();
        if (key.size() >= 4 && key.compare(key.size() - 4, 4, "_min") == 0) {
            const std::string base = key.substr(0, key.size() - 4);
            const double min = it.value().get<double>();
            REQUIRE(features.count(base) > 0);
            CHECK_MESSAGE(features.at(base) >= min - kEpsilon,
                          name << "." << base << ": " << features.at(base) << " < min " << min);
            continue;
        }
        if (key.size() >= 4 && key.compare(key.size() - 4, 4, "_max") == 0) {
            const std::string base = key.substr(0, key.size() - 4);
            const double max = it.value().get<double>();
            REQUIRE(features.count(base) > 0);
            CHECK_MESSAGE(features.at(base) <= max + kEpsilon,
                          name << "." << base << ": " << features.at(base) << " > max " << max);
            continue;
        }
        REQUIRE(features.count(key) > 0);
        const double expected = it.value().get<double>();
        CHECK_MESSAGE(std::abs(features.at(key) - expected) <= kEpsilon,
                      name << "." << key << ": expected " << expected << ", got "
                           << features.at(key));
    }
}

std::vector<AppRuleRecord> parse_rules(const nlohmann::json& scenario) {
    std::vector<AppRuleRecord> rules;
    if (!scenario.contains("rules")) return rules;
    for (const auto& r : scenario.at("rules")) {
        AppRuleRecord rule;
        rule.pattern = r.at("pattern").get<std::string>();
        const auto kind = r.at("rule_type").get<std::string>();
        if (kind == "allow") rule.rule_type = AppRuleKind::Allow;
        else if (kind == "block") rule.rule_type = AppRuleKind::Block;
        rules.push_back(rule);
    }
    return rules;
}

FocusMode parse_focus_mode(const nlohmann::json& scenario) {
    if (!scenario.contains("focus_mode")) return FocusMode::Normal;
    return focus_mode_from_string(scenario.at("focus_mode").get<std::string>());
}

void run_feature_file(const std::filesystem::path& path) {
    const auto file = nlohmann::json::parse(read_file(path));
    for (const auto& scenario : file.at("scenarios")) {
        const std::string name = scenario.at("name").get<std::string>();
        const auto features =
            training_column_values(replay_feature_parity_scenario(scenario, {}));
        check_feature_expectations(name, scenario.at("expect"), features);
    }
}

void run_feature_golden_file(const std::filesystem::path& scenarios_path,
                             const std::filesystem::path& golden_path) {
    const auto scenarios = nlohmann::json::parse(read_file(scenarios_path));
    const auto golden = nlohmann::json::parse(read_file(golden_path));
    REQUIRE(golden.is_array());
    REQUIRE(golden.size() == scenarios.at("scenarios").size());

    for (const auto& expected : golden) {
        const std::string name = expected.at("name").get<std::string>();
        const auto scenario_it = std::find_if(
            scenarios.at("scenarios").begin(), scenarios.at("scenarios").end(),
            [&name](const nlohmann::json& scenario) {
                return scenario.at("name").get<std::string>() == name;
            });
        REQUIRE(scenario_it != scenarios.at("scenarios").end());

        const auto actual = replay_feature_parity_scenario(*scenario_it, {});
        const auto& expected_features = expected.at("features");
        REQUIRE(expected_features.size() == kFeatureCount + 1);
        CHECK_MESSAGE(std::abs(actual.timestamp - expected_features.at("timestamp").get<double>()) <=
                          kEpsilon,
                      name << ".timestamp");
        for (std::size_t i = 0; i < kGoldenFeatureOrder.size(); ++i) {
            const auto key = kGoldenFeatureOrder[i];
            CHECK_MESSAGE(std::abs(actual.values[i] - expected_features.at(key).get<double>()) <=
                              kEpsilon,
                          name << "." << key << " [feature index " << i << "]");
        }
    }
}

void run_classifier_file(const std::filesystem::path& path) {
    Classifier classifier;
    const auto file = nlohmann::json::parse(read_file(path));
    for (const auto& scenario : file.at("scenarios")) {
        const std::string name = scenario.at("name").get<std::string>();
        const auto rules = parse_rules(scenario);
        const auto features = replay_feature_parity_scenario(scenario, rules);
        const auto scores =
            classifier.predict(features, parse_focus_mode(scenario), std::nullopt, rules);
        const auto& expect = scenario.at("expect");
        if (expect.contains("focus_state")) {
            CHECK_MESSAGE(scores.focus_state == expect.at("focus_state").get<std::string>(),
                          name << ".focus_state");
        }
        if (expect.contains("thrash_score_min")) {
            CHECK_MESSAGE(scores.thrash_score >= expect.at("thrash_score_min").get<double>(),
                          name << ".thrash_score");
        }
        if (expect.contains("drift_score_min")) {
            CHECK_MESSAGE(scores.drift_score >= expect.at("drift_score_min").get<double>(),
                          name << ".drift_score");
        }
        if (expect.contains("distraction_risk_max")) {
            CHECK_MESSAGE(scores.distraction_risk <= expect.at("distraction_risk_max").get<double>(),
                          name << ".distraction_risk");
        }
    }
}

}  // namespace

TEST_CASE("feature parity scenarios satisfy their behavioral expectations") {
    run_feature_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "feature_parity" /
                     "scenarios.json");
}

TEST_CASE("feature vectors match the checked-in golden fixture") {
    const auto dir = std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "feature_parity";
    run_feature_golden_file(dir / "scenarios.json", dir / "golden.json");
}

TEST_CASE("classifier parity scenarios match heuristic guardrails") {
    run_classifier_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "feature_parity" /
                        "classifier_scenarios.json");
}
