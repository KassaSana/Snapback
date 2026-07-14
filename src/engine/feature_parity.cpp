#include "engine/feature_parity.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace snapback {
namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to read " + path.string());
    }
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
    const auto raw_type = ev.at("type").get<std::string>();
    const auto event_type = parse_event_type(raw_type);
    if (!event_type) {
        throw std::runtime_error("unsupported scenario event type: " + raw_type);
    }

    CaptureEvent out;
    out.event_type = *event_type;
    out.timestamp_secs = timestamp_secs;
    out.app_name = ev.at("app").get<std::string>();
    out.window_title = ev.at("title").get<std::string>();
    out.mouse_speed = ev.value("mouse_speed", 0u);
    out.idle_duration_ms = ev.value("idle_duration_ms", 0u);
    return out;
}

}  // namespace

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

FeatureVector replay_feature_parity_scenario(const nlohmann::json& scenario,
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

std::vector<FeatureParityResult> replay_feature_parity_file(
    const std::filesystem::path& path,
    const std::vector<AppRuleRecord>& rules) {
    const auto file = nlohmann::json::parse(read_file(path));

    std::vector<FeatureParityResult> results;
    for (const auto& scenario : file.at("scenarios")) {
        FeatureParityResult result;
        result.name = scenario.at("name").get<std::string>();
        result.features = training_column_values(replay_feature_parity_scenario(scenario, rules));
        results.push_back(std::move(result));
    }
    return results;
}

nlohmann::json feature_parity_results_to_json(const std::vector<FeatureParityResult>& results) {
    auto out = nlohmann::json::array();
    for (const auto& result : results) {
        nlohmann::json features = nlohmann::json::object();
        for (const auto& [key, value] : result.features) {
            features[key] = value;
        }
        out.push_back({{"name", result.name}, {"features", features}});
    }
    return out;
}

}  // namespace snapback
