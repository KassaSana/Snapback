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

// null when absent, so Option<String> round-trips as JSON null (matches serde).
void put_opt(json& j, const char* key, const std::optional<std::string>& v) {
    if (v) j[key] = *v;
    else j[key] = nullptr;
}

std::optional<std::string> opt_str(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    return it->get<std::string>();
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
    return FocusMode::Normal;  // unknown -> Normal, per Rust from_str
}

std::optional<AppRuleKind> app_rule_kind_from_string(const std::string& s) {
    const std::string l = to_lower(s);
    if (l == "allow") return AppRuleKind::Allow;
    if (l == "block") return AppRuleKind::Block;
    return std::nullopt;  // unknown -> None (not a default), per Rust from_str
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
             {"timestamp", v.timestamp}};
}
void from_json(const json& j, PredictionRecord& v) {
    v.session_id = get_or<std::string>(j, "sessionId", "");
    v.focus_score = get_or<double>(j, "focusScore", 0.0);
    v.distraction_risk = get_or<double>(j, "distractionRisk", 0.0);
    v.focus_state = get_or<std::string>(j, "focusState", "");
    v.thrash_score = get_or<double>(j, "thrashScore", 0.0);
    v.drift_score = get_or<double>(j, "driftScore", 0.0);
    v.goal_alignment = get_or<double>(j, "goalAlignment", 0.5);
    v.timestamp = get_or<std::string>(j, "timestamp", "");
}

// ---- SessionRecord ---------------------------------------------------------

void to_json(json& j, const SessionRecord& v) {
    j = json{{"sessionId", v.session_id},
             {"goal", v.goal},
             {"status", v.status},
             {"focusMode", v.focus_mode}};
    put_opt(j, "startedAt", v.started_at);
    put_opt(j, "endedAt", v.ended_at);
}
void from_json(const json& j, SessionRecord& v) {
    v.session_id = get_or<std::string>(j, "sessionId", "");
    v.goal = get_or<std::string>(j, "goal", "");
    v.status = get_or<std::string>(j, "status", "");
    v.focus_mode = get_or<std::string>(j, "focusMode", "normal");
    v.started_at = opt_str(j, "startedAt");
    v.ended_at = opt_str(j, "endedAt");
}

// ---- SessionRecap ----------------------------------------------------------

void to_json(json& j, const SessionRecap& v) {
    j = json{{"sessionId", v.session_id},
             {"goal", v.goal},
             {"durationSecs", v.duration_secs},
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
}
void from_json(const json& j, ClassifierStatus& v) {
    v.backend = get_or<std::string>(j, "backend", "heuristic");
    v.onnx_runtime_enabled = get_or<bool>(j, "onnxRuntimeEnabled", false);
    v.model_path = opt_str(j, "modelPath");
}

// ---- HealthStatus ----------------------------------------------------------

void to_json(json& j, const HealthStatus& v) {
    j = json{{"status", v.status},
             {"captureRunning", v.capture_running},
             {"captureFailed", v.capture_failed},
             {"captureEventsDropped", v.capture_events_dropped},
             {"captureStalled", v.capture_stalled},
             {"permissions", v.permissions},
             {"classifier", v.classifier}};
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
    v.permissions = get_or<PermissionStatus>(j, "permissions", {});
    v.classifier = get_or<ClassifierStatus>(j, "classifier", {});
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
             {"timestamp", v.timestamp}};
}
void from_json(const json& j, ContextSnapshotDto& v) {
    v.app_name = get_or<std::string>(j, "appName", "");
    v.window_title = get_or<std::string>(j, "windowTitle", "");
    v.file_hint = get_or<std::string>(j, "fileHint", "");
    v.project_hint = get_or<std::string>(j, "projectHint", "");
    v.summary = get_or<std::string>(j, "summary", "");
    v.timestamp = get_or<std::string>(j, "timestamp", "");
}

// ---- AppRuleRecord ---------------------------------------------------------

void to_json(json& j, const AppRuleRecord& v) {
    j = json{{"id", v.id},
             {"pattern", v.pattern},
             {"ruleType", v.rule_type},
             {"createdAt", v.created_at},
             {"updatedAt", v.updated_at}};
    put_opt(j, "note", v.note);
}
void from_json(const json& j, AppRuleRecord& v) {
    v.id = get_or<std::int64_t>(j, "id", 0);
    v.pattern = get_or<std::string>(j, "pattern", "");
    v.rule_type = get_or<AppRuleKind>(j, "ruleType", AppRuleKind::Allow);
    v.note = opt_str(j, "note");
    v.created_at = get_or<std::string>(j, "createdAt", "");
    v.updated_at = get_or<std::string>(j, "updatedAt", "");
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

void to_json(json& j, const AppSettings& v) {
    j = json{{"defaultFocusMode", v.default_focus_mode}};
}
void from_json(const json& j, AppSettings& v) {
    v.default_focus_mode = get_or<FocusMode>(j, "defaultFocusMode", FocusMode::Normal);
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

}  // namespace snapback
