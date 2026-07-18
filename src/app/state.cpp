#include "app/state.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "capture/permissions.hpp"
#include "engine/onnx_model.hpp"

namespace snapback {

AppState::AppState(Storage storage, std::filesystem::path app_data_dir)
    : storage_(std::move(storage)), app_data_dir_(std::move(app_data_dir)) {
    if (!app_data_dir_.empty()) {
        settings_ = load_app_settings(app_data_dir_);
    }
    focus_mode_ = settings_.default_focus_mode;
}

std::string AppState::now_rfc3339() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::int64_t AppState::steady_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool AppState::is_input_event(EventType type) {
    return type == EventType::KeyPress || type == EventType::KeyRelease ||
           type == EventType::MouseMove || type == EventType::MouseClick;
}

IdleTransition AppState::update_idle_unlocked(std::int64_t now_ms, bool had_input) {
    // on_activity resets the clock (and wakes us); poll then checks the threshold. A tick
    // with input can only ever wake us, never sleep us; a tick without input can only sleep.
    IdleTransition edge = IdleTransition::None;
    if (had_input) edge = idle_detector_.on_activity(now_ms);
    if (const auto poll_edge = idle_detector_.poll(now_ms); poll_edge != IdleTransition::None) {
        edge = poll_edge;
    }
    idle_ = idle_detector_.state() == IdleState::Idle;
    return edge;
}

bool AppState::is_idle() const {
    std::lock_guard lock(mutex_);
    return idle_;
}

IdleTransition AppState::update_idle_for_test(std::int64_t now_ms, bool had_input) {
    std::lock_guard lock(mutex_);
    return update_idle_unlocked(now_ms, had_input);
}

void AppState::start_engine() {
    bool expected = false;
    if (!engine_running_.compare_exchange_strong(expected, true)) return;

    {
        std::lock_guard state_lock(mutex_);
        std::lock_guard store_lock(storage_mutex_);
        reload_app_rules_unlocked();  // seed the cache before the tick thread reads it
        // Resume a session left ACTIVE by a prior run, so the tick's compute path never
        // needs to touch storage_ to discover it (keeps the hot path storage-free).
        if (!active_session_) active_session_ = storage_.active_session();
    }
    capture_.start();
    engine_thread_ = std::thread([this] {
        while (engine_running_.load(std::memory_order_relaxed)) {
            engine_tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void AppState::set_emit_hook(EmitHook hook) {
    std::lock_guard lock(mutex_);
    emit_hook_ = std::move(hook);
}

void AppState::stop_engine() {
    engine_running_.store(false, std::memory_order_relaxed);
    capture_.stop();
    if (engine_thread_.joinable()) engine_thread_.join();
}

SessionRecord AppState::start_session(const std::string& goal, FocusMode mode) {
    // Mixed (in-memory + storage): take both locks in the fixed order mutex_ -> storage_mutex_.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    focus_mode_ = mode;
    features_.reset_for_session(std::nullopt);
    context_tracker_.reset();
    active_session_ = storage_.create_session(goal, mode);
    last_prediction_secs_ = -1.0;
    reload_app_rules_unlocked();  // pick up any rules edited while idle
    return *active_session_;
}

void AppState::stop_session() {
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    if (!active_session_) {
        active_session_ = storage_.active_session();
    }
    if (active_session_) {
        storage_.end_session(active_session_->session_id);
        active_session_.reset();
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
    }
}

SessionRecord AppState::stop_session(const std::string& session_id) {
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    SessionRecord record = storage_.stop_session(session_id);
    try {
        storage_.save_auto_session_label(session_id);
    } catch (const std::exception& err) {
        std::cerr << "failed to save automatic session label: " << err.what() << '\n';
    }
    if (active_session_ && active_session_->session_id == session_id) {
        active_session_.reset();
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
    }
    return record;
}

std::optional<SessionRecord> AppState::get_session(const std::string& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.get_session(session_id);
}

HealthStatus AppState::health() const {
    std::lock_guard lock(mutex_);
    HealthStatus h;
    h.status = engine_running_.load(std::memory_order_relaxed) ? "online" : "offline";
    h.capture_running = capture_.running();
    h.capture_failed = false;
    h.capture_events_dropped = capture_.events_dropped();
    h.capture_stalled = false;
    h.permissions = check_capture_permissions(capture_.running());
    h.classifier.backend = classifier_.backend();
    h.classifier.onnx_runtime_enabled = classifier_.backend() == "onnx";
    h.classifier.model_path = OnnxModel::instance().model_path();
    return h;
}

std::optional<PredictionRecord> AppState::latest_prediction() const {
    // Common case: the cached value under mutex_ (never blocks on disk). Only the cold
    // startup path falls back to storage, taking storage_mutex_ after releasing mutex_.
    {
        std::lock_guard lock(mutex_);
        if (latest_prediction_) return latest_prediction_;
    }
    std::lock_guard lock(storage_mutex_);
    return const_cast<Storage&>(storage_).latest_prediction();
}

std::optional<SessionRecord> AppState::active_session() const {
    {
        std::lock_guard lock(mutex_);
        if (active_session_) return active_session_;
    }
    std::lock_guard lock(storage_mutex_);
    return const_cast<Storage&>(storage_).active_session();
}

std::optional<SnapbackPayload> AppState::latest_snapback() const {
    std::lock_guard lock(mutex_);
    return latest_snapback_;
}

std::optional<SnapbackPayload> AppState::take_snapback() {
    std::lock_guard lock(mutex_);
    auto out = std::move(latest_snapback_);
    latest_snapback_.reset();
    return out;
}

void AppState::dismiss_snapback() {
    std::lock_guard lock(mutex_);
    // Clear the pending payload and return the tracker from Recovering to Focused so it
    // doesn't keep the recovery state latched (Rust: tracker.dismiss_recovery()).
    latest_snapback_.reset();
    context_tracker_.dismiss_recovery(last_event_secs_);
}

SessionRecap AppState::session_recap(const std::string& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.recap(session_id);
}

std::vector<PredictionRecord> AppState::prediction_history(std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    return storage_.recent_predictions(limit);
}

std::vector<SessionSummary> AppState::session_history(std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    std::vector<SessionSummary> out;
    for (auto& record : storage_.recent_sessions(limit)) {
        out.push_back(SessionSummary{record, storage_.recap(record.session_id)});
    }
    return out;
}

ExportTrainingResult AppState::export_training_data(
    const std::filesystem::path& out_dir, const std::optional<std::string>& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.export_training_csv(out_dir, session_id);
}

void AppState::set_focus_mode(FocusMode mode) {
    std::lock_guard lock(mutex_);
    focus_mode_ = mode;
    settings_.default_focus_mode = mode;
    if (!app_data_dir_.empty()) {
        save_app_settings(app_data_dir_, settings_);
    }
}

AppSettings AppState::settings() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

std::vector<AppRuleRecord> AppState::app_rules() {
    std::lock_guard lock(storage_mutex_);
    return storage_.list_app_rules();
}

AppRuleRecord AppState::upsert_app_rule(const std::string& pattern, AppRuleKind rule_type,
                                        std::optional<std::string> note) {
    // Mixed: writes storage_ AND refreshes the in-memory app_rules_ cache the tick reads.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    auto record = storage_.upsert_app_rule(pattern, rule_type, std::move(note));
    reload_app_rules_unlocked();  // the live classifier picks up the change next tick
    return record;
}

void AppState::delete_app_rule(std::int64_t id) {
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    storage_.delete_app_rule(id);
    reload_app_rules_unlocked();
}

std::vector<ContextSnapshotDto> AppState::context_timeline(std::optional<std::string> session_id,
                                                           std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    std::string id;
    if (session_id) {
        id = *session_id;
    } else if (auto active = storage_.active_session()) {
        id = active->session_id;
    } else {
        return {};  // no session to show a timeline for
    }
    return storage_.list_context_snapshots(id, limit);
}

ClassifierStatus AppState::classifier_status() const {
    std::lock_guard lock(mutex_);
    ClassifierStatus status;
    status.backend = classifier_.backend();
    status.onnx_runtime_enabled = classifier_.backend() == "onnx";
    status.model_path = OnnxModel::instance().model_path();
    return status;
}

PermissionStatus AppState::refresh_permissions() {
    std::lock_guard lock(mutex_);
    return check_capture_permissions(capture_.running());
}

void AppState::reload_app_rules_unlocked() {
    app_rules_ = storage_.list_app_rules();
}

void AppState::submit_label(FocusLabel label, const std::string& source,
                            std::optional<std::string> notes) {
    // Mixed: reads in-memory active_session_ (falling back to storage) then writes storage.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    if (!active_session_) active_session_ = storage_.active_session();
    if (!active_session_) throw std::runtime_error("no active session");
    storage_.insert_label(active_session_->session_id, label, source, std::move(notes));
}

void AppState::submit_label(const std::string& session_id, FocusLabel label,
                            const std::string& source, std::optional<std::string> notes) {
    std::lock_guard lock(storage_mutex_);
    storage_.insert_label(session_id, label, source, std::move(notes));
}

void AppState::process_event_for_test(const CaptureEvent& event) {
    std::optional<PersistJob> job;
    {
        std::lock_guard lock(mutex_);
        job = compute_event(event);
    }
    if (job) {
        std::lock_guard lock(storage_mutex_);
        Storage::Transaction txn(storage_);
        persist(*job);
        txn.commit();
    }
}

void AppState::engine_tick() {
    // Three phases with different locks so a disk write never blocks a UI read:
    //   1) drain + classify under mutex_ (in-memory only), collecting persist jobs;
    //   2) flush them under storage_mutex_ in ONE transaction, after releasing mutex_;
    //   3) emit to the frontend holding no lock (the hook hops to the UI thread).
    EmitHook hook;
    std::optional<PredictionRecord> pred_to_emit;
    std::optional<SnapbackPayload> snap_to_emit;
    std::vector<PersistJob> jobs;
    IdleTransition idle_edge = IdleTransition::None;
    {
        std::lock_guard lock(mutex_);
        bool had_input = false;
        while (auto ev = capture_.next_event()) {
            if (is_input_event(ev->event_type)) had_input = true;
            if (auto job = compute_event(*ev)) jobs.push_back(std::move(*job));
        }
        // Idle timing runs off the tick's monotonic clock, not event timestamps: true AFK
        // means no events arrive at all, so we must measure wall time, not the last event.
        idle_edge = update_idle_unlocked(steady_now_ms(), had_input);
        hook = emit_hook_;
        if (prediction_dirty_) {
            pred_to_emit = latest_prediction_;
            prediction_dirty_ = false;
        }
        if (latest_snapback_) {
            snap_to_emit = std::move(latest_snapback_);
            latest_snapback_.reset();
        }
    }

    if (!jobs.empty()) {
        std::lock_guard lock(storage_mutex_);
        Storage::Transaction txn(storage_);  // one commit for the whole drain
        for (const auto& job : jobs) persist(job);
        txn.commit();
    }

    if (!hook) return;
    if (idle_edge == IdleTransition::WentIdle) hook("idle", "{\"idle\":true}");
    if (idle_edge == IdleTransition::WokeUp) hook("idle", "{\"idle\":false}");
    if (pred_to_emit) hook("prediction", nlohmann::json(*pred_to_emit).dump());
    if (snap_to_emit) hook("snapback", nlohmann::json(*snap_to_emit).dump());
}

std::optional<AppState::PersistJob> AppState::compute_event(const CaptureEvent& event) {
    // Requires mutex_. Pure in-memory: advances features/classifier/tracker + latest_*,
    // and returns the rows to write (nullopt if this event produced nothing to persist).
    // No storage I/O — persistence happens later under storage_mutex_.
    last_event_secs_ = event.timestamp_secs;

    PersistJob job;
    const bool have_session = active_session_.has_value();
    if (have_session) job.session_id = active_session_->session_id;

    if (have_session) {
        if (event.event_type == EventType::WindowFocusChange ||
            event.event_type == EventType::WindowTitleChange) {
            // Window changes drive the distraction state machine; eager timestamp is fine
            // here since these events are infrequent.
            job.context_snapshot = context_tracker_.observe_window_change(
                event.app_name, event.window_title, app_rules_, event.timestamp_secs, now_rfc3339());
        } else {
            // Defer the RFC3339 formatting until the tracker actually checkpoints (rare),
            // so the ~99% of key/mouse events don't pay for a timestamp they won't use.
            job.context_snapshot = context_tracker_.maybe_checkpoint_snapshot(
                app_rules_, event.timestamp_secs, [] { return now_rfc3339(); });
        }
        // The tracker latches a snapback payload on the return-from-distraction edge;
        // drain it into the field the tick loop emits.
        if (auto snapback = context_tracker_.take_pending_snapback()) {
            latest_snapback_ = *snapback;
        }
    }

    // AFK freeze: while idle we skip ingest + prediction so an empty window doesn't get
    // scored as "distracted" and idle minutes don't dilute the feature windows. Context
    // snapshots (window changes) still persist so the recovery timeline stays intact.
    if (idle_) {
        if (job.context_snapshot) return job;
        return std::nullopt;
    }

    // Always ingest (cheap bookkeeping); defer the O(window) extract() until we actually
    // produce a prediction. ~99% of events are throttled, so this skips almost all scans.
    features_.ingest(event);
    const double now = event.timestamp_secs;

    if (last_prediction_secs_ >= 0.0 && now - last_prediction_secs_ < 1.0) {
        // Throttled: no new prediction this event. Persist only a context snapshot if one
        // was produced; otherwise there's nothing to write.
        if (job.context_snapshot) return job;
        return std::nullopt;
    }
    last_prediction_secs_ = now;

    const auto features = features_.extract(now, app_rules_);
    const auto goal =
        have_session ? std::optional<std::string>(active_session_->goal) : std::nullopt;
    const auto scores = classifier_.predict(features, focus_mode_, goal, app_rules_);
    features_.update_focus_score(scores.focus_score / 100.0, 0.2);
    context_tracker_.set_prediction_feedback(scores.focus_state, goal);

    PredictionRecord record;
    record.session_id = have_session ? active_session_->session_id : "";
    record.focus_score = scores.focus_score;
    record.distraction_risk = scores.distraction_risk;
    record.focus_state = scores.focus_state;
    record.thrash_score = scores.thrash_score;
    record.drift_score = scores.drift_score;
    record.goal_alignment = scores.goal_alignment;
    record.timestamp = now_rfc3339();
    latest_prediction_ = record;
    prediction_dirty_ = true;  // engine_tick emits this after unlocking

    if (have_session) {
        job.prediction = std::move(record);
        job.features = features;
    }
    return job;
}

void AppState::persist(const PersistJob& job) {
    // Requires storage_mutex_ (call inside a Storage::Transaction).
    if (job.session_id.empty()) return;
    if (job.context_snapshot) {
        storage_.save_context_snapshot(job.session_id, *job.context_snapshot);
    }
    if (job.prediction) {
        storage_.insert_prediction(*job.prediction);
        if (job.features) storage_.insert_feature_snapshot(job.session_id, *job.features);
    }
}

}  // namespace snapback
