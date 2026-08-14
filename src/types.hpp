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
    // MONOTONIC seconds, from an uptime clock (GetTickCount64 / steady_clock). Owns
    // durations, ordering, debounce, and the rolling windows. It is NOT epoch time and must
    // never be handed to a calendar function — Roadmap 7.24 exists because it was.
    double timestamp_secs{};
    // WALL-CLOCK seconds since the Unix epoch, for calendar features (hour_of_day,
    // day_of_week). Zero means "not supplied", in which case the extractor falls back to
    // timestamp_secs — which is what the feature-parity fixtures rely on, since they feed
    // epoch-shaped values through timestamp_secs directly.
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
    std::string timestamp;
    std::string model_id{"heuristic:snapback-features-v1-31"};
    // Which rule decided focus_state (ADR-0004): 'model', 'risk', 'thrash', 'block', or
    // 'drift'. nullopt on rows written before verdicts carried provenance; nothing can
    // backfill those, so nullopt means "unknown", not "model".
    std::optional<std::string> state_source;
};

// SessionRecord. Note focus_mode is a plain string here (not the enum).
struct SessionRecord {
    std::string session_id;
    std::string goal;
    std::string status;
    std::string focus_mode;
    std::optional<std::string> started_at;
    std::optional<std::string> ended_at;
    // Roadmap 2.14. The user's own account of the session, kept deliberately apart from the
    // focus label: a label is a training signal, this is a note to their future self. nullopt
    // means the question was never answered — Skip is one click and must stay indistinguishable
    // from never being asked, so it writes nothing rather than an empty string.
    std::optional<std::string> reflection_done;
    std::optional<std::string> reflection_next_step;
};

// SessionRecap.
struct SessionRecap {
    std::string session_id;
    std::string goal;
    // Wall clock from start to end, including time the user was away.
    std::uint64_t duration_secs{};
    // Time the user was actually present, summed from session_spans (Roadmap 7.23 /
    // ADR-0005). Empty for sessions that predate span recording — meaning "never measured",
    // not "zero", so a reader falls back to duration_secs instead of showing a fabricated 0.
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
    std::string backend{"heuristic"};
    bool onnx_runtime_enabled{};
    std::optional<std::string> model_path;
    std::optional<std::string> model_id;
};

// ModelDeploymentHealth — optional ONNX deployment recovery state. Roadmap 13.8.
//
// When cleanup debris cannot be removed at startup, the core app still opens on the heuristic
// backend. This object names what was preserved and whether the user can retry or roll back.
struct ModelDeploymentHealth {
    std::string state{"ok"};  // "ok" | "degraded"
    std::optional<std::string> message;
    std::vector<std::string> preserved_paths;
    bool retry_cleanup_available{};
    bool rollback_available{};
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
    // ADR-0006 / roadmap 13.7. True in Debug, or Release with SNAPBACK_DEV_TRAINING set.
    bool developer_tools_enabled{};
};

// SnapbackEpisode — one recorded distraction: the user left focused work and came back.
//
// Roadmap 2.15. The durable counterpart to SnapbackPayload, which is the transient thing the
// overlay shows. The payload was emitted, displayed, and dropped; `recap()` has always counted
// `snapback_events` rows, and nothing ever wrote one, so the Snapback count every user saw was
// zero. This is what gets stored.
//
// `started_at` and `session_id` together identify an episode — a duplicate tick or a delivery
// retry must not produce a second row (a UNIQUE index enforces it).
struct SnapbackEpisode {
    std::string session_id;
    // The route back, exactly as it was offered to the user.
    std::string summary;
    // Where they were before the distraction, not where they went. The distracting app is
    // deliberately not recorded: this table answers "what was I doing", and storing the other
    // half would make an interruption log into a browsing history.
    std::string app_name;
    std::string file_hint;
    std::string started_at;  // when the distraction began
    std::string ended_at;    // when they returned; the pre-existing `timestamp` column
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
    std::string timestamp;
};

// AppRuleRecord.
struct AppRuleRecord {
    std::int64_t id{};
    std::string pattern;
    AppRuleKind rule_type{AppRuleKind::Allow};
    std::optional<std::string> note;
    std::string created_at;
    std::string updated_at;
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

// Roadmap 7.23. Bounds on the AFK threshold the user may choose. The floor exists because a
// few seconds of silence is ordinary thought, not absence, and a threshold under it would
// shred one attended stretch into hundreds of spans. The ceiling exists because a threshold
// long enough to cover a lunch break stops measuring attendance at all. Out-of-range values
// are rejected rather than clamped: a rejected setting is visible, a clamped one is not.
inline constexpr std::int64_t kDefaultIdleThresholdSecs = 300;
inline constexpr std::int64_t kMinIdleThresholdSecs = 30;
inline constexpr std::int64_t kMaxIdleThresholdSecs = 3600;

// New C++ settings DTO. Persisted in app-data/settings.json and exposed to the
// frontend with camelCase keys.
struct AppSettings {
    FocusMode default_focus_mode{FocusMode::Normal};
    bool private_mode{};
    std::vector<std::string> excluded_apps;
    std::vector<GoalCategory> goal_categories;
    // Roadmap 7.23 / ADR-0005. How long without input before a session is treated as
    // unattended and its active-time span pauses. Five minutes was inherited from
    // kDefaultIdleThresholdMs and is a judgement about the user's working rhythm, not a
    // constant of the system: reading and thinking look identical to a keyboard.
    std::int64_t idle_threshold_secs{kDefaultIdleThresholdSecs};
    // Roadmap 2.13. Phase lengths, long-break cadence, and whether the next phase begins on
    // its own — a rhythm is a preference, and hardcoding 25/5/15 made it the app's opinion.
    PomodoroConfig pomodoro{};
    // Roadmap 2.13. The running timer, written down so a relaunch resumes it. Settings is
    // where this lives because it is already the app's atomically-written, fsync'd file; a
    // second store for six fields would be a second thing that can half-write.
    PomodoroSnapshot pomodoro_state{};
    // Roadmap 2.19. Opt-in attended-minute targets, **0 meaning no target**. Off by default
    // and deliberately so: a focus tool that ships with a quota has decided how much someone
    // should work before meeting them.
    std::uint32_t attended_target_daily_mins{};
    std::uint32_t attended_target_weekly_mins{};
    // Roadmap 2.10. Unix ms at which a *timed* privacy pause lapses; 0 when private mode is
    // off or indefinite. Persisted so a pause means the same thing after a restart — the item
    // requires exactly that, since a pause that quietly ended while the app was closed would
    // resume recording without the user ever being told.
    std::int64_t private_until_wall_ms{};
    // Roadmap 2.7. A dismissed missed-session prompt stays dismissed across a restart.
    std::int64_t untracked_nudge_until_wall_ms{};
};

// Roadmap 2.19. A plan and what actually happened, side by side.
//
// Minutes rather than seconds because that is the unit a target is set in, and reporting a
// finer one would imply a precision the plan does not have. A target of 0 means "not set" —
// the UI shows attendance without a bar rather than progress toward nothing.
struct AttendedProgress {
    std::uint32_t daily_target_mins{};
    std::uint64_t daily_actual_mins{};
    std::uint32_t weekly_target_mins{};
    std::uint64_t weekly_actual_mins{};
};

// What "Delete all activity" actually did. Roadmap 8.12.
//
// It used to return nothing, which meant the UI had exactly two things it could say —
// "deleted" or "failed" — for an operation that can legitimately half-succeed: a stale export
// held open by another program does not stop the database being cleared, and should not be
// reported as if it did. Saying "permanently deleted" over a partial result is the specific
// failure this structure exists to make impossible.
struct ActivityDeletionResult {
    // Activity-bearing artifacts that are now gone. Absence counts as removed: an export that
    // was never created is not a copy of anything.
    std::vector<std::string> deleted;
    // Activity-bearing artifacts that could not be removed, each with the reason. Non-empty
    // means the answer is "most of it", and the UI must say so.
    std::vector<std::string> failed;
    // Classified as configuration and deliberately kept. Listed rather than omitted so the
    // decision is visible to the person asking what remains, instead of being an unstated
    // assumption they would have to read the source to discover.
    std::vector<std::string> retained;

    [[nodiscard]] bool complete() const { return failed.empty(); }
};

struct PrivacySettings {
    bool private_mode{};
    std::vector<std::string> excluded_apps;
    bool local_only{true};
    // Roadmap 2.10. When private mode was turned on for a fixed stretch, the wall-clock instant
    // (unix ms) it lapses at; 0 when private mode is indefinite or off. Wall clock rather than
    // monotonic for the same reason the Pomodoro deadline is: a timed pause has to mean the
    // same thing after a restart, and the monotonic timeline begins again with the process.
    std::int64_t private_until_wall_ms{};
};

// Roadmap 2.10. The single answer to "am I being recorded right now?".
//
// One model, derived in one place, so the header and the tray cannot disagree — the item is
// explicit that two surfaces computing this separately is the defect, not the layout.
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

// Precedence, highest first, and each step is a promise the one below it cannot keep:
//
//   Blocked        — nothing can be observed, so no other answer is true.
//   PausedPrivate  — the user said not to. Ranked above NoSession because it stays true when
//                    they start a session, and a person checking this wants the strongest
//                    reason recording is not happening, not the incidental one.
//   NoSession      — nothing declared; capture may run but is attributed to nothing.
//   PausedIdle     — declared and running, but they are away. 7.23 already owns this state.
//   Recording      — everything else.
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
    std::size_t completed_session_count{};
    std::uint64_t focus_seconds{};
    std::size_t sample_count{};
    double avg_focus_score{};
    double distracted_fraction{};
    // Roadmap 10.13. Seconds, not a row count. It was previously the number of consecutive
    // non-DISTRACTED prediction rows, displayed as "Best streak" — a time-shaped label over a
    // quantity that is not time.
    std::uint64_t longest_focus_secs{};
    std::string top_context_app;
    // Roadmap 2.19 Review half. Durable attended seconds in the comparison window, and the
    // matching plan in minutes when one applies (daily for today, weekly for 7d). Zero planned
    // means "no target for this range" — not "target of zero minutes".
    std::uint64_t attended_seconds{};
    std::uint32_t planned_mins{};
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

// CaptureFailurePayload / OverlayFailurePayload / PersistenceFailurePayload.
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

// FocusTargetResult — Roadmap 2.8 ("Take me back").
struct FocusTargetResult {
    bool ok{false};
    std::string message;
};

// FileDialog types — Roadmap 10.14.
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
void to_json(json& j, const ModelDeploymentHealth& v);
void from_json(const json& j, ModelDeploymentHealth& v);
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
void to_json(json& j, const RecordingStatus& v);
void to_json(json& j, const AttendedProgress& v);
void to_json(json& j, const AppSettings& v);
void from_json(const json& j, AppSettings& v);
void to_json(json& j, const PrivacySettings& v);
void from_json(const json& j, PrivacySettings& v);
void to_json(json& j, const ActivityDeletionResult& v);
void from_json(const json& j, ActivityDeletionResult& v);
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
void to_json(json& j, const FocusTargetResult& v);
void from_json(const json& j, FocusTargetResult& v);
void to_json(json& j, const FileDialogFilter& v);
void from_json(const json& j, FileDialogFilter& v);
void to_json(json& j, const FileDialogOptions& v);
void from_json(const json& j, FileDialogOptions& v);
void to_json(json& j, const FileDialogResult& v);
void from_json(const json& j, FileDialogResult& v);

}  // namespace snapback

