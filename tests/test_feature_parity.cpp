#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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
        if (expect.contains("distraction_risk_max")) {
            CHECK_MESSAGE(scores.distraction_risk <= expect.at("distraction_risk_max").get<double>(),
                          name << ".distraction_risk");
        }
    }
}

}  // namespace

TEST_CASE("feature parity scenarios.json matches Rust fixtures") {
    run_feature_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "feature_parity" /
                     "scenarios.json");
}

TEST_CASE("classifier parity scenarios match heuristic guardrails") {
    run_classifier_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "feature_parity" /
                        "classifier_scenarios.json");
}
