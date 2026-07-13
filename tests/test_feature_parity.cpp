#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "types.hpp"

using namespace snapback;

#ifndef SNAPBACK_FIXTURES_DIR
#define SNAPBACK_FIXTURES_DIR "fixtures"
#endif

namespace {

constexpr double kEpsilon = 1e-6;

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::optional<EventType> parse_event_type(const std::string& raw) {
    if (raw == "key_press") return EventType::KeyPress;
    if (raw == "key_release") return EventType::KeyRelease;
    if (raw == "mouse_move") return EventType::MouseMove;
    if (raw == "mouse_click") return EventType::MouseClick;
    if (raw == "window_focus_change") return EventType::WindowFocusChange;
    if (raw == "window_title_change") return EventType::WindowTitleChange;
    if (raw == "idle_start") return EventType::IdleStart;
    if (raw == "idle_end") return EventType::IdleEnd;
    return std::nullopt;
}

CaptureEvent scenario_event(const nlohmann::json& ev, double timestamp_secs) {
    CaptureEvent out;
    out.event_type = *parse_event_type(ev.at("type").get<std::string>());
    out.timestamp_secs = timestamp_secs;
    out.app_name = ev.at("app").get<std::string>();
    out.window_title = ev.at("title").get<std::string>();
    out.mouse_speed = ev.value("mouse_speed", 0u);
    out.idle_duration_ms = ev.value("idle_duration_ms", 0u);
    return out;
}

std::unordered_map<std::string, double> training_column_values(const FeatureVector& f) {
    return {
        {"timestamp", f.timestamp},
        {"seconds_since_session_start", f.seconds_since_session_start()},
        {"hour_of_day", f.hour_of_day()},
        {"day_of_week", f.day_of_week()},
        {"minutes_since_last_break", f.minutes_since_last_break()},
        {"keystroke_count", f.keystroke_count()},
        {"keystroke_rate", f.keystroke_rate()},
        {"keystroke_interval_mean", f.keystroke_interval_mean()},
        {"keystroke_interval_std", f.keystroke_interval_std()},
        {"keystroke_interval_trend", f.keystroke_interval_trend()},
        {"mouse_move_count", f.mouse_move_count()},
        {"mouse_distance_pixels", f.mouse_distance_pixels()},
        {"mouse_speed_mean", f.mouse_speed_mean()},
        {"mouse_speed_std", f.mouse_speed_std()},
        {"mouse_acceleration_mean", f.mouse_acceleration_mean()},
        {"mouse_click_count", f.mouse_click_count()},
        {"context_switches_30s", f.context_switches_30s()},
        {"context_switches_5min", f.context_switches_5min()},
        {"time_in_current_app", f.time_in_current_app()},
        {"unique_apps_5min", f.unique_apps_5min()},
        {"idle_time_30s", f.idle_time_30s()},
        {"idle_event_count_5min", f.idle_event_count_5min()},
        {"longest_active_stretch_5min", f.longest_active_stretch_5min()},
        {"window_title_length", f.window_title_length()},
        {"window_title_changed_30s", f.window_title_changed_30s()},
        {"is_browser", f.is_browser()},
        {"is_ide", f.is_ide()},
        {"is_communication", f.is_communication()},
        {"is_entertainment", f.is_entertainment()},
        {"is_productivity", f.is_productivity()},
        {"focus_momentum", f.focus_momentum()},
        {"is_pseudo_productive", f.is_pseudo_productive()},
    };
}

FeatureVector replay_scenario(const nlohmann::json& scenario,
                              const std::vector<AppRuleRecord>& rules) {
    FeatureExtractor extractor;
    const double base_time = scenario.at("base_time").get<double>();
    for (const auto& ev : scenario.at("events")) {
        const double ts = base_time + ev.at("offset_secs").get<double>();
        extractor.update(scenario_event(ev, ts), rules);
    }
    const auto& events = scenario.at("events");
    const double now = events.empty()
                           ? base_time
                           : base_time + events.back().at("offset_secs").get<double>();
    return extractor.extract(now, rules);
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
        const auto features = training_column_values(replay_scenario(scenario, {}));
        check_feature_expectations(name, scenario.at("expect"), features);
    }
}

void run_classifier_file(const std::filesystem::path& path) {
    Classifier classifier;
    const auto file = nlohmann::json::parse(read_file(path));
    for (const auto& scenario : file.at("scenarios")) {
        const std::string name = scenario.at("name").get<std::string>();
        const auto rules = parse_rules(scenario);
        const auto features = replay_scenario(scenario, rules);
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
