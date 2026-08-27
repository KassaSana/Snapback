// Central application state and the engine tick loop.
//
// Owns storage, the capture thread, the classifier, and the snapback tracker, and
// runs the engine loop that ties them together. The shared object is guarded by
// std::mutex where mutation crosses threads.
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

namespace snapback {

inline constexpr std::int64_t kCaptureStallThresholdMs = 30'000;

// How much uptime passes between retention prunes. Snapback closes to the tray and is meant
// to run for weeks, so "prune on open" -- which was the only prune -- meant a user who never
// restarts kept every row past the retention window until their next reboot.
inline constexpr std::int64_t kRetentionPruneIntervalMs = 24 * 60 * 60 * 1000;

class AppState {
public:
    // `logger` and `clock` are both optional (default null) so existing call sites keep
    // compiling unchanged. Pass a logger to route non-fatal warnings (e.g. a failed
    // auto-label save) somewhere other than stderr.
    //
    // ROADMAP 11.4: pass a `clock` to make time an input rather than an ambient fact. The
    // engine's idle threshold, the pomodoro's 25 minutes, and the one-prediction-per-second
    // throttle are all durations no sleep-based test can reach, so before this seam they were
    // exercised only through `_for_test` methods that took `now_ms` as an argument — which is
    // exactly what 7.14 objects to. A test clock can advance an hour instantly.
    explicit AppState(Storage storage, std::filesystem::path app_data_dir = {},
                      Logger* logger = nullptr, Clock* clock = nullptr,
                      ModelDeploymentHealth model_deployment = {});
    ~AppState() noexcept;

    // Spawn capture and the engine tick thread.
    void start_engine();
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
    std::optional<PredictionRecord> latest_prediction() const;
    std::optional<SessionRecord> active_session() const;

    // Snapback context-recovery payload. The engine tick stores the latest one and emits
    // it once (see snapback_emitted_); latest_snapback() peeks, take_snapback() drains it,
    // dismiss_snapback() clears it on the frontend's request. The payload outlives its
    // emission so restore_snapback_target() still has a target when the user clicks.
    std::optional<SnapbackPayload> latest_snapback() const;
    std::optional<SnapbackPayload> take_snapback();
    void dismiss_snapback();
    // Roadmap 2.8 ("Take me back"): activates the target application/window of the latest snapback,
    // then dismisses the snapback.
    FocusTargetResult restore_snapback_target();
    SessionRecap session_recap(const std::string& session_id);

    // Roadmap 2.14. Saves the optional end-of-session reflection. nullopt result means no such
    // session. Either field may be nullopt to leave (or clear) that answer.
    std::optional<SessionRecord> save_session_reflection(
        const std::string& session_id, const std::optional<std::string>& done,
        const std::optional<std::string>& next_step);
    std::vector<PredictionRecord> prediction_history(std::size_t limit);
    // Aggregate the most recent `limit` predictions into recap stats (avg/peak/streak).
    FocusSummary focus_summary(std::size_t limit = 200);
    std::vector<SessionSummary> session_history(std::size_t limit);
    // Erases every app-owned copy of the user's activity and reports what happened to each.
    //
    // Roadmap 8.12. Returns a result rather than void because the operation can legitimately
    // half-succeed — a stale export held open by another program does not stop the database
    // being cleared — and "permanently deleted" must never be said over a partial one. Every
    // target is attempted regardless of what happened to the ones before it.
    ActivityDeletionResult delete_all_activity_data();

    // Removes one session and everything recorded during it. Returns false when no such
    // session exists. If it is the session currently being filled, the live engine state is
    // reset too, so nothing keeps writing to a row that is gone.
    bool delete_session(const std::string& session_id);
    ExportTrainingResult export_training_data(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);

    // Roadmap 7.6: the human-readable counterpart to export_training_data — what Snapback
    // recorded *about you*, as Markdown, rather than a feature matrix for a model.
    //
    // Roadmap 9.16: **complete**. The caps this used to take (200 sessions, 500 windows each)
    // are gone; what remains is `page_size`, which is how many rows are held in memory at once
    // rather than how many are written. A small value in a test therefore exercises the paging
    // without changing the output — the previous arguments changed the *answer*, which is how
    // an export that omitted history could pass its own tests.
    PersonalArchiveExport export_personal_data(const std::filesystem::path& out_dir,
                                               std::size_t page_size = 200);
    void set_focus_mode(FocusMode mode);
    AppSettings settings() const;
    PrivacySettings privacy_settings() const;
    void set_private_mode(bool enabled);
    // Roadmap 2.10. The one answer to "am I being recorded right now?", derived here so the
    // header and the tray cannot compute it differently. Also lapses an expired timed pause,
    // so nobody has to poll a deadline separately to keep the answer honest.
    RecordingStatus recording_status();
    // Turns private mode on for a fixed stretch. 0 minutes means indefinite (the old
    // behaviour). Returns the resulting status so the caller renders what was accepted.
    RecordingStatus pause_privately_for(std::int64_t minutes);
    // Roadmap 2.16. Silences alert *delivery* for a stretch, leaving recording alone. 0
    // minutes means the default 30. Returns the resulting status so the caller renders what
    // was accepted rather than what it asked for.
    RecordingStatus snooze_alerts_for(std::int64_t minutes);
    RecordingStatus resume_alerts();
    RecordingStatus resume_from_private_pause();
    // Suppress the missed-session nudge for a deliberate interval (60 minutes by default).
    void dismiss_untracked_nudge(std::int64_t minutes = 60);
    // Roadmap 7.23. How long without input pauses attended time. Throws (changing nothing)
    // outside [kMinIdleThresholdSecs, kMaxIdleThresholdSecs].
    void set_idle_threshold_secs(std::int64_t seconds);
    void set_privacy_exclusions(std::vector<std::string> exclusions);
    AnalyticsSummary analytics(const std::string& window = "all",
                               const std::optional<std::string>& since = std::nullopt) const;
    SummaryReport summary_report(const std::string& window,
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
    // Roadmap 2.13. Pause/resume freeze and continue the current phase; skip ends it early
    // (without crediting an unfinished work interval); restart replays it; acknowledge begins
    // the phase that has been waiting since the last boundary. Each persists the timer so a
    // relaunch resumes where the user left it.
    PomodoroStatus pause_pomodoro();
    PomodoroStatus resume_pomodoro();
    PomodoroStatus skip_pomodoro_phase();
    PomodoroStatus restart_pomodoro_phase();
    PomodoroStatus acknowledge_pomodoro_phase();
    // Phase lengths, long-break cadence, and auto-start. Applies from the next phase on.
    PomodoroStatus set_pomodoro_config(const PomodoroConfig& config);
    PomodoroConfig pomodoro_config() const;
    // Roadmap 2.19. Opt-in attended-minute targets and how today/this week compare. Targets of
    // 0 mean "not set"; setting 0 turns a target off.
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
    // ROADMAP 7.14: the three seams below used to be public.
    //
    // They exist because the engine tick is the only production caller of the idle and
    // pomodoro state machines, and it reads the clock itself — so a deterministic test had to
    // pass `now_ms` in by hand. 11.4's injected clock removed that need for
    // `start_pomodoro_for_test`, which is **deleted**: a test sets a `ManualClock` and calls
    // the real `start_pomodoro()`.
    //
    // The remaining three cannot be deleted the same way, because their production entry
    // point is the tick *thread* rather than a method — driving them through public API would
    // mean running the engine and waiting, which is the sleep-based testing 11.4 exists to
    // avoid. So they are private, reachable only through `AppStateTestAccess`
    // (`tests/app_state_test_access.hpp`). That is a smaller claim than "gone", and the
    // difference is stated honestly on 7.14: they are no longer *public API* — nothing outside
    // the tests can call them, and they cannot be mistaken for supported behaviour — but they
    // are still compiled in. Closing that gap needs a synchronous `tick_once` seam, which is a
    // design question rather than an access-control one.
    friend struct AppStateTestAccess;

    // Immutable state published after a mutation finishes. Live UI reads load this snapshot
    // without joining the engine's compute critical section; storage-backed reads retain
    // their existing storage seam. The snapshot is private because callers should keep using
    // AppState's domain interface rather than learning its publication mechanism.
    struct LiveReadSnapshot {
        std::optional<SessionRecord> active_session;
        std::optional<PredictionRecord> latest_prediction;
        std::optional<SnapbackPayload> latest_snapback;
        std::optional<std::int64_t> last_prediction_at_ms;
        ClassifierStatus classifier;
        ModelDeploymentHealth model_deployment;
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
        // Roadmap 2.15. Carried on the same job — and therefore the same transaction and the
        // same activity epoch — as the event that produced it, so an episode cannot survive a
        // "delete all" that removes the rows it describes.
        std::optional<SnapbackEpisode> snapback_episode;
    };

    void engine_tick();  // features -> classifier -> tracker -> (emit) ; persist off-lock
    // Runs the event through features/classifier/tracker and updates in-memory state.
    // Requires mutex_. Does NO storage I/O — returns what to persist (nullopt if nothing).
    std::optional<PersistJob> compute_event(const CaptureEvent& event);

    // Roadmap 7.23. The session-span change an idle edge implies, decided under mutex_ by
    // update_idle_unlocked and written by engine_tick under storage_mutex_. Split so the
    // decision sits beside the idle logic that knows why, while the disk write stays out of
    // the lock every UI read takes.
    // Roadmap 2.7 / ADR-0005. Nothing is recorded without a session, so someone who forgets
    // to press Start gets no data at all and is never told. This notices sustained work with
    // no session and asks once per stretch.
    //
    // Latched like the hyperfocus nudge, and cleared both when a session starts and when the
    // user goes idle — going idle ends the stretch, so coming back begins a new one rather
    // than immediately re-firing.
    static constexpr std::int64_t kUntrackedNudgeMinutes = 15;
    std::optional<std::int64_t> untracked_since_ms_;
    bool untracked_latched_ = false;
    std::optional<std::uint64_t> untracked_minutes_;  // pending emit, drained by the tick
    // Last capture event's app. Excluded-app time resets the untracked stretch (2.7).
    std::string last_capture_app_;

    // The session a pending span decision belongs to. Carried with the decision because the
    // decision outlives the moment it was made: phase 1 records it, phase 2 writes it, and
    // the session can be stopped, replaced, or deleted in between. Without the id there is
    // nothing to invalidate against.
    std::optional<std::string> pending_span_session_;
    std::int64_t pending_span_secs_ago_ = 0;  // how far to back-date a pause
    bool pending_span_opens_ = false;  // true = the user came back, false = they went away
    // Whether the active session currently has a span open. Attendance is tracked as a level
    // rather than inferred from idle edges alone, because it also changes at start, stop,
    // shutdown, and crash hydration — none of which produce an edge. Guarded by mutex_.
    bool session_attended_ = false;
    // Closes a span a previous process left open, at the session's last recorded activity.
    // Requires mutex_ + storage_mutex_ (the constructor runs before either can be contended).
    void hydrate_session_attendance_unlocked();
    // Restores the live state that belongs to a hydrated active session: its saved focus mode
    // and the feature extractor's session origin (Roadmap 7.25). Same locking note as above.
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
    // Roadmap 7.26. The one path every settings mutation takes: write the candidate to disk,
    // then commit it in memory, then publish it to live state. Requires mutex_.
    //
    // A throw from the write leaves `settings_` and every live field untouched, which is the
    // guarantee the setters could not previously make — they mutated first and saved second,
    // so a failed save reported an error while the process kept the new behaviour.
    //
    // `publish` runs only after the commit and must not throw: its only job is to copy
    // already-committed settings into the live fields that mirror them.
    void commit_settings_unlocked(AppSettings candidate, const std::function<void()>& publish);
    // Roadmap 2.10. Ends a timed privacy pause whose deadline has passed. Requires mutex_.
    bool lapse_private_pause_unlocked();
    void save_auto_session_label_unlocked(const std::string& session_id);
    // Drops the pending snapback payload and its emitted flag together. Requires mutex_.
    //
    // One helper rather than the two-line pair repeated at each site, because the pair being
    // hand-written is what let three session-lifecycle paths forget it. The flag is not
    // independent state — a payload that is gone cannot meaningfully have been emitted — so
    // clearing one without the other has no correct meaning to express.
    void clear_snapback_unlocked();
    void reload_app_rules_unlocked();  // refresh app_rules_; requires mutex_ + storage_mutex_
    static std::vector<std::string> normalize_privacy_exclusions(
        std::vector<std::string> exclusions);
    bool app_matches_exclusion_unlocked(const std::string& app_name) const;
    bool is_private_event_unlocked(const CaptureEvent& event) const;
    // ROADMAP 11.4: these were static and read the process clock directly. They now go
    // through clock(), so an injected clock reaches every timestamp the engine writes and
    // every duration it measures. Non-static as a consequence, which is the point — reading
    // the time is now something an *instance* does, not something anyone can do from
    // anywhere.
    // ADR-0007. One wall reading, in UTC epoch milliseconds. `rfc3339_at`, `now_rfc3339`, and
    // `wall_now_ms` were three names for two representations of the same thing; formatting now
    // happens at the edges that display a time, in `util/time.hpp`.
    std::int64_t now_unix_ms() const;
    // Wall-clock instant `secs` in the past. Used to stamp a pause at the moment the user
    // actually stopped rather than when the idle threshold noticed (Roadmap 7.23).
    std::int64_t unix_ms_secs_ago(std::int64_t secs) const;
    std::int64_t steady_now_ms() const;  // monotonic clock for idle timing
    // Roadmap 2.16. The delivery decision for one interruption, taken here so the policy in
    // app/alert_routing.hpp stays clock-free and the local-time conversion happens exactly
    // once per alert. Requires mutex_: it reads settings_.
    AlertRoute alert_route_unlocked(AlertEvent event) const;
    static bool is_input_event(EventType type);  // key/mouse = real user activity
    // Advance the idle state machine one step. Requires mutex_. Returns the transition
    // edge so the tick loop can emit it. Sets idle_ from the resulting state.
    IdleTransition update_idle_unlocked(std::int64_t now_ms, bool had_input);
    PomodoroStatus start_pomodoro_unlocked(std::int64_t now_ms);
    // Roadmap 2.13. Writes the timer's position into settings.json; best-effort, so a failed
    // save costs the relaunch-resume but never the running phase.
    void persist_pomodoro_unlocked();
    PomodoroStatus mutate_pomodoro(const std::function<void(std::int64_t)>& apply);
    // Injected logger if one was passed in, otherwise the stderr fallback below.
    Logger& log() { return logger_ ? *logger_ : local_logger_; }
    const Logger& log() const { return logger_ ? *logger_ : local_logger_; }
    // Same shape for time: injected clock if one was passed in, otherwise the real one.
    // Written as an if rather than a ternary because the two arms differ in both type and
    // constness, which the conditional operator will not reconcile.
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
    // ROADMAP 11.6: the paragraph above is no longer the only thing holding that order.
    // These are RankedMutex, so an inverted acquisition reports itself on the first run
    // through the bad path rather than waiting for two threads to collide. The ranks in
    // LockRank are the same ordering, written where the lock is instead of where the
    // convention is. Every call site is `std::lock_guard lock(...)` with a deduced argument,
    // so they needed no change.
    mutable RankedMutex mutex_{LockRank::State};
    // Fences the off-lock persistence phase against activity deletion. Asynchronous UI
    // emissions carry activity_epoch_ and validate it in the dispatched UI closure.
    mutable RankedMutex activity_boundary_mutex_{LockRank::ActivityBoundary};
    mutable RankedMutex storage_mutex_{LockRank::Storage};
    Storage storage_;
    std::filesystem::path app_data_dir_;
    Logger* logger_ = nullptr;
    Logger local_logger_{std::cerr};
    Clock* clock_ = nullptr;
    SystemClock local_clock_;
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
    // Roadmap 2.16. The delivery decision taken when this payload was latched, carried to the
    // emit block so main.cpp can act on it. Decided once, at the latch, rather than recomputed
    // at emit: a route that flipped to suppressed in between would skip delivery *without*
    // going through the re-arm branch, which is the one path that must never be reachable.
    AlertRoute latest_snapback_route_;
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
    bool idle_ = false;              // user is currently AFK (mirrors idle_detector_ state)
    bool live_read_dirty_ = true;    // protected by mutex_; cleared after publication
    // Uptime at the last retention prune. Monotonic, not wall clock: this measures how long
    // the process has been up, so a system clock jump cannot make a prune overdue or
    // unreachable. Seeded at construction because Storage::open just pruned.
    std::int64_t last_prune_steady_ms_ = 0;
    // Use the shared_ptr atomic free functions instead of atomic<shared_ptr>: the Apple
    // libc++ shipped with the supported command-line tools does not provide the C++20 class
    // specialization, while atomic_load/store(shared_ptr*) are available cross-platform.
    std::shared_ptr<const LiveReadSnapshot> live_read_snapshot_;
    std::atomic<std::uint64_t> activity_epoch_{0};

    EmitHook emit_hook_;
    std::thread engine_thread_;
    std::atomic<bool> engine_running_{false};
};

}  // namespace snapback
