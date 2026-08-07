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
#include "engine/idle_detector.hpp"
#include "engine/pomodoro.hpp"
#include "snapback/tracker.hpp"
#include "app/data_export.hpp"
#include "app/settings.hpp"
#include "storage/storage.hpp"
#include "types.hpp"
#include "util/clock.hpp"
#include "util/logger.hpp"
#include "util/ranked_mutex.hpp"

namespace snapback {

inline constexpr std::int64_t kCaptureStallThresholdMs = 30'000;

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
                      Logger* logger = nullptr, Clock* clock = nullptr);
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
    void delete_all_activity_data();

    // Removes one session and everything recorded during it. Returns false when no such
    // session exists. If it is the session currently being filled, the live engine state is
    // reset too, so nothing keeps writing to a row that is gone.
    bool delete_session(const std::string& session_id);
    ExportTrainingResult export_training_data(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);

    // Roadmap 7.6: the human-readable counterpart to export_training_data — what Snapback
    // recorded *about you*, as Markdown, rather than a feature matrix for a model. Caps are
    // arguments rather than constants so a test can drive truncation with three rows instead
    // of thousands, and so the honest "there is more than this" line has a test at all.
    PersonalArchiveExport export_personal_data(const std::filesystem::path& out_dir,
                                               std::size_t session_limit = 200,
                                               std::size_t windows_per_session = 500);
    void set_focus_mode(FocusMode mode);
    AppSettings settings() const;
    PrivacySettings privacy_settings() const;
    void set_private_mode(bool enabled);
    // Roadmap 7.23. How long without input pauses attended time. Throws (changing nothing)
    // outside [kMinIdleThresholdSecs, kMaxIdleThresholdSecs].
    void set_idle_threshold_secs(std::int64_t seconds);
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
    ClassifierStatus reload_classifier_model();
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

    void start_engine_impl(InputHook* hook);
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
    // Closes the open span on the way out, so a clean exit does not look like a crash to the
    // next launch. Takes both locks itself; safe to call when nothing is open.
    void close_open_span_on_shutdown() noexcept;

    // Hyperfocus guardrail state. `hyperfocus_latched_` prevents a second nudge inside the
    // same unbroken stretch; `hyperfocus_minutes_` is the pending emit drained by the tick.
    bool hyperfocus_latched_ = false;
    std::optional<std::uint64_t> hyperfocus_minutes_;
    // Writes a job to storage. Requires storage_mutex_ (call inside a Transaction).
    void persist(const PersistJob& job);
    void save_auto_session_label_unlocked(const std::string& session_id);
    void reload_app_rules_unlocked();  // refresh app_rules_; requires mutex_ + storage_mutex_
    static std::vector<std::string> normalize_privacy_exclusions(
        std::vector<std::string> exclusions);
    bool is_private_event_unlocked(const CaptureEvent& event) const;
    // ROADMAP 11.4: these were static and read the process clock directly. They now go
    // through clock(), so an injected clock reaches every timestamp the engine writes and
    // every duration it measures. Non-static as a consequence, which is the point — reading
    // the time is now something an *instance* does, not something anyone can do from
    // anywhere.
    std::string rfc3339_at(std::time_t when) const;
    std::string now_rfc3339() const;
    // Wall-clock timestamp `secs` in the past. Used to stamp a pause at the moment the user
    // actually stopped rather than when the idle threshold noticed (Roadmap 7.23).
    std::string rfc3339_secs_ago(std::int64_t secs) const;
    std::int64_t steady_now_ms() const;  // monotonic clock for idle timing
    static bool is_input_event(EventType type);  // key/mouse = real user activity
    // Advance the idle state machine one step. Requires mutex_. Returns the transition
    // edge so the tick loop can emit it. Sets idle_ from the resulting state.
    IdleTransition update_idle_unlocked(std::int64_t now_ms, bool had_input);
    PomodoroStatus start_pomodoro_unlocked(std::int64_t now_ms);
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
    std::optional<std::int64_t> last_prediction_at_ms_;
    AppSettings settings_;
    FocusMode focus_mode_ = FocusMode::Normal;
    double last_prediction_secs_ = -1.0;
    double last_event_secs_ = 0.0;  // timestamp of the most recent processed event
    bool prediction_dirty_ = false;  // a new prediction awaits emission this tick
    bool idle_ = false;              // user is currently AFK (mirrors idle_detector_ state)
    bool live_read_dirty_ = true;    // protected by mutex_; cleared after publication
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
