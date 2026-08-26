#include "types.hpp"

#include <algorithm>
#include <cctype>

namespace snapback {
namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// An empty optional is written as JSON null rather than omitted, so the field is
// always present on the wire and the frontend can read it without a guard.
void put_opt(json& j, const char* key, const std::optional<std::string>& v) {
    if (v) j[key] = *v;
    else j[key] = nullptr;
}

std::optional<std::string> opt_str(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    return it->get<std::string>();
}

// ADR-0007's optional instants. Null on the wire stays null in C++, because for these fields
// it is a fact rather than an absence of data: a session with no `endedAtMs` is still running.
void put_opt_ms(json& j, const char* key, const std::optional<std::int64_t>& v) {
    if (v) j[key] = *v;
    else j[key] = nullptr;
}

std::optional<std::int64_t> opt_ms(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    return it->get<std::int64_t>();
}

// Tolerant getter: missing/null -> default. Keeps from_json robust to schema drift.
template <class T>
T get_or(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->template get<T>();
}

}  // namespace

// ---- enum / string helpers -------------------------------------------------

FocusMode focus_mode_from_string(const std::string& s) {
    const std::string l = to_lower(s);
    if (l == "deep") return FocusMode::Deep;
    if (l == "recovery") return FocusMode::Recovery;
    return FocusMode::Normal;  // unknown -> Normal
}

std::optional<AppRuleKind> app_rule_kind_from_string(const std::string& s) {
    const std::string l = to_lower(s);
    if (l == "allow") return AppRuleKind::Allow;
    if (l == "block") return AppRuleKind::Block;
    return std::nullopt;  // unknown -> None (not a default)
}

const char* label_source_as_str(LabelSource s) {
    switch (s) {
        case LabelSource::Hotkey: return "hotkey";
        case LabelSource::Survey: return "survey";
        case LabelSource::Auto: return "auto";
        case LabelSource::Manual: default: return "manual";
    }
}

LabelSource label_source_parse(const std::optional<std::string>& value) {
    const std::string l = to_lower(value.value_or("manual"));
    if (l == "hotkey") return LabelSource::Hotkey;
    if (l == "survey") return LabelSource::Survey;
    if (l == "auto") return LabelSource::Auto;
    return LabelSource::Manual;  // None/empty/unknown -> Manual
}

// ---- CaptureEvent (snake_case, internal) -----------------------------------

void to_json(json& j, const CaptureEvent& v) {
    j = json{{"event_type", v.event_type},
             {"timestamp_secs", v.timestamp_secs},
             {"wall_clock_secs", v.wall_clock_secs},
             {"app_name", v.app_name},
             {"window_title", v.window_title},
             {"mouse_x", v.mouse_x},
             {"mouse_y", v.mouse_y},
             {"mouse_speed", v.mouse_speed},
             {"idle_duration_ms", v.idle_duration_ms}};
}
void from_json(const json& j, CaptureEvent& v) {
    v.event_type = j.at("event_type").get<EventType>();
    v.timestamp_secs = j.at("timestamp_secs").get<double>();
    // Tolerant: replayed fixtures and older payloads carry no wall clock, and 0 means
    // "fall back to timestamp_secs" rather than "1970".
    v.wall_clock_secs = get_or<double>(j, "wall_clock_secs", 0.0);
    v.app_name = get_or<std::string>(j, "app_name", "");
    v.window_title = get_or<std::string>(j, "window_title", "");
    v.mouse_x = get_or<std::int32_t>(j, "mouse_x", 0);
    v.mouse_y = get_or<std::int32_t>(j, "mouse_y", 0);
    v.mouse_speed = get_or<std::uint32_t>(j, "mouse_speed", 0);
    v.idle_duration_ms = get_or<std::uint32_t>(j, "idle_duration_ms", 0);
}

// ---- PredictionRecord ------------------------------------------------------

void to_json(json& j, const PredictionRecord& v) {
    j = json{{"sessionId", v.session_id},
             {"focusScore", v.focus_score},
             {"distractionRisk", v.distraction_risk},
             {"focusState", v.focus_state},
             {"thrashScore", v.thrash_score},
             {"driftScore", v.drift_score},
             {"goalAlignment", v.goal_alignment},
             {"timestampMs", v.timestamp_ms},
             {"modelId", v.model_id}};
    put_opt(j, "stateSource", v.state_source);
}
void from_json(const json& j, PredictionRecord& v) {
    v.session_id = get_or<std::string>(j, "sessionId", "");
    v.focus_score = get_or<double>(j, "focusScore", 0.0);
    v.distraction_risk = get_or<double>(j, "distractionRisk", 0.0);
    v.focus_state = get_or<std::string>(j, "focusState", "");
    v.thrash_score = get_or<double>(j, "thrashScore", 0.0);
    v.drift_score = get_or<double>(j, "driftScore", 0.0);
    v.goal_alignment = get_or<double>(j, "goalAlignment", 0.5);
    v.timestamp_ms = get_or<std::int64_t>(j, "timestampMs", 0);
    v.model_id = get_or<std::string>(j, "modelId", "heuristic:snapback-features-v1-31");
    v.state_source = opt_str(j, "stateSource");
}

// ---- SessionRecord ---------------------------------------------------------

void to_json(json& j, const SessionRecord& v) {
    j = json{{"sessionId", v.session_id},
             {"goal", v.goal},
             {"status", v.status},
             {"focusMode", v.focus_mode}};
    put_opt_ms(j, "startedAtMs", v.started_at_ms);
    put_opt_ms(j, "endedAtMs", v.ended_at_ms);
    put_opt(j, "reflectionDone", v.reflection_done);
    put_opt(j, "reflectionNextStep", v.reflection_next_step);
}
void from_json(const json& j, SessionRecord& v) {
    v.session_id = get_or<std::string>(j, "sessionId", "");
    v.goal = get_or<std::string>(j, "goal", "");
    v.status = get_or<std::string>(j, "status", "");
    v.focus_mode = get_or<std::string>(j, "focusMode", "normal");
    v.started_at_ms = opt_ms(j, "startedAtMs");
    v.ended_at_ms = opt_ms(j, "endedAtMs");
    v.reflection_done = opt_str(j, "reflectionDone");
    v.reflection_next_step = opt_str(j, "reflectionNextStep");
}

// ---- SessionRecap ----------------------------------------------------------

void to_json(json& j, const SessionRecap& v) {
    j = json{{"sessionId", v.session_id},
             {"goal", v.goal},
             {"durationSecs", v.duration_secs},
             // null rather than 0 when unmeasured, so the UI can tell "we did not track this
             // session" from "the user was present for none of it".
             {"activeSecs", v.active_secs ? json(*v.active_secs) : json(nullptr)},
             {"avgFocusScore", v.avg_focus_score},
             {"avgDistractionRisk", v.avg_distraction_risk},
             {"snapbackCount", v.snapback_count},
             {"thrashSpikes", v.thrash_spikes},
             {"deepFocusPct", v.deep_focus_pct}};
}
void from_json(const json& j, SessionRecap& v) {
    v.session_id = get_or<std::string>(j, "sessionId", "");
    v.goal = get_or<std::string>(j, "goal", "");
    v.duration_secs = get_or<std::uint64_t>(j, "durationSecs", 0);
    // get_or maps missing *and* null to the default, which is exactly right here: both mean
    // "not measured", and nullopt is how that is spelled.
    if (const auto it = j.find("activeSecs"); it != j.end() && !it->is_null()) {
        v.active_secs = it->get<std::uint64_t>();
    } else {
        v.active_secs.reset();
    }
    v.avg_focus_score = get_or<double>(j, "avgFocusScore", 0.0);
    v.avg_distraction_risk = get_or<double>(j, "avgDistractionRisk", 0.0);
    v.snapback_count = get_or<std::uint32_t>(j, "snapbackCount", 0);
    v.thrash_spikes = get_or<std::uint32_t>(j, "thrashSpikes", 0);
    v.deep_focus_pct = get_or<double>(j, "deepFocusPct", 0.0);
}

// ---- SessionSummary --------------------------------------------------------

void to_json(json& j, const SessionSummary& v) {
    j = json{{"record", v.record}, {"recap", v.recap}};
}
void from_json(const json& j, SessionSummary& v) {
    v.record = j.at("record").get<SessionRecord>();
    v.recap = j.at("recap").get<SessionRecap>();
}

// ---- PermissionStatus ------------------------------------------------------

void to_json(json& j, const PermissionStatus& v) {
    j = json{{"captureAvailable", v.capture_available},
             {"captureProbeConfirmed", v.capture_probe_confirmed},
             {"activeWindowAvailable", v.active_window_available},
             {"message", v.message},
             {"setupSteps", v.setup_steps}};
}
void from_json(const json& j, PermissionStatus& v) {
    v.capture_available = get_or<bool>(j, "captureAvailable", false);
    v.capture_probe_confirmed = get_or<bool>(j, "captureProbeConfirmed", false);
    v.active_window_available = get_or<bool>(j, "activeWindowAvailable", false);
    v.message = get_or<std::string>(j, "message", "");
    v.setup_steps = get_or<std::vector<std::string>>(j, "setupSteps", {});
}

// ---- ClassifierStatus ------------------------------------------------------

void to_json(json& j, const ClassifierStatus& v) {
    j = json{{"backend", v.backend}, {"onnxRuntimeEnabled", v.onnx_runtime_enabled}};
    put_opt(j, "modelPath", v.model_path);
    put_opt(j, "modelId", v.model_id);
}
void from_json(const json& j, ClassifierStatus& v) {
    v.backend = get_or<std::string>(j, "backend", "heuristic");
    v.onnx_runtime_enabled = get_or<bool>(j, "onnxRuntimeEnabled", false);
    v.model_path = opt_str(j, "modelPath");
    v.model_id = opt_str(j, "modelId");
}

// ---- ModelDeploymentHealth -------------------------------------------------

void to_json(json& j, const ModelDeploymentHealth& v) {
    j = json{{"state", v.state},
             {"preservedPaths", v.preserved_paths},
             {"retryCleanupAvailable", v.retry_cleanup_available},
             {"rollbackAvailable", v.rollback_available}};
    put_opt(j, "message", v.message);
}
void from_json(const json& j, ModelDeploymentHealth& v) {
    v.state = get_or<std::string>(j, "state", "ok");
    v.message = opt_str(j, "message");
    v.preserved_paths = get_or<std::vector<std::string>>(j, "preservedPaths", {});
    v.retry_cleanup_available = get_or<bool>(j, "retryCleanupAvailable", false);
    v.rollback_available = get_or<bool>(j, "rollbackAvailable", false);
}

// ---- HealthStatus ----------------------------------------------------------

void to_json(json& j, const HealthStatus& v) {
    j = json{{"status", v.status},
             {"captureRunning", v.capture_running},
             {"captureFailed", v.capture_failed},
             {"captureEventsDropped", v.capture_events_dropped},
             {"captureStalled", v.capture_stalled},
             {"predictionSuppressionReason", v.prediction_suppression_reason},
             {"permissions", v.permissions},
             {"classifier", v.classifier},
             {"modelDeployment", v.model_deployment},
             {"developerToolsEnabled", v.developer_tools_enabled}};
    if (v.last_prediction_age_secs) {
        j["lastPredictionAgeSecs"] = *v.last_prediction_age_secs;
    } else {
        j["lastPredictionAgeSecs"] = nullptr;
    }
    put_opt(j, "captureFailureReason", v.capture_failure_reason);
    put_opt(j, "overlayFailureReason", v.overlay_failure_reason);
    put_opt(j, "persistenceFailureReason", v.persistence_failure_reason);
}
void from_json(const json& j, HealthStatus& v) {
    v.status = get_or<std::string>(j, "status", "offline");
    v.capture_running = get_or<bool>(j, "captureRunning", false);
    v.capture_failed = get_or<bool>(j, "captureFailed", false);
    v.capture_failure_reason = opt_str(j, "captureFailureReason");
    v.overlay_failure_reason = opt_str(j, "overlayFailureReason");
    v.persistence_failure_reason = opt_str(j, "persistenceFailureReason");
    v.capture_events_dropped = get_or<std::uint64_t>(j, "captureEventsDropped", 0);
    v.capture_stalled = get_or<bool>(j, "captureStalled", false);
    const auto age = j.find("lastPredictionAgeSecs");
    v.last_prediction_age_secs = age == j.end() || age->is_null()
                                     ? std::nullopt
                                     : std::optional<double>(age->get<double>());
    v.prediction_suppression_reason =
        get_or<std::string>(j, "predictionSuppressionReason", "none");
    v.permissions = get_or<PermissionStatus>(j, "permissions", {});
    v.classifier = get_or<ClassifierStatus>(j, "classifier", {});
    v.model_deployment = get_or<ModelDeploymentHealth>(j, "modelDeployment", {});
    v.developer_tools_enabled = get_or<bool>(j, "developerToolsEnabled", false);
}

// ---- SnapbackPayload -------------------------------------------------------

void to_json(json& j, const SnapbackPayload& v) {
    j = json{{"summary", v.summary},
             {"appName", v.app_name},
             {"windowTitle", v.window_title},
             {"fileHint", v.file_hint},
             {"distractionDurationSecs", v.distraction_duration_secs}};
}
void from_json(const json& j, SnapbackPayload& v) {
    v.summary = get_or<std::string>(j, "summary", "");
    v.app_name = get_or<std::string>(j, "appName", "");
    v.window_title = get_or<std::string>(j, "windowTitle", "");
    v.file_hint = get_or<std::string>(j, "fileHint", "");
    v.distraction_duration_secs = get_or<std::uint32_t>(j, "distractionDurationSecs", 0);
}

// ---- ContextSnapshotDto ----------------------------------------------------

void to_json(json& j, const ContextSnapshotDto& v) {
    j = json{{"appName", v.app_name},
             {"windowTitle", v.window_title},
             {"fileHint", v.file_hint},
             {"projectHint", v.project_hint},
             {"summary", v.summary},
             {"timestampMs", v.timestamp_ms}};
}
void from_json(const json& j, ContextSnapshotDto& v) {
    v.app_name = get_or<std::string>(j, "appName", "");
    v.window_title = get_or<std::string>(j, "windowTitle", "");
    v.file_hint = get_or<std::string>(j, "fileHint", "");
    v.project_hint = get_or<std::string>(j, "projectHint", "");
    v.summary = get_or<std::string>(j, "summary", "");
    v.timestamp_ms = get_or<std::int64_t>(j, "timestampMs", 0);
}

// ---- AppRuleRecord ---------------------------------------------------------

void to_json(json& j, const AppRuleRecord& v) {
    j = json{{"id", v.id},
             {"pattern", v.pattern},
             {"ruleType", v.rule_type},
             {"createdAtMs", v.created_at_ms},
             {"updatedAtMs", v.updated_at_ms}};
    put_opt(j, "note", v.note);
}
void from_json(const json& j, AppRuleRecord& v) {
    v.id = get_or<std::int64_t>(j, "id", 0);
    v.pattern = get_or<std::string>(j, "pattern", "");
    v.rule_type = get_or<AppRuleKind>(j, "ruleType", AppRuleKind::Allow);
    v.note = opt_str(j, "note");
    v.created_at_ms = get_or<std::int64_t>(j, "createdAtMs", 0);
    v.updated_at_ms = get_or<std::int64_t>(j, "updatedAtMs", 0);
}

// ---- UpsertAppRuleRequest --------------------------------------------------

void to_json(json& j, const UpsertAppRuleRequest& v) {
    j = json{{"pattern", v.pattern}, {"ruleType", v.rule_type}};
    put_opt(j, "note", v.note);
}
void from_json(const json& j, UpsertAppRuleRequest& v) {
    v.pattern = get_or<std::string>(j, "pattern", "");
    v.rule_type = get_or<AppRuleKind>(j, "ruleType", AppRuleKind::Allow);
    v.note = opt_str(j, "note");
}

// ---- LabelRequest ----------------------------------------------------------

void to_json(json& j, const LabelRequest& v) {
    j = json{{"sessionId", v.session_id}, {"label", v.label}};
    put_opt(j, "notes", v.notes);
    put_opt(j, "source", v.source);
}
void from_json(const json& j, LabelRequest& v) {
    v.session_id = get_or<std::string>(j, "sessionId", "");
    v.label = j.at("label").get<FocusLabel>();
    v.notes = opt_str(j, "notes");
    v.source = opt_str(j, "source");
}

// ---- ExportTrainingResult --------------------------------------------------

void to_json(json& j, const ExportTrainingResult& v) {
    j = json{{"outputDir", v.output_dir},
             {"featuresPath", v.features_path},
             {"labelsPath", v.labels_path},
             {"featureCount", v.feature_count},
             {"labelCount", v.label_count}};
}
void from_json(const json& j, ExportTrainingResult& v) {
    v.output_dir = get_or<std::string>(j, "outputDir", "");
    v.features_path = get_or<std::string>(j, "featuresPath", "");
    v.labels_path = get_or<std::string>(j, "labelsPath", "");
    v.feature_count = get_or<std::uint64_t>(j, "featureCount", 0);
    v.label_count = get_or<std::uint64_t>(j, "labelCount", 0);
}

// ---- AppSettings -----------------------------------------------------------

// Roadmap 2.13. Config and running state are separate objects in the file because they answer
// different questions: one is the rhythm the user chose, the other is where the timer got to.
// Restoring a rhythm should never depend on a stale deadline being parseable, and vice versa.
void to_json(json& j, const PomodoroConfig& v) {
    j = json{{"workMs", v.work_ms},
             {"shortBreakMs", v.short_break_ms},
             {"longBreakMs", v.long_break_ms},
             {"intervalsBeforeLongBreak", v.intervals_before_long_break},
             {"autoStartNextPhase", v.auto_start_next_phase}};
}
void from_json(const json& j, PomodoroConfig& v) {
    const PomodoroConfig d;
    // Each length is floored at one second. A zero or negative phase would end the instant it
    // began, and poll()'s catch-up loop would spin through phases without advancing time.
    const auto positive = [](std::int64_t value, std::int64_t fallback) {
        return value > 0 ? value : fallback;
    };
    v.work_ms = positive(get_or<std::int64_t>(j, "workMs", d.work_ms), d.work_ms);
    v.short_break_ms =
        positive(get_or<std::int64_t>(j, "shortBreakMs", d.short_break_ms), d.short_break_ms);
    v.long_break_ms =
        positive(get_or<std::int64_t>(j, "longBreakMs", d.long_break_ms), d.long_break_ms);
    // 0 is meaningful here — it disables long breaks — so only negatives fall back.
    const auto intervals =
        get_or<int>(j, "intervalsBeforeLongBreak", d.intervals_before_long_break);
    v.intervals_before_long_break = intervals >= 0 ? intervals : d.intervals_before_long_break;
    v.auto_start_next_phase = get_or<bool>(j, "autoStartNextPhase", d.auto_start_next_phase);
}

void to_json(json& j, const PomodoroSnapshot& v) {
    j = json{{"running", v.running},
             {"paused", v.paused},
             {"awaitingAcknowledgement", v.awaiting_acknowledgement},
             {"phase", pomodoro_phase_as_str(v.phase)},
             {"completedWorkIntervals", v.completed_work_intervals},
             {"deadlineWallMs", v.deadline_wall_ms},
             {"pausedRemainingMs", v.paused_remaining_ms}};
}
void from_json(const json& j, PomodoroSnapshot& v) {
    v.running = get_or<bool>(j, "running", false);
    v.paused = get_or<bool>(j, "paused", false);
    v.awaiting_acknowledgement = get_or<bool>(j, "awaitingAcknowledgement", false);
    v.phase = pomodoro_phase_from_string(get_or<std::string>(j, "phase", "work"));
    v.completed_work_intervals = std::max(0, get_or<int>(j, "completedWorkIntervals", 0));
    v.deadline_wall_ms = get_or<std::int64_t>(j, "deadlineWallMs", 0);
    v.paused_remaining_ms = std::max<std::int64_t>(0, get_or<std::int64_t>(j, "pausedRemainingMs", 0));
}

void to_json(json& j, const RecordingStatus& v) {
    j = json{{"state", recording_state_as_str(v.state)},
             {"privatePauseRemainingMs", v.private_pause_remaining_ms}};
}

void to_json(json& j, const AttendedProgress& v) {
    j = json{{"dailyTargetMins", v.daily_target_mins},
             {"dailyActualMins", v.daily_actual_mins},
             {"weeklyTargetMins", v.weekly_target_mins},
             {"weeklyActualMins", v.weekly_actual_mins}};
}

void to_json(json& j, const AppSettings& v) {
    j = json{{"defaultFocusMode", v.default_focus_mode},
             {"privateMode", v.private_mode},
             {"excludedApps", v.excluded_apps},
             {"goalCategories", v.goal_categories},
             {"idleThresholdSecs", v.idle_threshold_secs},
             {"pomodoro", v.pomodoro},
             {"pomodoroState", v.pomodoro_state},
             {"attendedTargetDailyMins", v.attended_target_daily_mins},
             {"attendedTargetWeeklyMins", v.attended_target_weekly_mins},
             {"privateUntilWallMs", v.private_until_wall_ms},
             {"untrackedNudgeUntilWallMs", v.untracked_nudge_until_wall_ms}};
}
void from_json(const json& j, AppSettings& v) {
    v.default_focus_mode = get_or<FocusMode>(j, "defaultFocusMode", FocusMode::Normal);
    v.private_mode = get_or<bool>(j, "privateMode", false);
    v.excluded_apps = get_or<std::vector<std::string>>(j, "excludedApps", {});
    v.goal_categories = get_or<std::vector<GoalCategory>>(j, "goalCategories", {});
    // A settings.json written before 7.23 has no such key, and a hand-edited one can hold
    // anything. Both land on the default rather than on a threshold that would disable idle
    // detection (<= 0) or pause a session mid-sentence.
    const auto threshold = get_or<std::int64_t>(j, "idleThresholdSecs", kDefaultIdleThresholdSecs);
    v.idle_threshold_secs =
        (threshold >= kMinIdleThresholdSecs && threshold <= kMaxIdleThresholdSecs)
            ? threshold
            : kDefaultIdleThresholdSecs;
    v.pomodoro = get_or<PomodoroConfig>(j, "pomodoro", PomodoroConfig{});
    v.pomodoro_state = get_or<PomodoroSnapshot>(j, "pomodoroState", PomodoroSnapshot{});
    v.attended_target_daily_mins = get_or<std::uint32_t>(j, "attendedTargetDailyMins", 0);
    v.attended_target_weekly_mins = get_or<std::uint32_t>(j, "attendedTargetWeeklyMins", 0);
    v.private_until_wall_ms = get_or<std::int64_t>(j, "privateUntilWallMs", 0);
    v.untracked_nudge_until_wall_ms =
        std::max<std::int64_t>(0, get_or<std::int64_t>(j, "untrackedNudgeUntilWallMs", 0));
}

void to_json(json& j, const PrivacySettings& v) {
    j = json{{"privateMode", v.private_mode},
             {"privateUntilWallMs", v.private_until_wall_ms},
             {"excludedApps", v.excluded_apps},
             {"localOnly", v.local_only}};
}

void to_json(json& j, const ActivityDeletionResult& v) {
    // `complete` is derived rather than stored, but it crosses the wire anyway: it is the one
    // field the UI branches on, and recomputing "failed is empty" on the frontend is how the
    // two sides drift apart.
    j = json{{"deleted", v.deleted},
             {"failed", v.failed},
             {"retained", v.retained},
             {"complete", v.complete()}};
}
void from_json(const json& j, ActivityDeletionResult& v) {
    v.deleted = get_or<std::vector<std::string>>(j, "deleted", {});
    v.failed = get_or<std::vector<std::string>>(j, "failed", {});
    v.retained = get_or<std::vector<std::string>>(j, "retained", {});
}
void from_json(const json& j, PrivacySettings& v) {
    v.private_mode = get_or<bool>(j, "privateMode", false);
    v.excluded_apps = get_or<std::vector<std::string>>(j, "excludedApps", {});
    v.local_only = get_or<bool>(j, "localOnly", true);
    v.private_until_wall_ms = get_or<std::int64_t>(j, "privateUntilWallMs", 0);
}

void to_json(json& j, const AnalyticsHour& v) {
    j = json{{"hour", v.hour},
             {"sampleCount", v.sample_count},
             {"avgFocusScore", v.avg_focus_score},
             {"distractedFraction", v.distracted_fraction}};
}
void from_json(const json& j, AnalyticsHour& v) {
    v.hour = get_or<int>(j, "hour", 0);
    v.sample_count = get_or<std::size_t>(j, "sampleCount", 0);
    v.avg_focus_score = get_or<double>(j, "avgFocusScore", 0.0);
    v.distracted_fraction = get_or<double>(j, "distractedFraction", 0.0);
}

void to_json(json& j, const AnalyticsApp& v) {
    j = json{{"appName", v.app_name}, {"windowCount", v.window_count}};
}
void from_json(const json& j, AnalyticsApp& v) {
    v.app_name = get_or<std::string>(j, "appName", "");
    v.window_count = get_or<std::size_t>(j, "windowCount", 0);
}

void to_json(json& j, const AnalyticsSummary& v) {
    j = json{{"sampleCount", v.sample_count},
             {"avgFocusScore", v.avg_focus_score},
             {"productiveSessionStreak", v.productive_session_streak},
             {"hourly", v.hourly},
             {"topApps", v.top_apps}};
}
void from_json(const json& j, AnalyticsSummary& v) {
    v.sample_count = get_or<std::size_t>(j, "sampleCount", 0);
    v.avg_focus_score = get_or<double>(j, "avgFocusScore", 0.0);
    v.productive_session_streak = get_or<std::size_t>(j, "productiveSessionStreak", 0);
    v.hourly = get_or<std::vector<AnalyticsHour>>(j, "hourly", {});
    v.top_apps = get_or<std::vector<AnalyticsApp>>(j, "topApps", {});
}

void to_json(json& j, const SummaryReport& v) {
    j = json{{"window", v.window},
             {"generatedAtMs", v.generated_at_ms},
             {"sessionCount", v.session_count},
             {"completedSessionCount", v.completed_session_count},
             {"focusSeconds", v.focus_seconds},
             {"sampleCount", v.sample_count},
             {"avgFocusScore", v.avg_focus_score},
             {"distractedFraction", v.distracted_fraction},
             {"longestFocusSecs", v.longest_focus_secs},
             {"topContextApp", v.top_context_app},
             {"attendedSeconds", v.attended_seconds},
             {"plannedMins", v.planned_mins}};
}
void from_json(const json& j, SummaryReport& v) {
    v.window = get_or<std::string>(j, "window", "day");
    v.generated_at_ms = get_or<std::int64_t>(j, "generatedAtMs", 0);
    v.session_count = get_or<std::size_t>(j, "sessionCount", 0);
    v.completed_session_count = get_or<std::size_t>(j, "completedSessionCount", 0);
    v.focus_seconds = get_or<std::uint64_t>(j, "focusSeconds", 0);
    v.sample_count = get_or<std::size_t>(j, "sampleCount", 0);
    v.avg_focus_score = get_or<double>(j, "avgFocusScore", 0.0);
    v.distracted_fraction = get_or<double>(j, "distractedFraction", 0.0);
    v.longest_focus_secs = get_or<std::uint64_t>(j, "longestFocusSecs", 0);
    v.top_context_app = get_or<std::string>(j, "topContextApp", "");
    v.attended_seconds = get_or<std::uint64_t>(j, "attendedSeconds", 0);
    v.planned_mins = get_or<std::uint32_t>(j, "plannedMins", 0);
}

void to_json(json& j, const SummaryExportResult& v) {
    j = json{{"window", v.window}, {"outputPath", v.output_path}};
}
void from_json(const json& j, SummaryExportResult& v) {
    v.window = get_or<std::string>(j, "window", "day");
    v.output_path = get_or<std::string>(j, "outputPath", "");
}

void to_json(json& j, const DiagnosticsSnapshot& v) {
    j = json{{"version", v.version}, {"health", v.health}, {"recentLogs", v.recent_logs}};
}
void from_json(const json& j, DiagnosticsSnapshot& v) {
    v.version = get_or<std::string>(j, "version", "0.0.0-dev");
    v.health = get_or<HealthStatus>(j, "health", {});
    v.recent_logs = get_or<std::vector<std::string>>(j, "recentLogs", {});
}

void to_json(json& j, const GoalCategory& v) {
    j = json{{"name", v.name}, {"keywords", v.keywords}};
}
void from_json(const json& j, GoalCategory& v) {
    v.name = get_or<std::string>(j, "name", "");
    v.keywords = get_or<std::vector<std::string>>(j, "keywords", {});
}

// ---- Failure payloads ------------------------------------------------------

void to_json(json& j, const CaptureFailurePayload& v) {
    j = json{{"reason", v.reason}, {"message", v.message}, {"setupSteps", v.setup_steps}};
}
void from_json(const json& j, CaptureFailurePayload& v) {
    v.reason = get_or<std::string>(j, "reason", "");
    v.message = get_or<std::string>(j, "message", "");
    v.setup_steps = get_or<std::vector<std::string>>(j, "setupSteps", {});
}

void to_json(json& j, const OverlayFailurePayload& v) {
    j = json{{"reason", v.reason}, {"message", v.message}};
}
void from_json(const json& j, OverlayFailurePayload& v) {
    v.reason = get_or<std::string>(j, "reason", "");
    v.message = get_or<std::string>(j, "message", "");
}

void to_json(json& j, const PersistenceFailurePayload& v) {
    j = json{{"reason", v.reason}, {"message", v.message}};
}
void from_json(const json& j, PersistenceFailurePayload& v) {
    v.reason = get_or<std::string>(j, "reason", "");
    v.message = get_or<std::string>(j, "message", "");
}

// ---- LabelHotkeyPayload ----------------------------------------------------

void to_json(json& j, const LabelHotkeyPayload& v) {
    j = json{{"ok", v.ok}, {"message", v.message}};
    if (v.label) j["label"] = *v.label;
    else j["label"] = nullptr;
    put_opt(j, "sessionId", v.session_id);
}
void from_json(const json& j, LabelHotkeyPayload& v) {
    v.ok = get_or<bool>(j, "ok", false);
    v.message = get_or<std::string>(j, "message", "");
    auto it = j.find("label");
    if (it != j.end() && !it->is_null()) v.label = it->get<FocusLabel>();
    else v.label = std::nullopt;
    v.session_id = opt_str(j, "sessionId");
}

// ---- FocusTargetResult -----------------------------------------------------

void to_json(json& j, const FocusTargetResult& v) {
    j = json{{"ok", v.ok}, {"message", v.message}};
}
void from_json(const json& j, FocusTargetResult& v) {
    v.ok = get_or<bool>(j, "ok", false);
    v.message = get_or<std::string>(j, "message", "");
}

// ---- FileDialog ------------------------------------------------------------

void to_json(json& j, const FileDialogFilter& v) {
    j = json{{"name", v.name}, {"pattern", v.pattern}};
}
void from_json(const json& j, FileDialogFilter& v) {
    v.name = get_or<std::string>(j, "name", "");
    v.pattern = get_or<std::string>(j, "pattern", "");
}

void to_json(json& j, const FileDialogOptions& v) {
    j = json{{"title", v.title},
             {"defaultPath", v.default_path},
             {"defaultName", v.default_name},
             {"filters", v.filters}};
}
void from_json(const json& j, FileDialogOptions& v) {
    v.title = get_or<std::string>(j, "title", "");
    v.default_path = get_or<std::string>(j, "defaultPath", "");
    v.default_name = get_or<std::string>(j, "defaultName", "");
    v.filters = get_or<std::vector<FileDialogFilter>>(j, "filters", {});
}

void to_json(json& j, const FileDialogResult& v) {
    j = json{{"ok", v.ok},
             {"cancelled", v.cancelled},
             {"path", v.path},
             {"message", v.message}};
}
void from_json(const json& j, FileDialogResult& v) {
    v.ok = get_or<bool>(j, "ok", false);
    v.cancelled = get_or<bool>(j, "cancelled", false);
    v.path = get_or<std::string>(j, "path", "");
    v.message = get_or<std::string>(j, "message", "");
}

}  // namespace snapback


