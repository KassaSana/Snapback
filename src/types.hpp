// Shared data model. Mirrors ../Snapback/src-tauri/src/types.rs.
//
// WIRE FORMAT: in Rust every DTO below carries #[serde(rename_all = "camelCase")],
// so the JSON the frontend sees is camelCase (e.g. focusScore, sessionId). We match
// that byte-for-byte here. The one internal exception is CaptureEvent, which has no
// serde rename in Rust and stays snake_case (it never crosses to the frontend).
//
// Enums serialize to fixed strings: EventType/FocusLabel = SCREAMING_SNAKE_CASE,
// FocusMode/AppRuleKind = lowercase.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace snapback {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Enums (+ string conversions used by command handling and tests)
// ---------------------------------------------------------------------------

// Rust: EventType. On-wire strings are SCREAMING_SNAKE_CASE; numeric values match
// the Rust discriminants (used as the SQLite event code).
enum class EventType : int {
    KeyPress = 1,
    KeyRelease = 2,
    MouseMove = 3,
    MouseClick = 4,
    WindowFocusChange = 5,
    WindowTitleChange = 6,
    IdleStart = 7,
    IdleEnd = 8,
};
NLOHMANN_JSON_SERIALIZE_ENUM(EventType, {
    {EventType::KeyPress, "KEY_PRESS"},
    {EventType::KeyRelease, "KEY_RELEASE"},
    {EventType::MouseMove, "MOUSE_MOVE"},
    {EventType::MouseClick, "MOUSE_CLICK"},
    {EventType::WindowFocusChange, "WINDOW_FOCUS_CHANGE"},
    {EventType::WindowTitleChange, "WINDOW_TITLE_CHANGE"},
    {EventType::IdleStart, "IDLE_START"},
    {EventType::IdleEnd, "IDLE_END"},
})

// Rust: FocusLabel. Note Distracted = -1 (the label stored in SQLite).
enum class FocusLabel : int {
    Distracted = -1,
    PseudoProductive = 0,
    Productive = 1,
    DeepFocus = 2,
};
NLOHMANN_JSON_SERIALIZE_ENUM(FocusLabel, {
    {FocusLabel::Distracted, "DISTRACTED"},
    {FocusLabel::PseudoProductive, "PSEUDO_PRODUCTIVE"},
    {FocusLabel::Productive, "PRODUCTIVE"},
    {FocusLabel::DeepFocus, "DEEP_FOCUS"},
})

// Rust: FocusMode (lowercase on the wire). Thresholds ported verbatim from types.rs.
enum class FocusMode { Deep, Normal, Recovery };
// Normal is listed first so nlohmann's enum from_json falls back to it on an
// unknown string — matching Rust FocusMode::from_str (unknown -> Normal).
NLOHMANN_JSON_SERIALIZE_ENUM(FocusMode, {
    {FocusMode::Normal, "normal"},
    {FocusMode::Deep, "deep"},
    {FocusMode::Recovery, "recovery"},
})

inline const char* focus_mode_to_string(FocusMode m) {
    switch (m) {
        case FocusMode::Deep: return "deep";
        case FocusMode::Recovery: return "recovery";
        case FocusMode::Normal: default: return "normal";
    }
}

// Rust: FocusMode::from_str — case-insensitive, unknown -> Normal.
FocusMode focus_mode_from_string(const std::string& s);

inline double risk_threshold(FocusMode m) {
    switch (m) {
        case FocusMode::Deep: return 0.55;
        case FocusMode::Recovery: return 0.85;
        case FocusMode::Normal: default: return 0.70;
    }
}

inline std::uint32_t hyperfocus_minutes(FocusMode m) {
    switch (m) {
        case FocusMode::Deep: return 90;
        case FocusMode::Recovery: return 45;
        case FocusMode::Normal: default: return 120;
    }
}

// Rust: AppRuleKind (lowercase). from_str returns None on unknown (NOT a default) —
// so we expose an optional parse rather than a lossy fallback.
enum class AppRuleKind { Allow, Block };
NLOHMANN_JSON_SERIALIZE_ENUM(AppRuleKind, {
    {AppRuleKind::Allow, "allow"},
    {AppRuleKind::Block, "block"},
})
inline const char* app_rule_kind_to_string(AppRuleKind k) {
    return k == AppRuleKind::Allow ? "allow" : "block";
}
std::optional<AppRuleKind> app_rule_kind_from_string(const std::string& s);

// Rust: LabelSource. No serde derive there; it round-trips through as_str/parse.
enum class LabelSource { Manual, Hotkey, Survey, Auto };
const char* label_source_as_str(LabelSource s);
LabelSource label_source_parse(const std::optional<std::string>& value);

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

// Rust: CaptureEvent — internal capture->engine record. snake_case wire (no rename).
struct CaptureEvent {
    EventType event_type{EventType::KeyPress};
    double timestamp_secs{};
    std::string app_name;
    std::string window_title;
    std::int32_t mouse_x{};
    std::int32_t mouse_y{};
    std::uint32_t mouse_speed{};
    std::uint32_t idle_duration_ms{};
};

// Rust: PredictionRecord (camelCase).
struct PredictionRecord {
    std::string session_id;
    double focus_score{};
    double distraction_risk{};
    std::string focus_state;
    double thrash_score{};
    double drift_score{};
    double goal_alignment{0.5};
    std::string timestamp;
};

// Rust: SessionRecord. Note focus_mode is a plain string here (not the enum).
struct SessionRecord {
    std::string session_id;
    std::string goal;
    std::string status;
    std::string focus_mode;
    std::optional<std::string> started_at;
    std::optional<std::string> ended_at;
};

// Rust: SessionRecap.
struct SessionRecap {
    std::string session_id;
    std::string goal;
    std::uint64_t duration_secs{};
    double avg_focus_score{};
    double avg_distraction_risk{};
    std::uint32_t snapback_count{};
    std::uint32_t thrash_spikes{};
    double deep_focus_pct{};
};

// Rust: SessionSummary — a past session plus its computed recap.
struct SessionSummary {
    SessionRecord record;
    SessionRecap recap;
};

// Rust: PermissionStatus.
struct PermissionStatus {
    bool capture_available{};
    bool capture_probe_confirmed{};
    bool active_window_available{};
    std::string message;
    std::vector<std::string> setup_steps;
};

// Rust: ClassifierStatus.
struct ClassifierStatus {
    std::string backend{"heuristic"};
    bool onnx_runtime_enabled{};
    std::optional<std::string> model_path;
};

// Rust: HealthStatus — nests PermissionStatus + ClassifierStatus as objects.
struct HealthStatus {
    std::string status;
    bool capture_running{};
    bool capture_failed{};
    std::optional<std::string> capture_failure_reason;
    std::optional<std::string> overlay_failure_reason;
    std::optional<std::string> persistence_failure_reason;
    std::uint64_t capture_events_dropped{};
    bool capture_stalled{};
    PermissionStatus permissions;
    ClassifierStatus classifier;
};

// Rust: SnapbackPayload — what the overlay/dashboard show on return-from-distraction.
struct SnapbackPayload {
    std::string summary;
    std::string app_name;
    std::string window_title;
    std::string file_hint;
    std::uint32_t distraction_duration_secs{};
};

// Rust: ContextSnapshotDto.
struct ContextSnapshotDto {
    std::string app_name;
    std::string window_title;
    std::string file_hint;
    std::string project_hint;
    std::string summary;
    std::string timestamp;
};

// Rust: AppRuleRecord.
struct AppRuleRecord {
    std::int64_t id{};
    std::string pattern;
    AppRuleKind rule_type{AppRuleKind::Allow};
    std::optional<std::string> note;
    std::string created_at;
    std::string updated_at;
};

// Rust: UpsertAppRuleRequest.
struct UpsertAppRuleRequest {
    std::string pattern;
    AppRuleKind rule_type{AppRuleKind::Allow};
    std::optional<std::string> note;
};

// Rust: LabelRequest (the nested `request` arg of submit_label).
struct LabelRequest {
    std::string session_id;
    FocusLabel label{FocusLabel::Productive};
    std::optional<std::string> notes;
    std::optional<std::string> source;
};

// Rust: ExportTrainingResult.
struct ExportTrainingResult {
    std::string output_dir;
    std::string features_path;
    std::string labels_path;
    std::uint64_t feature_count{};
    std::uint64_t label_count{};
};

struct GoalCategory {
    std::string name;
    std::vector<std::string> keywords;
};

// New C++ settings DTO. Persisted in app-data/settings.json and exposed to the
// frontend with camelCase keys.
struct AppSettings {
    FocusMode default_focus_mode{FocusMode::Normal};
    bool private_mode{};
    std::vector<std::string> excluded_apps;
    std::vector<GoalCategory> goal_categories;
};

struct PrivacySettings {
    bool private_mode{};
    std::vector<std::string> excluded_apps;
    bool local_only{true};
};

struct AnalyticsHour {
    int hour{};
    std::size_t sample_count{};
    double avg_focus_score{};
    double distracted_fraction{};
};

struct AnalyticsApp {
    std::string app_name;
    std::size_t window_count{};
};

struct AnalyticsSummary {
    std::size_t sample_count{};
    double avg_focus_score{};
    std::size_t productive_session_streak{};
    std::vector<AnalyticsHour> hourly;
    std::vector<AnalyticsApp> top_apps;
};

struct SummaryReport {
    std::string window;
    std::string generated_at;
    std::size_t session_count{};
    std::uint64_t focus_seconds{};
    std::size_t sample_count{};
    double avg_focus_score{};
    double distracted_fraction{};
    std::size_t longest_focus_streak{};
    std::string top_context_app;
};

struct SummaryExportResult {
    std::string window;
    std::string output_path;
};

struct DiagnosticsSnapshot {
    HealthStatus health;
    std::vector<std::string> recent_logs;
};

// Rust: CaptureFailurePayload / OverlayFailurePayload / PersistenceFailurePayload.
struct CaptureFailurePayload {
    std::string reason;
    std::string message;
    std::vector<std::string> setup_steps;
};
struct OverlayFailurePayload {
    std::string reason;
    std::string message;
};
struct PersistenceFailurePayload {
    std::string reason;
    std::string message;
};

// Emitted on the `label-hotkey` event. sessionId stays camelCase (frontend reads it
// camelCase-only here, unlike the tolerant mappers elsewhere).
struct LabelHotkeyPayload {
    bool ok{};
    std::string message;
    std::optional<FocusLabel> label;
    std::optional<std::string> session_id;
};

// ---------------------------------------------------------------------------
// JSON (de)serialization — camelCase keys. Defined in types.cpp.
// ---------------------------------------------------------------------------
void to_json(json& j, const CaptureEvent& v);
void from_json(const json& j, CaptureEvent& v);
void to_json(json& j, const PredictionRecord& v);
void from_json(const json& j, PredictionRecord& v);
void to_json(json& j, const SessionRecord& v);
void from_json(const json& j, SessionRecord& v);
void to_json(json& j, const SessionRecap& v);
void from_json(const json& j, SessionRecap& v);
void to_json(json& j, const SessionSummary& v);
void from_json(const json& j, SessionSummary& v);
void to_json(json& j, const PermissionStatus& v);
void from_json(const json& j, PermissionStatus& v);
void to_json(json& j, const ClassifierStatus& v);
void from_json(const json& j, ClassifierStatus& v);
void to_json(json& j, const HealthStatus& v);
void from_json(const json& j, HealthStatus& v);
void to_json(json& j, const SnapbackPayload& v);
void from_json(const json& j, SnapbackPayload& v);
void to_json(json& j, const ContextSnapshotDto& v);
void from_json(const json& j, ContextSnapshotDto& v);
void to_json(json& j, const AppRuleRecord& v);
void from_json(const json& j, AppRuleRecord& v);
void to_json(json& j, const UpsertAppRuleRequest& v);
void from_json(const json& j, UpsertAppRuleRequest& v);
void to_json(json& j, const LabelRequest& v);
void from_json(const json& j, LabelRequest& v);
void to_json(json& j, const ExportTrainingResult& v);
void from_json(const json& j, ExportTrainingResult& v);
void to_json(json& j, const AppSettings& v);
void from_json(const json& j, AppSettings& v);
void to_json(json& j, const PrivacySettings& v);
void from_json(const json& j, PrivacySettings& v);
void to_json(json& j, const AnalyticsHour& v);
void from_json(const json& j, AnalyticsHour& v);
void to_json(json& j, const AnalyticsApp& v);
void from_json(const json& j, AnalyticsApp& v);
void to_json(json& j, const AnalyticsSummary& v);
void from_json(const json& j, AnalyticsSummary& v);
void to_json(json& j, const SummaryReport& v);
void from_json(const json& j, SummaryReport& v);
void to_json(json& j, const SummaryExportResult& v);
void from_json(const json& j, SummaryExportResult& v);
void to_json(json& j, const DiagnosticsSnapshot& v);
void from_json(const json& j, DiagnosticsSnapshot& v);
void to_json(json& j, const GoalCategory& v);
void from_json(const json& j, GoalCategory& v);
void to_json(json& j, const CaptureFailurePayload& v);
void from_json(const json& j, CaptureFailurePayload& v);
void to_json(json& j, const OverlayFailurePayload& v);
void from_json(const json& j, OverlayFailurePayload& v);
void to_json(json& j, const PersistenceFailurePayload& v);
void from_json(const json& j, PersistenceFailurePayload& v);
void to_json(json& j, const LabelHotkeyPayload& v);
void from_json(const json& j, LabelHotkeyPayload& v);

}  // namespace snapback
