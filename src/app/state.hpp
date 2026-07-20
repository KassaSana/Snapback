// Central app state. Rust: state.rs (AppState) + the engine tick loop.
//
// Owns storage, the capture thread, the classifier, and the snapback tracker, and
// runs the engine loop that ties them together. Tauri's `.manage(state)` +
// interior-mutability (Mutex) becomes a plain shared object guarded by std::mutex.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "capture/capture_thread.hpp"
#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "engine/focus_summary.hpp"
#include "engine/idle_detector.hpp"
#include "engine/pomodoro.hpp"
#include "snapback/tracker.hpp"
#include "app/settings.hpp"
#include "storage/storage.hpp"
#include "types.hpp"
#include "util/logger.hpp"

namespace snapback {

class AppState {
public:
    // `logger` is optional (defaults to null) so existing call sites keep compiling
    // unchanged; pass one to route non-fatal warnings (e.g. a failed auto-label save)
    // somewhere other than stderr.
    explicit AppState(Storage storage, std::filesystem::path app_data_dir = {},
                      Logger* logger = nullptr);

    // Rust: AppState::start_engine — spawn capture + the engine tick thread.
    void start_engine();
    void stop_engine();

    // Host->frontend event sink, set by main.cpp once the webview exists. Called with
    // (event_name, json_payload) when the tick produces a new prediction or snapback.
    // The hook itself must be thread-safe (main.cpp marshals to the UI thread).
    using EmitHook = std::function<void(const char* event, const std::string& json_payload)>;
    void set_emit_hook(EmitHook hook);

    // Called by the IPC commands (app/commands.cpp). Guarded by mutex_.
    SessionRecord start_session(const std::string& goal, FocusMode mode);
    void stop_session();
    SessionRecord stop_session(const std::string& session_id);
    std::optional<SessionRecord> get_session(const std::string& session_id);
    HealthStatus health() const;
    DiagnosticsSnapshot diagnostics() const;
    std::optional<PredictionRecord> latest_prediction() const;
    std::optional<SessionRecord> active_session() const;

    // Snapback context-recovery payload. The engine tick stores the latest one;
    // latest_snapback() peeks, take_snapback() drains it (so each payload emits
    // once), dismiss_snapback() clears it on the frontend's request.
    std::optional<SnapbackPayload> latest_snapback() const;
    std::optional<SnapbackPayload> take_snapback();
    void dismiss_snapback();
    SessionRecap session_recap(const std::string& session_id);
    std::vector<PredictionRecord> prediction_history(std::size_t limit);
    // Aggregate the most recent `limit` predictions into recap stats (avg/peak/streak).
    FocusSummary focus_summary(std::size_t limit = 200);
    std::vector<SessionSummary> session_history(std::size_t limit);
    ExportTrainingResult export_training_data(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);
    void set_focus_mode(FocusMode mode);
    AppSettings settings() const;
    PrivacySettings privacy_settings() const;
    void set_private_mode(bool enabled);
    void set_privacy_exclusions(std::vector<std::string> exclusions);
    AnalyticsSummary analytics() const;
    SummaryReport summary_report(const std::string& window) const;
    SummaryExportResult export_summary_report(const std::filesystem::path& out_dir,
                                              const std::string& window) const;
    std::vector<GoalCategory> goal_categories() const;
    void set_goal_categories(std::vector<GoalCategory> categories);

    // Optional Pomodoro timer bound to the active focus session. Starting without an
    // active session is rejected; ending the active session stops the timer.
    PomodoroStatus start_pomodoro();
    PomodoroStatus stop_pomodoro();
    PomodoroStatus pomodoro_status() const;

    // App rules (allow/block overrides). The CRUD methods keep the cached rule set
    // (app_rules_) in sync so the live classifier sees changes immediately.
    std::vector<AppRuleRecord> app_rules();
    AppRuleRecord upsert_app_rule(const std::string& pattern, AppRuleKind rule_type,
                                  std::optional<std::string> note);
    void delete_app_rule(std::int64_t id);

    // Context-recovery timeline. nullopt session_id -> the active session (empty if none).
    std::vector<ContextSnapshotDto> context_timeline(std::optional<std::string> session_id,
                                                     std::size_t limit);

    ClassifierStatus classifier_status() const;
    PermissionStatus refresh_permissions();
    void submit_label(FocusLabel label, const std::string& source,
                      std::optional<std::string> notes = std::nullopt);
    void submit_label(const std::string& session_id, FocusLabel label, const std::string& source,
                      std::optional<std::string> notes = std::nullopt);

    // Test/headless seam: run one captured event through the same prediction path.
    void process_event_for_test(const CaptureEvent& event);

    // True once the user has gone AFK (no input for the idle threshold). Flips back on
    // the next real input. The engine tick maintains it; the frontend gets an "idle" event.
    bool is_idle() const;

    // Test/headless seam for the idle wiring: apply one idle-detection step at `now_ms`
    // (had_input = an input event was seen since the last step) and return the edge.
    IdleTransition update_idle_for_test(std::int64_t now_ms, bool had_input);

    // Deterministic seams for the same timer operations used by IPC/engine_tick.
    PomodoroStatus start_pomodoro_for_test(std::int64_t now_ms);
    std::optional<PomodoroStatus> update_pomodoro_for_test(std::int64_t now_ms);

private:
    // A tick's writes, computed under mutex_ (no storage I/O) and flushed later under
    // storage_mutex_. Keeping persistence out of the state lock is what stops a disk
    // write from blocking a UI read.
    struct PersistJob {
        std::string session_id;
        std::optional<ContextSnapshotDto> context_snapshot;
        std::optional<PredictionRecord> prediction;
        std::optional<FeatureVector> features;  // paired with prediction
    };

    void engine_tick();  // features -> classifier -> tracker -> (emit) ; persist off-lock
    // Runs the event through features/classifier/tracker and updates in-memory state.
    // Requires mutex_. Does NO storage I/O — returns what to persist (nullopt if nothing).
    std::optional<PersistJob> compute_event(const CaptureEvent& event);
    // Writes a job to storage. Requires storage_mutex_ (call inside a Transaction).
    void persist(const PersistJob& job);
    void reload_app_rules_unlocked();  // refresh app_rules_; requires mutex_ + storage_mutex_
    static std::vector<std::string> normalize_privacy_exclusions(
        std::vector<std::string> exclusions);
    bool is_private_event_unlocked(const CaptureEvent& event) const;
    static std::string now_rfc3339();
    static std::int64_t steady_now_ms();  // monotonic clock for idle timing
    static bool is_input_event(EventType type);  // key/mouse = real user activity
    // Advance the idle state machine one step. Requires mutex_. Returns the transition
    // edge so the tick loop can emit it. Sets idle_ from the resulting state.
    IdleTransition update_idle_unlocked(std::int64_t now_ms, bool had_input);
    PomodoroStatus start_pomodoro_unlocked(std::int64_t now_ms);
    // Injected logger if one was passed in, otherwise the stderr fallback below.
    Logger& log() { return logger_ ? *logger_ : local_logger_; }
    const Logger& log() const { return logger_ ? *logger_ : local_logger_; }

    // Lock order (deadlock-free): always acquire mutex_ BEFORE storage_mutex_, never the
    // reverse. mutex_ guards in-memory state; storage_mutex_ serializes all storage_ access
    // (so cross-thread transactions never interleave). Hot UI reads take only mutex_.
    mutable std::mutex mutex_;
    mutable std::mutex storage_mutex_;
    Storage storage_;
    std::filesystem::path app_data_dir_;
    Logger* logger_ = nullptr;
    Logger local_logger_{std::cerr};
    CaptureThread capture_;
    FeatureExtractor features_;
    Classifier classifier_;
    ContextTracker context_tracker_;
    IdleDetector idle_detector_;
    PomodoroTimer pomodoro_;

    std::optional<SessionRecord> active_session_;
    std::vector<AppRuleRecord> app_rules_;  // cached; passed to the live classifier
    std::optional<PredictionRecord> latest_prediction_;
    std::optional<SnapbackPayload> latest_snapback_;
    AppSettings settings_;
    FocusMode focus_mode_ = FocusMode::Normal;
    double last_prediction_secs_ = -1.0;
    double last_event_secs_ = 0.0;  // timestamp of the most recent processed event
    bool prediction_dirty_ = false;  // a new prediction awaits emission this tick
    bool idle_ = false;              // user is currently AFK (mirrors idle_detector_ state)

    EmitHook emit_hook_;
    std::thread engine_thread_;
    std::atomic<bool> engine_running_{false};
};

}  // namespace snapback
