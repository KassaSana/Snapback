// Shared data model and JSON wire format used by the native app and frontend.
//
// Frontend-facing DTOs use camelCase (e.g. focusScore, sessionId). Internal-only
// CaptureEvent values stay snake_case and never cross to the frontend.
//
// Enums serialize to fixed strings: EventType/FocusLabel = SCREAMING_SNAKE_CASE,
// FocusMode/AppRuleKind = lowercase.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// PomodoroConfig/PomodoroSnapshot are part of AppSettings below. pomodoro.hpp is a leaf —
// it includes only <cstdint> and json_fwd — so this cannot cycle back into types.hpp.
#include "engine/pomodoro.hpp"

namespace snapback {

using json = nlohmann::json;

// Serializes to JSON text, replacing invalid UTF-8 with U+FFFD instead of throwing. Use for any
// string that originates outside the app (e.g. X11 window titles are arbitrary bytes).
inline std::string dump_json(const json& value) {
    return value.dump(-1, ' ', /*ensure_ascii=*/false, json::error_handler_t::replace);
}

// ---------------------------------------------------------------------------
// Enums (+ string conversions used by command handling and tests)
// ---------------------------------------------------------------------------

// EventType. On-wire strings are SCREAMING_SNAKE_CASE; numeric values are used as
// the SQLite event code.
enum class EventType : int {
    KeyPress = 1,
    KeyRelease = 2,
    MouseMove = 3,
    MouseClick = 4,
    WindowFocusChange = 5,
    WindowTitleChange = 6,
    IdleStart = 7,
    IdleEnd = 8,
    MouseScroll = 9,
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
    {EventType::MouseScroll, "mouse_scroll"},
})

// FocusLabel. Note Distracted = -1 (the label stored in SQLite).
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

// FocusMode (lowercase on the wire).
enum class FocusMode { Deep, Normal, Recovery };
// Normal is listed first so nlohmann's enum from_json falls back to it on an
// unknown string maps to Normal.
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

// Case-insensitive parser; unknown values map to Normal.
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

// AppRuleKind (lowercase). Unknown values are rejected rather than defaulted —
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

// LabelSource. It round-trips through as_str/parse.
enum class LabelSource { Manual, Hotkey, Survey, Auto };
const char* label_source_as_str(LabelSource s);
LabelSource label_source_parse(const std::optional<std::string>& value);

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

// Immutable context shared by hot-path input events. Platform hooks replace this only
// when the foreground window changes, so keyboard/mouse callbacks can hand context to the
// capture queue without allocating or copying strings.
struct CaptureContext {
    std::string app_name;
    std::string window_title;
};

// CaptureEvent — internal capture-to-engine record. snake_case wire.
struct CaptureEvent {
    EventType event_type{EventType::KeyPress};
    // MONOTONIC seconds from an uptime clock. Owns durations, ordering, and the rolling
    // windows. Never pass it to a calendar function.
    double timestamp_secs{};
    // Wall-clock epoch seconds for calendar features. 0 means "not supplied": the extractor
    // then falls back to timestamp_secs, which the feature-parity fixtures rely on.
    double wall_clock_secs{};
    std::string app_name;
    std::string window_title;
    std::int32_t mouse_x{};
    std::int32_t mouse_y{};
    std::uint32_t mouse_speed{};
    std::uint32_t idle_duration_ms{};
    // Internal-only producer representation. CaptureThread resolves it into the string
    // fields after dequeueing, on the engine side of the OS callback boundary.
    std::shared_ptr<const CaptureContext> captured_context;

    void materialize_captured_context() {
        if (!captured_context) return;
        app_name = captured_context->app_name;
        window_title = captured_context->window_title;
        captured_context.reset();
    }
};

// PredictionRecord (camelCase).
struct PredictionRecord {
    std::string session_id;
    double focus_score{};
    double distraction_risk{};
    std::string focus_state;
    double thrash_score{};
    double drift_score{};
    double goal_alignment{0.5};
    // UTC epoch milliseconds (ADR-0007).
    std::int64_t timestamp_ms{};
    std::string model_id{"heuristic:snapback-features-v1-31"};
    // Which rule decided focus_state (ADR-0004): 'model', 'risk', 'thrash', 'block', or
    // 'drift'. nullopt means unknown (rows written before provenance existed).
    std::optional<std::string> state_source;
};

// SessionRecord. Note focus_mode is a plain string here (not the enum).
struct SessionRecord {
    std::string session_id;
    std::string goal;
    std::string status;
    std::string focus_mode;
    // nullopt on ended_at_ms means the session is still running -- the same load-bearing NULL
    // the schema keeps.
    std::optional<std::int64_t> started_at_ms;
    std::optional<std::int64_t> ended_at_ms;
    // The user's own end-of-session note, separate from the focus label. nullopt means
    // unanswered; Skip writes nothing.
    std::optional<std::string> reflection_done;
    std::optional<std::string> reflection_next_step;
};

// SessionRecap.
struct SessionRecap {
    std::string session_id;
    std::string goal;
    // Wall clock from start to end, including time the user was away.
    std::uint64_t duration_secs{};
    // Time actually present, summed from session_spans (ADR-0005). nullopt means never measured
    // (not zero), so readers fall back to duration_secs.
    std::optional<std::uint64_t> active_secs;
    double avg_focus_score{};
    double avg_distraction_risk{};
    std::uint32_t snapback_count{};
    std::uint32_t thrash_spikes{};
    double deep_focus_pct{};
};

// SessionSummary — a past session plus its computed recap.
struct SessionSummary {
    SessionRecord record;
    SessionRecap recap;
};

// PermissionStatus.
struct PermissionStatus {
    bool capture_available{};
    bool capture_probe_confirmed{};
    bool active_window_available{};
    std::string message;
    std::vector<std::string> setup_steps;
};

// ClassifierStatus.
struct ClassifierStatus {
    // The backend that made the most recent prediction; a failed ONNX inference falls back to
    // the heuristic and reports "heuristic".
    std::string backend{"heuristic"};
    bool onnx_runtime_enabled{};
    std::optional<std::string> model_path;
    std::optional<std::string> model_id;
    // A model is loaded but its last inference failed or was rejected by the output
    // validator; the classifier is running on the heuristic until one succeeds.
    bool inference_degraded{};
    std::uint64_t inference_failures{};
};

// Optional ONNX deployment recovery state. When cleanup debris cannot be removed at startup the
// app still opens on the heuristic; this names what was preserved and whether retry or rollback
// is available.
struct ModelDeploymentHealth {
    std::string state{"ok"};  // "ok" | "degraded"
    std::optional<std::string> message;
    std::vector<std::string> preserved_paths;
    bool retry_cleanup_available{};
    bool rollback_available{};
};

// What the process is costing itself, carried in HealthStatus so it reaches support bundles.
// Percentiles are histogram upper bounds (see ranked_mutex.hpp), not exact values; the exact
// tail is the max_* field.
struct RuntimeMetrics {
    // Tick-loop iterations; the denominator for the CPU figure.
    std::uint64_t persistence_failures{};
    std::uint64_t persistence_dropped_predictions{};
    std::uint64_t engine_wakeups{};
    // User + kernel milliseconds for the whole process since it started. 0 where the platform
    // call failed, or where the process has not yet burned one scheduler tick.
    std::uint64_t process_cpu_ms{};
    // How deep the capture ring ever got, against what it holds. `capture_events_dropped`
    // above says it overflowed; this says how much margin there was before it did.
    std::uint64_t capture_ring_high_water{};
    std::uint64_t capture_ring_capacity{};
    // storage_mutex_, shared by storage-backed UI reads and the engine's persist phase.
    std::uint64_t storage_lock_acquisitions{};
    std::uint64_t storage_lock_contended{};
    std::uint64_t storage_lock_hold_p50_us{};
    std::uint64_t storage_lock_hold_p95_us{};
    std::uint64_t storage_lock_max_hold_us{};
    std::uint64_t storage_lock_wait_p95_us{};
    std::uint64_t storage_lock_max_wait_us{};
    // SQLITE_BUSY pressure and failure. An exhausted wait is a discarded persistence batch.
    std::uint64_t sqlite_busy_waits{};
    std::uint64_t sqlite_busy_exhausted{};
    std::uint64_t sqlite_busy_max_wait_ms{};
};

// HealthStatus — nests PermissionStatus + ClassifierStatus as objects.
struct HealthStatus {
    std::string status;
    bool capture_running{};
    bool capture_failed{};
    std::optional<std::string> capture_failure_reason;
    std::optional<std::string> overlay_failure_reason;
    std::optional<std::string> persistence_failure_reason;
    std::uint64_t capture_events_dropped{};
    bool capture_stalled{};
    std::optional<double> last_prediction_age_secs;
    std::string prediction_suppression_reason{"none"};
    PermissionStatus permissions;
    ClassifierStatus classifier;
    ModelDeploymentHealth model_deployment;
    RuntimeMetrics runtime;
    // ADR-0006. True in Debug, or Release with SNAPBACK_DEV_TRAINING set.
    bool developer_tools_enabled{};
};

// One recorded distraction: the user left focused work and came back. The durable counterpart
// to SnapbackPayload. (session_id, started_at) is UNIQUE, so retries cannot double-count.
struct SnapbackEpisode {
    std::string session_id;
    // The route back, exactly as it was offered to the user.
    std::string summary;
    // Where they were before the distraction. The distracting app is deliberately not stored,
    // so this never becomes a browsing history.
    std::string app_name;
    std::string file_hint;
    // nullopt for legacy rows with no start. Optional rather than a sentinel, because the
    // UNIQUE index needs those rows to stay distinct.
    std::optional<std::int64_t> started_at_ms;  // when the distraction began
    std::int64_t ended_at_ms{};  // when they returned; the pre-existing `timestamp` column
    std::uint32_t duration_secs{};
};

// SnapbackPayload — what the overlay/dashboard show on return-from-distraction.
struct SnapbackPayload {
    std::string summary;
    std::string app_name;
    std::string window_title;
    std::string file_hint;
    std::uint32_t distraction_duration_secs{};
};

// ContextSnapshotDto.
struct ContextSnapshotDto {
    std::string app_name;
    std::string window_title;
    std::string file_hint;
    std::string project_hint;
    std::string summary;
    std::int64_t timestamp_ms{};
};

// AppRuleRecord.
struct AppRuleRecord {
    std::int64_t id{};
    std::string pattern;
    AppRuleKind rule_type{AppRuleKind::Allow};
    std::optional<std::string> note;
    std::int64_t created_at_ms{};
    std::int64_t updated_at_ms{};
};

// UpsertAppRuleRequest.
struct UpsertAppRuleRequest {
    std::string pattern;
    AppRuleKind rule_type{AppRuleKind::Allow};
    std::optional<std::string> note;
};

// LabelRequest (the nested `request` arg of submit_label).
struct LabelRequest {
    std::string session_id;
    FocusLabel label{FocusLabel::Productive};
    std::optional<std::string> notes;
    std::optional<std::string> source;
};

// ExportTrainingResult.
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

// Bounds on the user-chosen AFK threshold. Out-of-range values are rejected, not clamped.
inline constexpr std::int64_t kDefaultIdleThresholdSecs = 300;
inline constexpr std::int64_t kMinIdleThresholdSecs = 30;
inline constexpr std::int64_t kMaxIdleThresholdSecs = 3600;

// How long a tray snooze silences delivery, and the bound on any value reaching the setter.
inline constexpr std::int64_t kDefaultAlertSnoozeMins = 30;
inline constexpr std::int64_t kMaxAlertSnoozeMins = 24 * 60;
inline constexpr int kMinutesPerDay = 1440;

// Which channels one kind of interruption may use. Independent flags so any combination,
// including none, is expressible.
struct AlertChannels {
    bool in_app{};
    bool overlay{};
    bool native{};

    bool any() const { return in_app || overlay || native; }
};

// How much a native notification may say. Only the native channel has this axis: toasts are
// kept in OS history and shown on the lock screen.
enum class AlertPreviewMode { Detailed, Generic };

// Detailed first, so an unknown string falls back to it.
NLOHMANN_JSON_SERIALIZE_ENUM(AlertPreviewMode, {
    {AlertPreviewMode::Detailed, "detailed"},
    {AlertPreviewMode::Generic, "generic"},
})

// When and how Snapback may interrupt.
struct AlertDeliverySettings {
    // One visible intervention per event: the overlay already reaches the user, and a toast
    // would also copy the summary into OS history.
    AlertChannels snapback{false, true, false};
    // Native only, preserving today's behaviour. An overlay here would interrupt exactly the
    // deep work the hyperfocus guardrail exists to protect.
    AlertChannels hyperfocus{false, false, true};
    // In-app only, preserving today's behaviour: the Pomodoro card is on screen when a
    // Pomodoro is running, so it is already where the user is looking.
    AlertChannels pomodoro{true, false, false};

    AlertPreviewMode preview{AlertPreviewMode::Detailed};

    // Local minutes since midnight, 0..1439 -- a reading, not an instant (see
    // local_minute_of_day_from_unix_ms). Off by default.
    bool quiet_hours_enabled{};
    std::int32_t quiet_hours_start_min{22 * 60};
    std::int32_t quiet_hours_end_min{7 * 60};

    // Unix ms at which a tray snooze lapses; 0 when not snoozed. A deadline, not a duration, so
    // closing the window does not restart it. Not privacy mode: recording continues, only
    // delivery is silenced.
    std::int64_t snoozed_until_wall_ms{};
};

// New C++ settings DTO. Persisted in app-data/settings.json and exposed to the
// frontend with camelCase keys.
struct AppSettings {
    FocusMode default_focus_mode{FocusMode::Normal};
    bool private_mode{};
    std::vector<std::string> excluded_apps;
    std::vector<GoalCategory> goal_categories;
    // Seconds without input before a session counts as unattended (ADR-0005).
    std::int64_t idle_threshold_secs{kDefaultIdleThresholdSecs};
    // Phase lengths, long-break cadence, and auto-start.
    PomodoroConfig pomodoro{};
    // The running timer, persisted so a relaunch resumes it.
    PomodoroSnapshot pomodoro_state{};
    // Opt-in attended-minute targets; 0 means no target.
    std::uint32_t attended_target_daily_mins{};
    std::uint32_t attended_target_weekly_mins{};
    // Unix ms at which a timed privacy pause lapses; 0 when off or indefinite. Persisted so the
    // pause survives a restart.
    std::int64_t private_until_wall_ms{};
    // A dismissed untracked-work prompt stays dismissed across a restart.
    std::int64_t untracked_nudge_until_wall_ms{};
    AlertDeliverySettings alerts{};
    // Whether the one-time "closing left Snapback in the tray" notice has been shown.
    bool tray_close_notice_shown{};
};

// A plan and what actually happened, in minutes. A target of 0 means "not set".
struct AttendedProgress {
    std::uint32_t daily_target_mins{};
    std::uint64_t daily_actual_mins{};
    std::uint32_t weekly_target_mins{};
    std::uint64_t weekly_actual_mins{};
};

// What "Delete all activity" did. The operation can half-succeed (e.g. an export held open by
// another program), so each target is reported.
struct ActivityDeletionResult {
    // Activity-bearing artifacts that are now gone. Absence counts as removed: an export that
    // was never created is not a copy of anything.
    std::vector<std::string> deleted;
    // Activity-bearing artifacts that could not be removed, each with the reason. Non-empty
    // means the answer is "most of it", and the UI must say so.
    std::vector<std::string> failed;
    // Configuration deliberately kept, listed so the decision is visible.
    std::vector<std::string> retained;

    [[nodiscard]] bool complete() const { return failed.empty(); }
};

struct PrivacySettings {
    bool private_mode{};
    std::vector<std::string> excluded_apps;
    bool local_only{true};
    // Wall-clock unix ms at which a timed privacy pause lapses; 0 when indefinite or off.
    std::int64_t private_until_wall_ms{};
};

// The single answer to "am I being recorded right now?", derived in one place so the header and
// tray cannot disagree.
enum class RecordingState {
    Blocked,        // capture cannot run at all: permissions, a failed hook
    PausedPrivate,  // the user said no, indefinitely or until private_until
    NoSession,      // nothing declared, so nothing is being attributed to anything
    PausedIdle,     // a session is running but the user is away (7.23's paused state)
    Recording,      // actually observing
};

inline const char* recording_state_as_str(RecordingState state) noexcept {
    switch (state) {
        case RecordingState::Blocked: return "blocked";
        case RecordingState::PausedPrivate: return "pausedPrivate";
        case RecordingState::NoSession: return "noSession";
        case RecordingState::PausedIdle: return "pausedIdle";
        case RecordingState::Recording: return "recording";
    }
    return "blocked";
}

struct RecordingStatus {
    RecordingState state{RecordingState::NoSession};
    // Remaining milliseconds of a timed privacy pause; 0 when the pause is indefinite or the
    // state is not PausedPrivate. The UI shows this rather than counting down on its own, so a
    // closed and reopened window cannot drift from the real deadline.
    std::int64_t private_pause_remaining_ms{};
    // Remaining ms of an alert snooze; 0 when not snoozed. A separate field, not a state: a
    // snooze silences alerts while recording continues.
    std::int64_t alert_snooze_remaining_ms{};
};

// The inputs the state is derived from, gathered so the rule itself is a pure function of
// them and can be exercised without an AppState.
struct RecordingInputs {
    bool capture_failed{};
    bool capture_permitted{true};
    bool private_mode{};
    bool has_active_session{};
    bool idle{};
};

// Precedence, highest first:
//
//   Blocked        nothing can be observed.
//   PausedPrivate  the user said not to (outranks NoSession: it stays true after Start).
//   NoSession      nothing declared.
//   PausedIdle     declared and running, but the user is away.
//   Recording      everything else.
constexpr RecordingState derive_recording_state(const RecordingInputs& in) noexcept {
    if (in.capture_failed || !in.capture_permitted) return RecordingState::Blocked;
    if (in.private_mode) return RecordingState::PausedPrivate;
    if (!in.has_active_session) return RecordingState::NoSession;
    if (in.idle) return RecordingState::PausedIdle;
    return RecordingState::Recording;
}

struct AnalyticsHour {
    int hour{};
    std::size_t sample_count{};
    double avg_focus_score{};
    double distracted_fraction{};
};

// One slice of a session's focus curve. `start_ms` is the slice's first prediction, so it is
// always a real instant.
struct FocusCurvePoint {
    std::int64_t start_ms{};
    std::size_t sample_count{};
    double avg_focus_score{};
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
    std::int64_t generated_at_ms{};
    std::size_t session_count{};
    std::size_t completed_session_count{};
    // Summed start-to-end duration of completed sessions in the window, including idle and
    // distracted stretches. Not focused time and not attended time.
    std::uint64_t focus_seconds{};
    // The session queries above read the newest `session_limit` sessions and filter those by
    // the window. `sessions_truncated` is set when that cap was binding, so "All time" can say
    // it is really "the latest 500" instead of silently omitting the rest.
    std::size_t session_limit{};
    bool sessions_truncated{};
    std::size_t sample_count{};
    double avg_focus_score{};
    double distracted_fraction{};
    // Seconds, not a row count.
    std::uint64_t longest_focus_secs{};
    std::string top_context_app;
    // Attended seconds in the window and the matching plan (daily for today, weekly for 7d); 0
    // planned means no target for this range.
    std::uint64_t attended_seconds{};
    std::uint32_t planned_mins{};
};

// One local calendar day of the Review trend series; `day` is "YYYY-MM-DD" on the user's clock
// (ADR-0007). All counts are durations: attended from clipped session_spans, focused/deep from
// gaps between consecutive same-state predictions.
struct DailySummaryDay {
    std::string day;
    std::uint64_t attended_secs{};
    std::uint64_t focused_secs{};     // non-DISTRACTED run time
    std::uint64_t deep_focus_secs{};  // DEEP_FOCUS-only run time
    double avg_focus_score{};
    std::size_t sample_count{};
    std::size_t session_count{};
    std::size_t snapback_count{};
};

struct DailySummary {
    std::string window;
    std::int64_t generated_at_ms{};
    // True when the requested range reached past the retention window and was clamped to it.
    bool capped{};
    std::vector<DailySummaryDay> days;
};

struct SummaryExportResult {
    std::string window;
    std::string output_path;
};

struct DiagnosticsSnapshot {
    std::string version;
    HealthStatus health;
    std::vector<std::string> recent_logs;
};

// Reserved for the planned `persistence-failed` event (see fixtures/ipc_commands.json).
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

// "Take me back".
struct FocusTargetResult {
    bool ok{false};
    std::string message;
};

struct FileDialogFilter {
    std::string name;
    std::string pattern;
};

struct FileDialogOptions {
    std::string title;
    std::string default_path;
    std::string default_name;
    std::vector<FileDialogFilter> filters;
};

struct FileDialogResult {
    bool ok{false};
    bool cancelled{false};
    std::string path;
    std::string message;
};


// --------------------------------------------------------------------------- JSON
// (de)serialization -- camelCase keys. Defined in types.cpp.
// ---------------------------------------------------------------------------
void to_json(json& j, const CaptureEvent& v);
void to_json(json& j, const PredictionRecord& v);
void from_json(const json& j, PredictionRecord& v);
void to_json(json& j, const SessionRecord& v);
void from_json(const json& j, SessionRecord& v);
void to_json(json& j, const SessionRecap& v);
void to_json(json& j, const SessionSummary& v);
void to_json(json& j, const PermissionStatus& v);
void from_json(const json& j, PermissionStatus& v);
void to_json(json& j, const ClassifierStatus& v);
void from_json(const json& j, ClassifierStatus& v);
void to_json(json& j, const ModelDeploymentHealth& v);
void from_json(const json& j, ModelDeploymentHealth& v);
void to_json(json& j, const RuntimeMetrics& v);
void from_json(const json& j, RuntimeMetrics& v);
void to_json(json& j, const HealthStatus& v);
void from_json(const json& j, HealthStatus& v);
void to_json(json& j, const SnapbackPayload& v);
void from_json(const json& j, SnapbackPayload& v);
void to_json(json& j, const ContextSnapshotDto& v);
void to_json(json& j, const AppRuleRecord& v);
void to_json(json& j, const UpsertAppRuleRequest& v);
void from_json(const json& j, UpsertAppRuleRequest& v);
void to_json(json& j, const LabelRequest& v);
void from_json(const json& j, LabelRequest& v);
void to_json(json& j, const ExportTrainingResult& v);
void to_json(json& j, const RecordingStatus& v);
void to_json(json& j, const AttendedProgress& v);
void to_json(json& j, const AlertChannels& v);
void from_json(const json& j, AlertChannels& v);
void to_json(json& j, const AlertDeliverySettings& v);
void from_json(const json& j, AlertDeliverySettings& v);
void to_json(json& j, const AppSettings& v);
void from_json(const json& j, AppSettings& v);
void to_json(json& j, const PrivacySettings& v);
void to_json(json& j, const ActivityDeletionResult& v);
void to_json(json& j, const AnalyticsHour& v);
void to_json(json& j, const FocusCurvePoint& v);
void to_json(json& j, const AnalyticsApp& v);
void to_json(json& j, const AnalyticsSummary& v);
void to_json(json& j, const SummaryReport& v);
void to_json(json& j, const DailySummaryDay& v);
void to_json(json& j, const DailySummary& v);
void to_json(json& j, const SummaryExportResult& v);
void to_json(json& j, const DiagnosticsSnapshot& v);
void to_json(json& j, const GoalCategory& v);
void from_json(const json& j, GoalCategory& v);
void to_json(json& j, const PersistenceFailurePayload& v);
void to_json(json& j, const LabelHotkeyPayload& v);
void to_json(json& j, const FocusTargetResult& v);
void to_json(json& j, const FileDialogFilter& v);
void from_json(const json& j, FileDialogFilter& v);
void to_json(json& j, const FileDialogOptions& v);
void from_json(const json& j, FileDialogOptions& v);
void to_json(json& j, const FileDialogResult& v);

}  // namespace snapback

