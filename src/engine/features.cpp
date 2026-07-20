#include "engine/features.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <numeric>
#include <set>
#include <vector>

#include "engine/app_context.hpp"

namespace snapback {
namespace {

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double std_dev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double avg = mean(values);
    double sum = 0.0;
    for (double v : values) sum += (v - avg) * (v - avg);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

double linear_slope(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double mean_x = static_cast<double>(values.size() - 1) / 2.0;
    const double mean_y = mean(values);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double x = static_cast<double>(i);
        num += (x - mean_x) * (values[i] - mean_y);
        den += (x - mean_x) * (x - mean_x);
    }
    return den == 0.0 ? 0.0 : num / den;
}

bool is_idle(EventType type) {
    return type == EventType::IdleStart || type == EventType::IdleEnd;
}

std::chrono::system_clock::time_point unix_time(double seconds) {
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(seconds))};
}

void fill_time_fields(FeatureVector& out, double now_secs) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(unix_time(now_secs));
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    out.hour_of_day() = static_cast<double>(tm.tm_hour);
    out.day_of_week() = static_cast<double>((tm.tm_wday + 6) % 7);  // Monday = 0.
}

}  // namespace

void FeatureExtractor::ingest(const CaptureEvent& ev) {
    // Cheap, per-event bookkeeping only: push into the rolling windows, trim, and track
    // current app / break state. Deliberately does NOT run extract() — callers that only
    // need a prediction occasionally (the engine throttles to ~1/sec) ingest every event
    // but extract on demand, so the O(window) scan runs ~1/sec instead of per event.
    const double now = ev.timestamp_secs;
    // Seed the session origin from the first event of the session (see begin_session).
    // The first event is the earliest moment the event clock and the session agree on.
    if (awaiting_session_start_) {
        session_start_secs_ = now;
        last_break_secs_ = now;  // Rust does the same: last_break_ts starts at session start
        awaiting_session_start_ = false;
    }
    if (!current_app_start_secs_ && !ev.app_name.empty()) {
        current_app_name_ = ev.app_name;
        current_window_title_ = ev.window_title;
        current_app_start_secs_ = now;
        if (!last_break_secs_) last_break_secs_ = now;
    }

    // Store a compact, string-free record in the rolling windows: interning app_name to a
    // small id (once, here) means the two deque copies + the unique-apps set no longer
    // allocate/copy strings per event. Distinct ids map 1:1 to distinct names, so
    // unique_apps_5min is unchanged.
    const WindowedEvent we{ev.event_type, now, ev.mouse_speed, ev.idle_duration_ms,
                           intern_app(ev.app_name)};
    events_30s_.push_back(we);
    events_5min_.push_back(we);
    trim(now);
    update_break_state(ev, now);
    update_current_app(ev, now);
}

std::uint32_t FeatureExtractor::intern_app(const std::string& app_name) {
    auto it = app_ids_.find(app_name);
    if (it != app_ids_.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(app_ids_.size());
    app_ids_.emplace(app_name, id);
    return id;
}

FeatureVector FeatureExtractor::extract(double now_secs) {
    return extract(now_secs, {});
}

void FeatureExtractor::update_focus_score(double score, double alpha) {
    focus_momentum_ = alpha * score + (1.0 - alpha) * focus_momentum_;
}

void FeatureExtractor::begin_session() {
    reset_for_session(std::nullopt);
    awaiting_session_start_ = true;
}

void FeatureExtractor::reset_for_session(std::optional<double> session_start_secs) {
    session_start_secs_ = session_start_secs;
    // An explicit origin (or an explicit "no session") overrides any pending lazy seed.
    awaiting_session_start_ = false;
    events_30s_.clear();
    events_5min_.clear();
    app_ids_.clear();
    last_break_secs_ = session_start_secs;
    current_app_name_.clear();
    current_window_title_.clear();
    current_app_start_secs_.reset();
    focus_momentum_ = 0.0;
}

FeatureVector FeatureExtractor::update(const CaptureEvent& ev,
                                       const std::vector<AppRuleRecord>& rules) {
    // ingest (bookkeeping) + extract (the scan). Kept as one call for tests and any
    // caller that wants a vector for every event; the engine splits the two (see
    // AppState::compute_event) to skip extract on throttled events.
    ingest(ev);
    return extract(ev.timestamp_secs, rules);
}

FeatureVector FeatureExtractor::extract(double now, const std::vector<AppRuleRecord>& rules) const {
    FeatureVector out;
    out.timestamp = now;
    fill_time_fields(out, now);

    if (events_5min_.empty()) return out;

    const double oldest_30s = events_30s_.empty() ? now : events_30s_.front().timestamp_secs;
    const double span_30s = std::max(std::min(window_seconds_, now - oldest_30s), 1e-6);

    std::vector<double> key_times;
    std::vector<double> speeds;
    std::vector<double> distances;
    std::vector<double> accelerations;
    std::size_t mouse_clicks = 0;
    std::size_t context_switches_30s = 0;
    bool title_changed = false;

    WindowedEvent previous_mouse{};
    bool have_previous_mouse = false;
    for (const auto& ev : events_30s_) {
        if (ev.event_type == EventType::KeyPress) key_times.push_back(ev.timestamp_secs);
        if (ev.event_type == EventType::MouseClick) ++mouse_clicks;
        if (ev.event_type == EventType::WindowFocusChange) ++context_switches_30s;
        if (ev.event_type == EventType::WindowTitleChange) title_changed = true;
        if (ev.event_type == EventType::MouseMove) {
            const double speed = static_cast<double>(ev.mouse_speed);
            speeds.push_back(speed);
            if (have_previous_mouse) {
                const double dt = std::max(ev.timestamp_secs - previous_mouse.timestamp_secs, 1e-6);
                distances.push_back(speed * dt);
                accelerations.push_back(std::abs(speed - static_cast<double>(previous_mouse.mouse_speed)) / dt);
            }
            previous_mouse = ev;
            have_previous_mouse = true;
        }
    }

    std::vector<double> intervals;
    for (std::size_t i = 1; i < key_times.size(); ++i) {
        intervals.push_back(key_times[i] - key_times[i - 1]);
    }

    std::size_t context_switches_5min = 0;
    std::set<std::uint32_t> unique_apps;  // interned app ids (distinct ids <-> distinct names)
    std::vector<double> idle_timestamps;
    double idle_time_30s = 0.0;
    std::size_t idle_count_5min = 0;
    for (const auto& ev : events_5min_) {
        if (ev.event_type == EventType::WindowFocusChange) ++context_switches_5min;
        unique_apps.insert(ev.app_id);
        if (is_idle(ev.event_type)) {
            ++idle_count_5min;
            idle_timestamps.push_back(ev.timestamp_secs);
        }
    }
    for (const auto& ev : events_30s_) {
        if (is_idle(ev.event_type)) idle_time_30s += static_cast<double>(ev.idle_duration_ms) / 1000.0;
    }

    double longest_active_stretch = long_window_seconds_;
    if (!idle_timestamps.empty()) {
        std::vector<double> boundaries = {now - long_window_seconds_};
        boundaries.insert(boundaries.end(), idle_timestamps.begin(), idle_timestamps.end());
        boundaries.push_back(now);
        longest_active_stretch = 0.0;
        for (std::size_t i = 1; i < boundaries.size(); ++i) {
            longest_active_stretch = std::max(longest_active_stretch, boundaries[i] - boundaries[i - 1]);
        }
    }

    const auto ctx = classify_app_context(current_app_name_, current_window_title_, rules);
    out.app_name = current_app_name_;
    out.window_title = current_window_title_;
    out.productivity_category = ctx.productivity_category();
    out.seconds_since_session_start() =
        session_start_secs_ ? std::floor(std::max(0.0, now - *session_start_secs_)) : 0.0;
    out.minutes_since_last_break() = last_break_secs_
                                         ? std::floor(std::max(0.0, (now - *last_break_secs_) / 60.0))
                                         : 0.0;
    out.keystroke_count() = static_cast<double>(key_times.size());
    out.keystroke_rate() = static_cast<double>(key_times.size()) / span_30s;
    out.keystroke_interval_mean() = mean(intervals);
    out.keystroke_interval_std() = std_dev(intervals);
    out.keystroke_interval_trend() = linear_slope(intervals);
    out.mouse_move_count() = static_cast<double>(speeds.size());
    out.mouse_distance_pixels() = std::accumulate(distances.begin(), distances.end(), 0.0);
    out.mouse_speed_mean() = mean(speeds);
    out.mouse_speed_std() = std_dev(speeds);
    out.mouse_acceleration_mean() = mean(accelerations);
    out.mouse_click_count() = static_cast<double>(mouse_clicks);
    out.context_switches_30s() = static_cast<double>(context_switches_30s);
    out.context_switches_5min() = static_cast<double>(context_switches_5min);
    out.time_in_current_app() =
        current_app_start_secs_ ? std::floor(std::max(0.0, now - *current_app_start_secs_)) : 0.0;
    out.unique_apps_5min() = static_cast<double>(unique_apps.size());
    out.idle_time_30s() = idle_time_30s;
    out.idle_event_count_5min() = static_cast<double>(idle_count_5min);
    out.longest_active_stretch_5min() = longest_active_stretch;
    out.window_title_length() = static_cast<double>(current_window_title_.size());
    out.window_title_changed_30s() = title_changed ? 1.0 : 0.0;
    out.is_browser() = ctx.is_browser ? 1.0 : 0.0;
    out.is_ide() = ctx.is_ide ? 1.0 : 0.0;
    out.is_communication() = ctx.is_communication ? 1.0 : 0.0;
    out.is_entertainment() = (ctx.is_entertainment || ctx.title_is_distracting) ? 1.0 : 0.0;
    out.is_productivity() = ctx.is_productivity ? 1.0 : 0.0;
    out.focus_momentum() = focus_momentum_;
    out.is_pseudo_productive() = 0.0;
    return out;
}

void FeatureExtractor::trim(double now) {
    while (!events_30s_.empty() && now - events_30s_.front().timestamp_secs > window_seconds_) {
        events_30s_.pop_front();
    }
    while (!events_5min_.empty() && now - events_5min_.front().timestamp_secs > long_window_seconds_) {
        events_5min_.pop_front();
    }
}

void FeatureExtractor::update_break_state(const CaptureEvent& ev, double now) {
    if (is_idle(ev.event_type) &&
        static_cast<double>(ev.idle_duration_ms) / 1000.0 >= break_threshold_seconds_) {
        last_break_secs_ = now;
    }
}

void FeatureExtractor::update_current_app(const CaptureEvent& ev, double now) {
    if (ev.event_type == EventType::WindowFocusChange) {
        current_app_name_ = ev.app_name;
        current_window_title_ = ev.window_title;
        current_app_start_secs_ = now;
    } else if (ev.event_type == EventType::WindowTitleChange) {
        current_window_title_ = ev.window_title;
    }
}

}  // namespace snapback
