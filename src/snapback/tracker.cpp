#include "snapback/tracker.hpp"

#include <algorithm>

#include "engine/app_context.hpp"
#include "snapback/title_parser.hpp"

namespace snapback {

void ContextTracker::reset() {
    *this = ContextTracker{};
}

void ContextTracker::set_prediction_feedback(std::optional<std::string> focus_state,
                                             std::optional<std::string> session_goal) {
    latest_focus_state_ = std::move(focus_state);
    latest_session_goal_ = std::move(session_goal);
}

std::optional<ContextSnapshotDto> ContextTracker::observe_window_change(
    const std::string& app_name,
    const std::string& window_title,
    const std::vector<AppRuleRecord>& app_rules,
    double now_secs,
    const std::string& timestamp) {
    const bool was_on_task = is_on_task(current_.app_name, current_.window_title, app_rules);
    const bool now_on_task = is_on_task(app_name, window_title, app_rules);

    if (state_ == DistractionState::Focused && current_.meaningful() && was_on_task) {
        last_focus_snapshot_ = current_;
    }

    if (state_ == DistractionState::Focused && was_on_task && !now_on_task) {
        state_ = DistractionState::Distracted;
        distraction_started_secs_ = now_secs;
    } else if (state_ == DistractionState::Distracted && now_on_task) {
        const double duration = distraction_started_secs_ >= 0.0
                                    ? std::max(0.0, now_secs - distraction_started_secs_)
                                    : 0.0;
        if (duration >= min_distraction_secs_) {
            state_ = DistractionState::Recovering;
            pending_snapback_ = build_snapback(duration);
        } else {
            state_ = DistractionState::Focused;
            focus_started_secs_ = now_secs;
        }
    }

    current_ = make_snapshot(app_name, window_title, timestamp);

    if (!current_.meaningful() || !now_on_task) return std::nullopt;
    last_focus_snapshot_ = current_;
    last_snapshot_secs_ = now_secs;
    return to_dto(current_);
}

std::optional<ContextSnapshotDto> ContextTracker::maybe_checkpoint_snapshot(
    const std::vector<AppRuleRecord>& app_rules,
    double now_secs,
    const std::string& timestamp) {
    return maybe_checkpoint_snapshot(app_rules, now_secs, [&timestamp] { return timestamp; });
}

std::optional<ContextSnapshotDto> ContextTracker::maybe_checkpoint_snapshot(
    const std::vector<AppRuleRecord>& app_rules,
    double now_secs,
    const std::function<std::string()>& timestamp_factory) {
    if (state_ != DistractionState::Focused) return std::nullopt;
    if (now_secs - last_snapshot_secs_ < snapshot_interval_secs_) return std::nullopt;
    if (!current_.meaningful() ||
        !is_on_task(current_.app_name, current_.window_title, app_rules)) {
        last_snapshot_secs_ = now_secs;
        return std::nullopt;
    }

    current_.timestamp = timestamp_factory();
    last_focus_snapshot_ = current_;
    last_snapshot_secs_ = now_secs;
    return to_dto(current_);
}

std::optional<SnapbackPayload> ContextTracker::take_pending_snapback() {
    auto out = std::move(pending_snapback_);
    pending_snapback_.reset();
    return out;
}

void ContextTracker::dismiss_recovery(double now_secs) {
    if (state_ == DistractionState::Recovering) {
        state_ = DistractionState::Focused;
        focus_started_secs_ = now_secs;
    }
}

ContextTracker::Snapshot ContextTracker::make_snapshot(const std::string& app_name,
                                                       const std::string& window_title,
                                                       const std::string& timestamp) {
    const auto hints = parse_title(window_title);
    Snapshot snapshot;
    snapshot.app_name = app_name;
    snapshot.window_title = window_title;
    snapshot.file_hint = hints.file_hint;
    snapshot.project_hint = hints.project_hint;
    snapshot.summary = hints.file_hint.empty() ? "Working in " + app_name
                                               : "Editing " + hints.file_hint;
    snapshot.timestamp = timestamp;
    return snapshot;
}

ContextSnapshotDto ContextTracker::to_dto(const Snapshot& snapshot) {
    ContextSnapshotDto dto;
    dto.app_name = snapshot.app_name;
    dto.window_title = snapshot.window_title;
    dto.file_hint = snapshot.file_hint;
    dto.project_hint = snapshot.project_hint;
    dto.summary = snapshot.summary;
    dto.timestamp = snapshot.timestamp;
    return dto;
}

bool ContextTracker::is_on_task(const std::string& app_name,
                                const std::string& window_title,
                                const std::vector<AppRuleRecord>& app_rules) const {
    if (app_name.empty() && window_title.empty()) return false;
    const auto ctx = classify_app_context(app_name, window_title, app_rules);
    return snapback_on_task(ctx, window_title, latest_focus_state_, latest_session_goal_);
}

std::optional<SnapbackPayload> ContextTracker::build_snapback(double distraction_secs) const {
    if (!last_focus_snapshot_) return std::nullopt;
    SnapbackPayload payload;
    payload.app_name = last_focus_snapshot_->app_name;
    payload.window_title = last_focus_snapshot_->window_title;
    payload.file_hint = last_focus_snapshot_->file_hint;
    payload.summary = last_focus_snapshot_->file_hint.empty()
                          ? "Return to " + last_focus_snapshot_->app_name
                          : "Return to " + last_focus_snapshot_->file_hint;
    payload.distraction_duration_secs = static_cast<std::uint32_t>(distraction_secs);
    return payload;
}

}  // namespace snapback
