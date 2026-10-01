// Central application state and the engine tick loop.
//
// Owns storage, the capture thread, the classifier, and the snapback tracker, and
// runs the engine loop that ties them together. The shared object is guarded by
// std::mutex where mutation crosses threads.
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
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
#include "review_window.hpp"
#include "engine/idle_detector.hpp"
#include "engine/pomodoro.hpp"
#include "snapback/tracker.hpp"
#include "app/alert_routing.hpp"
#include "app/data_export.hpp"
#include "app/settings.hpp"
#include "storage/storage.hpp"
#include "types.hpp"
#include "util/clock.hpp"
#include "util/logger.hpp"
#include "util/ranked_mutex.hpp"
#include "util/writer_priority.hpp"

namespace snapback {

inline constexpr std::int64_t kCaptureStallThresholdMs = 30'000;

// Uptime between retention prunes. The app runs for weeks in the tray, so pruning only on
// open is not enough.
inline constexpr std::int64_t kRetentionPruneIntervalMs = 24 * 60 * 60 * 1000;
inline constexpr std::size_t kRetentionPruneBatchRows = 256;
inline constexpr std::int64_t kRetentionPruneYieldMs = 10;

// Max capture events one tick processes while holding mutex_, so a busy producer cannot hold
// the lock indefinitely. Nothing is dropped: the rest stays queued and the next tick runs
// immediately.
inline constexpr std::size_t kEngineDrainBudget = 2048;
// Time ceiling on the same drain, checked every kEngineDrainClockCheckStride events.
inline constexpr std::int64_t kEngineDrainBudgetMs = 20;
inline constexpr std::size_t kEngineDrainClockCheckStride = 128;
// Gap between ticks. The backlog gap is non-zero so a waiting command thread can take mutex_
// between two bounded drains.
// Quiet ticks wait for capture or the next authoritative deadline.
inline constexpr std::int64_t kEngineBacklogTickIntervalMs = 1;
// Throttle for the "capture backlog" log line.
inline constexpr std::int64_t kEngineBacklogLogIntervalMs = 30'000;

class AppState {
public:
    // `logger` defaults to stderr and `clock` to the system clock; tests inject both.
    explicit AppState(Storage storage, std::filesystem::path app_data_dir = {},
                      Logger* logger = nullptr, Clock* clock = nullptr,
                      ModelDeploymentHealth model_deployment = {});
    ~AppState() noexcept;

    // Spawn capture and the engine tick thread.
    void start_engine();
    void notify_frontend_ready();
    // Test seam: run the same engine loop with an injected hook instead of installing
    // the platform-wide input hook.
    void start_engine_for_test(InputHook* hook);
    void stop_engine() noexcept;

    // Host->frontend event sink, set by main.cpp once the webview exists. Called with
    // (event_name, json_payload) when the tick produces a new prediction or snapback.
    // The hook itself must be thread-safe (main.cpp marshals to the UI thread).
    using ActivityEpoch = std::uint64_t;
    using EmitHook =
        std::function<void(const char* event, const std::string& json_payload,
                           ActivityEpoch activity_epoch)>;
    void set_emit_hook(EmitHook hook);
    // Push one event to the frontend from any thread. It carries the current activity epoch,
    // so the UI-side check treats it like an engine emission: dropped only if a delete-all
    // has since reset the app's activity. A no-op before the hook is installed.
    void emit_event(const char* event, const std::string& json_payload);
    // UI dispatch is asynchronous. Event closures carry the epoch from their engine tick
    // and call this immediately before touching the webview, overlay, or notification.
    bool activity_epoch_is_current(ActivityEpoch epoch) const noexcept {
        return activity_epoch_.load(std::memory_order_acquire) == epoch;
    }

    // Called by the IPC commands. Mutations use mutex_; hot live reads consume the
    // immutable snapshot published after each relevant mutation.
    SessionRecord start_session(const std::string& goal, FocusMode mode);
    void stop_session();
    SessionRecord stop_session(const std::string& session_id);
    std::optional<SessionRecord> get_session(const std::string& session_id);
    HealthStatus health() const;
    DiagnosticsSnapshot diagnostics() const;
    // Iterations of the engine tick loop since start (pairs with process CPU time).
    std::uint64_t engine_wakeups() const {
        return engine_wakeups_.load(std::memory_order_relaxed);
    }
    // The capture ring's deepest occupancy so far, in events.
    std::size_t capture_ring_high_water() const { return capture_.ring_high_water(); }
    // Busy-wait counters for the engine's storage connection. Lock-free: a diagnostic must
    // not queue behind the contention it measures.
    SqliteBusySnapshot storage_busy_stats() const;
    // Runtime counters, taking no locks. Folded into `health()` so they reach support bundles.
    RuntimeMetrics runtime_metrics() const;
    std::optional<PredictionRecord> latest_prediction() const;
    std::optional<SessionRecord> active_session() const;

    // Snapback context-recovery payload. The engine tick stores the latest one and emits
    // it once (see snapback_emitted_); latest_snapback() peeks, take_snapback() drains it,
    // dismiss_snapback() clears it on the frontend's request. The payload outlives its
    // emission so restore_snapback_target() still has a target when the user clicks.
    std::optional<SnapbackPayload> latest_snapback() const;
    std::optional<SnapbackPayload> take_snapback();
    void dismiss_snapback();
    // "Take me back": activates the latest snapback's target window, then dismisses it.
    FocusTargetResult restore_snapback_target();
    SessionRecap session_recap(const std::string& session_id);
    std::optional<FocusLabel> session_auto_label(const std::string& session_id);
    // See Storage::session_focus_curve.
    std::vector<FocusCurvePoint> session_focus_curve(const std::string& session_id,
                                                     std::size_t buckets);
    std::optional<SnapbackEpisode> session_longest_snapback(const std::string& session_id);

    // Saves the optional end-of-session reflection; nullopt result means no such session.
    // Either field may be nullopt to leave (or clear) that answer.
    std::optional<SessionRecord> save_session_reflection(
        const std::string& session_id, const std::optional<std::string>& done,
        const std::optional<std::string>& next_step);
    std::vector<PredictionRecord> prediction_history(std::size_t limit);
    std::vector<SessionSummary> session_history(std::size_t limit);
    // Erases every app-owned copy of the user's activity and reports what happened to each.
    // It can half-succeed (e.g. an export held open by another program), so every target is
    // attempted and the result says which ones failed.
    ActivityDeletionResult delete_all_activity_data();

    // Removes one session and everything recorded during it. Returns false when no such
    // session exists. If it is the session currently being filled, the live engine state is
    // reset too, so nothing keeps writing to a row that is gone.
    bool delete_session(const std::string& session_id);
    ExportTrainingResult export_training_data(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);

    // Everything Snapback recorded about the user, as Markdown. `page_size` bounds rows held
    // in memory, not rows written.
    PersonalArchiveExport export_personal_data(const std::filesystem::path& out_dir,
                                               std::size_t page_size = 200);
    void set_focus_mode(FocusMode mode);
    AppSettings settings() const;
    // True exactly once, for the first close that leaves the app in the tray. Persisted, so
    // it survives a restart.
    bool claim_tray_close_notice();

    // Consume the right to act on a clicked alert; true at most once per alert. Duplicate
    // clicks and clicks on stale toasts (a newer alert replaced the id) claim nothing.
    // Callers still raise the window on false.
    bool claim_alert_action(AlertEvent event, std::int64_t alert_id);

    // The id a click on this kind's alert would currently claim, or 0. For the overlay, which
    // carries no id of its own and only ever shows the newest card. Does not consume it.
    std::int64_t outstanding_alert_id(AlertEvent event) const;
    PrivacySettings privacy_settings() const;
    void set_private_mode(bool enabled);
    // The one answer to "am I being recorded right now?". Also lapses an expired timed pause.
    RecordingStatus recording_status();
    // Turns private mode on for a fixed stretch; 0 minutes means indefinite.
    RecordingStatus pause_privately_for(std::int64_t minutes);
    // Emits `recording-status` with the current answer and returns it. Every mutator that can
    // change the answer calls this, so the page never has to poll.
    RecordingStatus announce_recording_status();
    // Silences alert delivery (not recording) for a stretch; 0 minutes means the default 30.
    RecordingStatus snooze_alerts_for(std::int64_t minutes);
    RecordingStatus resume_alerts();
    // Replaces the delivery preferences wholesale. The snooze deadline is not one of them, so a
    // Settings save cannot extend or cancel a snooze started from the tray.
    AppSettings set_alert_delivery(AlertDeliverySettings alerts);
    RecordingStatus resume_from_private_pause();
    // Suppress the missed-session nudge for a deliberate interval (60 minutes by default).
    void dismiss_untracked_nudge(std::int64_t minutes = 60);
    // How long without input pauses attended time. Throws (changing nothing) outside
    // [kMinIdleThresholdSecs, kMaxIdleThresholdSecs].
    void set_idle_threshold_secs(std::int64_t seconds);
    void set_privacy_exclusions(std::vector<std::string> exclusions);
    AnalyticsSummary analytics(const std::string& window = "all",
                               const std::optional<std::string>& since = std::nullopt) const;
    SummaryReport summary_report(const std::string& window,
                                 const std::optional<std::string>& since = std::nullopt) const;
    // Per-local-calendar-day series for the Review trend surfaces. "all" is clamped to the
    // retention window (predictions older than that are pruned anyway) and reported as capped.
    DailySummary daily_summary(const std::string& window,
                               const std::optional<std::string>& since = std::nullopt) const;
    SummaryExportResult export_summary_report(const std::filesystem::path& out_dir,
                                              const std::string& window,
                                              const std::optional<std::string>& since =
                                                  std::nullopt) const;
    FocusSummary focus_summary_for_window(const std::string& window,
                                          const std::optional<std::string>& since =
                                              std::nullopt);
    std::vector<SessionSummary> session_history_for_window(
        const std::string& window, const std::optional<std::string>& since = std::nullopt);
    std::vector<GoalCategory> goal_categories() const;
    void set_goal_categories(std::vector<GoalCategory> categories);

    // Optional Pomodoro timer bound to the active focus session. Starting without an
    // active session is rejected; ending the active session stops the timer.
    PomodoroStatus start_pomodoro();
    PomodoroStatus stop_pomodoro();
    PomodoroStatus pomodoro_status() const;
    // Skip ends a phase early without crediting an unfinished work interval; acknowledge begins
    // the phase waiting since the last boundary. Each persists the timer across relaunch.
    PomodoroStatus pause_pomodoro();
    PomodoroStatus resume_pomodoro();
    PomodoroStatus skip_pomodoro_phase();
    PomodoroStatus restart_pomodoro_phase();
    PomodoroStatus acknowledge_pomodoro_phase();
    // Phase lengths, long-break cadence, and auto-start. Applies from the next phase on.
    PomodoroStatus set_pomodoro_config(const PomodoroConfig& config);
    PomodoroConfig pomodoro_config() const;
    // Opt-in attended-minute targets; 0 means "not set".
    AttendedProgress attended_progress();
    AttendedProgress set_attended_targets(std::uint32_t daily_mins, std::uint32_t weekly_mins);

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
    ClassifierStatus reload_classifier_model();
    ModelDeploymentHealth retry_model_deployment_cleanup();
    PermissionStatus refresh_permissions();
    // Prompt for capture permission (macOS Accessibility dialog), then report the result.
    // User-initiated only — refresh_permissions() is the pollable, dialog-free version.
    PermissionStatus request_permissions();
    void submit_label(FocusLabel label, const std::string& source,
                      std::optional<std::string> notes = std::nullopt);
    void submit_label(const std::string& session_id, FocusLabel label, const std::string& source,
                      std::optional<std::string> notes = std::nullopt);

    // True once the user has gone AFK (no input for the idle threshold). Flips back on
    // the next real input. The engine tick maintains it; the frontend gets an "idle" event.
    bool is_idle() const;

private:
    // The *_for_test seams below drive the tick's state machines without running the engine
    // thread; only tests/app_state_test_access.hpp can reach them.
    friend struct AppStateTestAccess;

    // Immutable state published after a mutation, so live UI reads never take mutex_.
    struct LiveReadSnapshot {
        std::optional<SessionRecord> active_session;
        std::optional<PredictionRecord> latest_prediction;
        std::optional<SnapbackPayload> latest_snapback;
        std::optional<std::int64_t> last_prediction_at_ms;
        ClassifierStatus classifier;
        ModelDeploymentHealth model_deployment;
        std::optional<std::string> persistence_failure_reason;
        bool private_mode{};
        bool idle{};
    };

    std::shared_ptr<const LiveReadSnapshot> live_read_snapshot() const noexcept;
    // Requires mutex_ after construction. A dirty flag avoids allocating on empty 100 ms
    // engine ticks; publication happens only when a field above has changed.
    void publish_live_read_unlocked();

    // Run one captured event through the same prediction path the tick uses.
    void process_event_for_test(const CaptureEvent& event);
    // Apply one idle-detection step at `now_ms` (had_input = an input event was seen since
    // the last step) and return the edge.
    IdleTransition update_idle_for_test(std::int64_t now_ms, bool had_input);
    std::optional<PomodoroStatus> update_pomodoro_for_test(std::int64_t now_ms);
    // The feature vector the classifier would see at `now_secs` (event clock). Lets a test
    // assert what reached the model without reverse-engineering it from a prediction.
    FeatureVector extract_features_for_test(double now_secs);

    // Drops a span decision phase 1 has recorded but no tick has drained yet. `session_id`
    // nullopt drops whatever is pending. Requires mutex_.
    void discard_pending_span_unlocked(
        const std::optional<std::string>& session_id = std::nullopt);

    void start_engine_impl(InputHook* hook);
    // A tick's writes, computed under mutex_ (no storage I/O) and flushed later under
    // storage_mutex_. Keeping persistence out of the state lock is what stops a disk
    // write from blocking a UI read.
    struct PersistJob {
        std::string session_id;
        std::optional<ContextSnapshotDto> context_snapshot;
        std::optional<PredictionRecord> prediction;
        std::optional<FeatureVector> features;  // paired with prediction
        // Same job (so same transaction and activity epoch) as its event, so a "delete all"
        // cannot leave an episode behind.
        std::optional<SnapbackEpisode> snapback_episode;
    };

    // features -> classifier -> tracker -> (emit) ; persist off-lock.
    // Returns true when the drain stopped on kEngineDrainBudget / kEngineDrainBudgetMs rather
    // than on an empty ring — i.e. work is still queued and the caller should tick again now
    // instead of sleeping out the usual interval.
    bool engine_tick();
    std::optional<std::int64_t> next_engine_delay_ms() const;
    void request_retention_maintenance();
    void run_retention_maintenance() noexcept;

    // Every change to the maintenance flags goes through here. The flag must be set with
    // maintenance_mutex_ held, or the notify can be lost and stop_engine() hangs on join().
    template <typename Apply>
    void signal_maintenance(Apply&& apply) {
        {
            std::lock_guard lock(maintenance_mutex_);
            apply();
        }
        maintenance_ready_.notify_all();
    }
    // Runs the event through features/classifier/tracker and updates in-memory state.
    // Requires mutex_. Does NO storage I/O — returns what to persist (nullopt if nothing).
    std::optional<PersistJob> compute_event(const CaptureEvent& event);

    // Untracked-work nudge (ADR-0005): sustained input with no session prompts once per
    // stretch. Cleared when a session starts or the user goes idle.
    static constexpr std::int64_t kUntrackedNudgeMinutes = 15;
    std::optional<std::int64_t> untracked_since_ms_;
    bool untracked_latched_ = false;
    std::optional<std::uint64_t> untracked_minutes_;  // pending emit, drained by the tick
    // Last capture event's app. Excluded-app time resets the untracked stretch.
    std::string last_capture_app_;
    // AFK intentionally freezes feature ingestion, including foreground-change events. Keep
    // the newest permitted context separately so the first public event after wake can
    // reconcile the extractor before that event is counted and classified.
    std::optional<CaptureEvent> idle_foreground_context_;
    bool foreground_context_needs_resync_ = false;

    struct PendingSpanTransition {
        std::uint64_t id{};
        std::string session_id;
        bool opens{};
        std::int64_t millis_ago{};
        // Resolved from Storage's clock on the first attempt and retained across retries.
        std::optional<std::int64_t> timestamp_ms;
        std::optional<std::int64_t> decided_at_steady_ms;
    };
    // Span open/close decisions from idle edges, decided under mutex_ and written by the tick
    // under storage_mutex_. Kept until their transaction commits; a deque because a wake can
    // arrive while a failed close is waiting to retry.
    std::deque<PendingSpanTransition> pending_span_transitions_;
    std::uint64_t next_span_transition_id_ = 0;
    // Deterministic transaction-stage fault seam used by AppState tests. Empty in production.
    std::function<void(const char*)> persistence_test_hook_;
    std::optional<std::string> persistence_failure_reason_;
    std::string persistence_failure_category_;
    std::int64_t persistence_retry_at_ms_{};
    std::int64_t persistence_retry_delay_ms_{};
    std::optional<SnapbackEpisode> pending_snapback_episode_;
    std::atomic<std::uint64_t> persistence_failures_{0};
    std::atomic<std::uint64_t> persistence_dropped_predictions_{0};
    // Desired attendance follows the detector immediately; committed attendance advances
    // only when the corresponding storage transaction commits. Both require mutex_.
    bool session_attended_ = false;
    bool committed_session_attended_ = false;
    // Closes a span a previous process left open, at the session's last recorded activity.
    // Requires mutex_ + storage_mutex_ (the constructor runs before either can be contended).
    void hydrate_session_attendance_unlocked();
    // Restores a hydrated active session's focus mode and feature-extractor origin. Same
    // locking note as above.
    void hydrate_active_session_unlocked();
    // Closes the open span on the way out, so a clean exit does not look like a crash to the
    // next launch. Takes both locks itself; safe to call when nothing is open.
    void close_open_span_on_shutdown() noexcept;

    // Hyperfocus guardrail state. `hyperfocus_latched_` prevents a second nudge inside the
    // same unbroken stretch; `hyperfocus_minutes_` is the pending emit drained by the tick.
    bool hyperfocus_latched_ = false;
    std::optional<std::uint64_t> hyperfocus_minutes_;
    // Writes a job to storage. Requires storage_mutex_ (call inside a Transaction).
    void persist(const PersistJob& job);
    // The one path every settings mutation takes: write to disk, then commit in memory, then
    // `publish` to live fields. A failed write changes nothing. `publish` must not throw.
    // Requires mutex_.
    void commit_settings_unlocked(AppSettings candidate, const std::function<void()>& publish);
    // Returns the id a click may claim, or 0 when not clickable; clears the kind's outstanding
    // claim either way. Requires mutex_.
    std::int64_t issue_alert_id_unlocked(AlertEvent event, const AlertRoute& route);
    // Ends a timed privacy pause whose deadline has passed. Requires mutex_.
    bool lapse_private_pause_unlocked();
    void save_auto_session_label_unlocked(const std::string& session_id);
    // Drops the pending snapback payload and its emitted flag together. Requires mutex_.
    void clear_snapback_unlocked(bool discard_pending_recording = true);
    void reload_app_rules_unlocked();  // refresh app_rules_; requires mutex_ + storage_mutex_
    static std::vector<std::string> normalize_privacy_exclusions(
        std::vector<std::string> exclusions);
    bool app_matches_exclusion_unlocked(const std::string& app_name) const;
    bool is_private_event_unlocked(const CaptureEvent& event) const;
    // All time goes through clock(). Wall time is UTC epoch milliseconds (ADR-0007).
    std::int64_t now_unix_ms() const;
    // Wall-clock instant `secs` in the past: stamps a pause when the user actually stopped.
    std::int64_t unix_ms_secs_ago(std::int64_t secs) const;
    std::int64_t steady_now_ms() const;  // monotonic clock for idle timing
    // The delivery decision for one alert; keeps app/alert_routing.hpp clock-free. Requires
    // mutex_.
    AlertRoute alert_route_unlocked(AlertEvent event) const;
    static bool is_input_event(EventType type);  // key/mouse = real user activity
    // Advance the idle state machine one step. Requires mutex_. Returns the transition
    // edge so the tick loop can emit it. Sets idle_ from the resulting state.
    IdleTransition update_idle_unlocked(std::int64_t now_ms, bool had_input,
                                        const CaptureEvent* waking_event = nullptr);
    PomodoroStatus start_pomodoro_unlocked(std::int64_t now_ms);
    // Best-effort save of the timer position to settings.json.
    void persist_pomodoro_unlocked();
    PomodoroStatus mutate_pomodoro(const std::function<void(std::int64_t)>& apply);
    // Injected logger if one was passed in, otherwise the stderr fallback below.
    Logger& log() { return logger_ ? *logger_ : local_logger_; }
    const Logger& log() const { return logger_ ? *logger_ : local_logger_; }
    // Injected clock if one was passed in, otherwise the real one.
    const Clock& clock() const {
        if (clock_) return *clock_;
        return local_clock_;
    }

    // Lock order (deadlock-free): always acquire mutex_ BEFORE storage_mutex_, never the
    // reverse. Activity deletion additionally takes activity_boundary_mutex_ between them;
    // the engine's persistence tail takes activity_boundary_mutex_ before storage_mutex_.
    // Asynchronous emissions take neither: they carry and validate activity_epoch_ on the
    // UI thread. mutex_ guards mutable in-memory state; storage_mutex_ serializes all
    // storage_ access. Hot UI reads consume the immutable live snapshot and take neither.
    //
    // activity_epoch_ advances on activity deletion and on session start/stop/replace, so a
    // queued dispatch cannot paint after the user has moved on.
    //
    // RankedMutex enforces this order at runtime (see LockRank).
    mutable RankedMutex mutex_{LockRank::State};
    // Fences the off-lock persistence phase against activity deletion. Asynchronous UI
    // emissions carry activity_epoch_ and validate it in the dispatched UI closure.
    mutable RankedMutex activity_boundary_mutex_{LockRank::ActivityBoundary};
    mutable RankedMutex storage_mutex_{LockRank::Storage};
    // Lets the engine's persist jump ahead of queued Review reads; see
    // util/writer_priority.hpp.
    mutable WriterPriority storage_priority_;
    Storage storage_;
    std::filesystem::path app_data_dir_;
    Logger* logger_ = nullptr;
    Logger local_logger_{std::cerr};
    Clock* clock_ = nullptr;
    SystemClock local_clock_;
    EngineWakeSignal engine_wake_;
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
    // Delivery decision taken when the payload was latched; not recomputed at emit, so a
    // route that flips in between cannot skip the re-arm branch.
    AlertRoute latest_snapback_route_;
    // Claimable alert id per event kind; 0 means none (issued ids start at 1).
    std::int64_t next_alert_id_ = 0;
    std::array<std::int64_t, kAlertEventCount> actionable_alert_ids_{};
    std::optional<std::int64_t> last_prediction_at_ms_;
    AppSettings settings_;
    // Mutated under mutex_ by reload_classifier_model() and retry_model_deployment_cleanup().
    // Readers go through the snapshot's copy instead: this one holds std::strings, and
    // health() is deliberately lock-free.
    ModelDeploymentHealth model_deployment_health_;
    FocusMode focus_mode_ = FocusMode::Normal;
    double last_prediction_secs_ = -1.0;
    double last_event_secs_ = 0.0;  // timestamp of the most recent processed event
    bool prediction_dirty_ = false;  // a new prediction awaits emission this tick
    // Whether latest_snapback_ has already gone out as a `snapback` event. Emission and
    // lifetime are separate here: the event fires once, but the payload has to survive
    // until the user dismisses or restores it.
    bool snapback_emitted_ = false;
    std::uint64_t snapback_generation_ = 0;
    bool idle_ = false;              // user is currently AFK (mirrors idle_detector_ state)
    bool live_read_dirty_ = true;    // protected by mutex_; cleared after publication
    // Monotonic uptime at the last retention attempt. Seeded at construction because
    // Initial maintenance waits for the first painted frontend view.
    std::atomic<std::int64_t> last_prune_steady_ms_{0};
    // Uptime at the last "capture backlog" log line. Guarded by mutex_. Optional because 0 is
    // a valid steady-clock value.
    std::optional<std::int64_t> last_drain_backlog_log_ms_;
    // atomic_load/store free functions: Apple's libc++ lacks atomic<shared_ptr>.
    std::shared_ptr<const LiveReadSnapshot> live_read_snapshot_;
    std::atomic<std::uint64_t> activity_epoch_{0};

    EmitHook emit_hook_;
    std::thread engine_thread_;
    std::atomic<bool> engine_running_{false};
    std::atomic<std::uint64_t> engine_wakeups_{0};
    std::atomic<std::uint64_t> engine_max_drain_ms_{0};
    std::int64_t privacy_lapse_retry_at_ms_{};
    std::int64_t snooze_expiry_reported_wall_ms_{};
    // The tick only submits work. This owned worker deletes bounded batches while recording
    // is inactive and is cancelled/joined with the engine during shutdown.
    std::mutex maintenance_mutex_;
    std::condition_variable maintenance_ready_;
    std::thread maintenance_thread_;
    std::atomic<bool> frontend_ready_{false};
    std::atomic<bool> maintenance_pending_{false};
    std::atomic<bool> maintenance_running_{false};
    std::atomic<std::int64_t> maintenance_retry_at_ms_{0};
    std::atomic<std::uint64_t> maintenance_rows_deleted_{0};
    std::atomic<std::uint64_t> maintenance_elapsed_ms_{0};
    // 0 awaiting readiness, 1 pending, 2 running, 3 succeeded, 4 failed, 5 cancelled.
    std::atomic<int> maintenance_result_{0};
    std::function<void()> maintenance_test_hook_;  // guarded by storage_mutex_

    std::atomic<bool> maintenance_paused_{false};
    std::atomic<bool> maintenance_stopping_{false};
};

}  // namespace snapback
