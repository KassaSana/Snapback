// Central app state. Rust: state.rs (AppState) + the engine tick loop.
//
// Owns storage, the capture thread, the classifier, and the snapback tracker, and
// runs the engine loop that ties them together. Tauri's `.manage(state)` +
// interior-mutability (Mutex) becomes a plain shared object guarded by std::mutex.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "capture/capture_thread.hpp"
#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "snapback/tracker.hpp"
#include "app/settings.hpp"
#include "storage/storage.hpp"
#include "types.hpp"

namespace snapback {

class AppState {
public:
    explicit AppState(Storage storage, std::filesystem::path app_data_dir = {});

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
    std::vector<SessionSummary> session_history(std::size_t limit);
    ExportTrainingResult export_training_data(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);
    void set_focus_mode(FocusMode mode);
    AppSettings settings() const;

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
    static std::string now_rfc3339();

    // Lock order (deadlock-free): always acquire mutex_ BEFORE storage_mutex_, never the
    // reverse. mutex_ guards in-memory state; storage_mutex_ serializes all storage_ access
    // (so cross-thread transactions never interleave). Hot UI reads take only mutex_.
    mutable std::mutex mutex_;
    mutable std::mutex storage_mutex_;
    Storage storage_;
    std::filesystem::path app_data_dir_;
    CaptureThread capture_;
    FeatureExtractor features_;
    Classifier classifier_;
    ContextTracker context_tracker_;

    std::optional<SessionRecord> active_session_;
    std::vector<AppRuleRecord> app_rules_;  // cached; passed to the live classifier
    std::optional<PredictionRecord> latest_prediction_;
    std::optional<SnapbackPayload> latest_snapback_;
    AppSettings settings_;
    FocusMode focus_mode_ = FocusMode::Normal;
    double last_prediction_secs_ = -1.0;
    double last_event_secs_ = 0.0;  // timestamp of the most recent processed event
    bool prediction_dirty_ = false;  // a new prediction awaits emission this tick

    EmitHook emit_hook_;
    std::thread engine_thread_;
    std::atomic<bool> engine_running_{false};
};

}  // namespace snapback
