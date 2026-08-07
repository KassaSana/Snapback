// Feature extraction.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace snapback {

// 31 numeric model features in the fixed model-input order.
inline constexpr std::size_t kFeatureCount = 31;
// Model identity includes this contract so a prediction can be traced back to the feature
// order the deployed model was trained against.
inline constexpr std::string_view kFeatureContractId = "snapback-features-v1-31";

struct FeatureVector {
    std::array<double, kFeatureCount> values{};
    double timestamp{};
    std::string app_name;
    std::string window_title;
    std::string productivity_category{"Unknown"};

    double& seconds_since_session_start() { return values[0]; }
    double& hour_of_day() { return values[1]; }
    double& day_of_week() { return values[2]; }
    double& minutes_since_last_break() { return values[3]; }
    double& keystroke_count() { return values[4]; }
    double& keystroke_rate() { return values[5]; }
    double& keystroke_interval_mean() { return values[6]; }
    double& keystroke_interval_std() { return values[7]; }
    double& keystroke_interval_trend() { return values[8]; }
    double& mouse_move_count() { return values[9]; }
    double& mouse_distance_pixels() { return values[10]; }
    double& mouse_speed_mean() { return values[11]; }
    double& mouse_speed_std() { return values[12]; }
    double& mouse_acceleration_mean() { return values[13]; }
    double& mouse_click_count() { return values[14]; }
    double& context_switches_30s() { return values[15]; }
    double& context_switches_5min() { return values[16]; }
    double& time_in_current_app() { return values[17]; }
    double& unique_apps_5min() { return values[18]; }
    double& idle_time_30s() { return values[19]; }
    double& idle_event_count_5min() { return values[20]; }
    double& longest_active_stretch_5min() { return values[21]; }
    double& window_title_length() { return values[22]; }
    double& window_title_changed_30s() { return values[23]; }
    double& is_browser() { return values[24]; }
    double& is_ide() { return values[25]; }
    double& is_communication() { return values[26]; }
    double& is_entertainment() { return values[27]; }
    double& is_productivity() { return values[28]; }
    double& focus_momentum() { return values[29]; }
    double& is_pseudo_productive() { return values[30]; }

    double seconds_since_session_start() const { return values[0]; }
    double hour_of_day() const { return values[1]; }
    double day_of_week() const { return values[2]; }
    double minutes_since_last_break() const { return values[3]; }
    double keystroke_count() const { return values[4]; }
    double keystroke_rate() const { return values[5]; }
    double keystroke_interval_mean() const { return values[6]; }
    double keystroke_interval_std() const { return values[7]; }
    double keystroke_interval_trend() const { return values[8]; }
    double mouse_move_count() const { return values[9]; }
    double mouse_distance_pixels() const { return values[10]; }
    double mouse_speed_mean() const { return values[11]; }
    double mouse_speed_std() const { return values[12]; }
    double mouse_acceleration_mean() const { return values[13]; }
    double mouse_click_count() const { return values[14]; }
    double context_switches_30s() const { return values[15]; }
    double context_switches_5min() const { return values[16]; }
    double time_in_current_app() const { return values[17]; }
    double unique_apps_5min() const { return values[18]; }
    double idle_time_30s() const { return values[19]; }
    double idle_event_count_5min() const { return values[20]; }
    double longest_active_stretch_5min() const { return values[21]; }
    double window_title_length() const { return values[22]; }
    double window_title_changed_30s() const { return values[23]; }
    double is_browser() const { return values[24]; }
    double is_ide() const { return values[25]; }
    double is_communication() const { return values[26]; }
    double is_entertainment() const { return values[27]; }
    double is_productivity() const { return values[28]; }
    double focus_momentum() const { return values[29]; }
    double is_pseudo_productive() const { return values[30]; }
};

// Compact, string-free event stored in the rolling windows. Only the fields extract()
// reads from the deques — app_name is interned to a small id at ingest time so the
// two deque copies per event never allocate or copy std::string.
struct WindowedEvent {
    EventType event_type{};
    double timestamp_secs{};
    std::uint32_t mouse_speed{};
    std::uint32_t idle_duration_ms{};
    std::uint32_t app_id{};
};

class FeatureExtractor {
public:
    void ingest(const CaptureEvent& ev);
    FeatureVector extract(double now_secs);

    void update_focus_score(double score, double alpha);
    // Explicit origin. Used by tests and the parity harness, which know the timestamp the
    // scenario starts at. Pass nullopt to mean "no active session" (feature stays 0).
    void reset_for_session(std::optional<double> session_start_secs);

    // Start a session whose origin is not yet known, seeding it from the first event.
    //
    // AppState can't call reset_for_session with a real value: `started_at` is wall-clock
    // while event timestamps come from a monotonic clock started at process launch, and
    // there is no conversion between the two. Passing nullopt (which is what it used to do)
    // silently pinned seconds_since_session_start to 0.0 for every row ever written.
    void begin_session();

    // Roadmap 7.25. Resume a session that has already been running for `elapsed_secs` — the
    // restart case, where the row is still ACTIVE but this process has never seen an event
    // from it.
    //
    // Same lazy seed as begin_session(), back-dated: the origin becomes the first event's
    // timestamp minus the elapsed time the session already has. Without it, a session
    // recovered after a crash reported `seconds_since_session_start` counting up from zero
    // while the recap next to it said four hours — the same feature disagreeing with the same
    // session in two places.
    //
    // The break clock is deliberately *not* back-dated. Nothing is known about what happened
    // while the process was gone, and a back-dated `minutes_since_last_break` would fire the
    // hyperfocus nudge the instant the app reopened.
    void resume_session(double elapsed_secs);

    FeatureVector update(const CaptureEvent& ev, const std::vector<AppRuleRecord>& rules = {});
    FeatureVector extract(double now_secs, const std::vector<AppRuleRecord>& rules) const;

private:
    std::uint32_t intern_app(const std::string& app_name);
    void trim(double now_secs);
    void update_break_state(const CaptureEvent& ev, double now_secs);
    void update_current_app(const CaptureEvent& ev, double now_secs);

    double window_seconds_ = 30.0;
    double long_window_seconds_ = 300.0;
    double break_threshold_seconds_ = 300.0;
    // Roadmap 7.24. Wall-clock epoch seconds from the newest event, used only for the
    // calendar features. Zero means no event has supplied one, and the extractor falls back
    // to the monotonic clock — which is how the epoch-shaped parity fixtures keep working.
    double last_wall_clock_secs_ = 0.0;
    std::unordered_map<std::string, std::uint32_t> app_ids_;
    std::deque<WindowedEvent> events_30s_;
    std::deque<WindowedEvent> events_5min_;
    std::optional<double> session_start_secs_;
    bool awaiting_session_start_ = false;  // seed session_start_secs_ from the next event
    // How far behind the first event the seeded origin should sit (Roadmap 7.25). Zero for a
    // freshly started session; the already-elapsed seconds for a resumed one.
    double pending_session_backdate_secs_ = 0.0;
    std::optional<double> last_break_secs_;
    std::string current_app_name_;
    std::string current_window_title_;
    std::optional<double> current_app_start_secs_;
    double focus_momentum_ = 0.0;
};

}  // namespace snapback
