#include "app/state.hpp"

#include <chrono>
#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "capture/permissions.hpp"
#include "engine/app_context.hpp"
#include "engine/onnx_model.hpp"

namespace snapback {
namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string cutoff_rfc3339(int days_ago) {
    const auto now = std::chrono::system_clock::now() - std::chrono::hours(24 * days_ago);
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

int timestamp_hour(const std::string& timestamp) {
    if (timestamp.size() < 13 || timestamp[10] != 'T' ||
        !std::isdigit(static_cast<unsigned char>(timestamp[11])) ||
        !std::isdigit(static_cast<unsigned char>(timestamp[12]))) return -1;
    return (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
}

}  // namespace

AppState::AppState(Storage storage, std::filesystem::path app_data_dir, Logger* logger)
    : storage_(std::move(storage)), app_data_dir_(std::move(app_data_dir)), logger_(logger) {
    if (!app_data_dir_.empty()) {
        settings_ = load_app_settings(app_data_dir_);
    }
    focus_mode_ = settings_.default_focus_mode;
    context_tracker_.set_goal_categories(settings_.goal_categories);
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

PomodoroStatus AppState::start_pomodoro_unlocked(std::int64_t now_ms) {
    if (!active_session_) throw std::runtime_error("no active session");
    pomodoro_.start(now_ms);
    return pomodoro_.status(now_ms);
}

PomodoroStatus AppState::start_pomodoro() {
    std::lock_guard lock(mutex_);
    return start_pomodoro_unlocked(steady_now_ms());
}

PomodoroStatus AppState::start_pomodoro_for_test(std::int64_t now_ms) {
    std::lock_guard lock(mutex_);
    return start_pomodoro_unlocked(now_ms);
}

PomodoroStatus AppState::stop_pomodoro() {
    std::lock_guard lock(mutex_);
    pomodoro_.stop();
    return pomodoro_.status(steady_now_ms());
}

PomodoroStatus AppState::pomodoro_status() const {
    std::lock_guard lock(mutex_);
    return pomodoro_.status(steady_now_ms());
}

std::optional<PomodoroStatus> AppState::update_pomodoro_for_test(std::int64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (!pomodoro_.poll(now_ms)) return std::nullopt;
    return pomodoro_.status(now_ms);
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
    context_tracker_.set_goal_categories(settings_.goal_categories);
    pomodoro_.reset();
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
    pomodoro_.reset();
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
        std::ostringstream msg;
        msg << "failed to save automatic session label: " << err.what();
        log().warn(msg.str());
    }
    if (active_session_ && active_session_->session_id == session_id) {
        pomodoro_.reset();
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

DiagnosticsSnapshot AppState::diagnostics() const {
    return DiagnosticsSnapshot{health(), log().recent_lines()};
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

FocusSummary AppState::focus_summary(std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    return summarize_predictions(storage_.recent_predictions(limit));
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

PrivacySettings AppState::privacy_settings() const {
    std::lock_guard lock(mutex_);
    return PrivacySettings{settings_.private_mode, settings_.excluded_apps, true};
}

void AppState::set_private_mode(bool enabled) {
    std::lock_guard lock(mutex_);
    settings_.private_mode = enabled;
    if (!app_data_dir_.empty()) save_app_settings(app_data_dir_, settings_);
}

void AppState::set_privacy_exclusions(std::vector<std::string> exclusions) {
    std::lock_guard lock(mutex_);
    settings_.excluded_apps = normalize_privacy_exclusions(std::move(exclusions));
    if (!app_data_dir_.empty()) save_app_settings(app_data_dir_, settings_);
}

AnalyticsSummary AppState::analytics() const {
    std::lock_guard lock(storage_mutex_);
    AnalyticsSummary summary;
    struct Bucket {
        std::size_t count{};
        double focus_sum{};
        std::size_t distracted{};
    };
    std::array<Bucket, 24> buckets{};
    const auto predictions = const_cast<Storage&>(storage_).recent_predictions(10000);
    for (const auto& prediction : predictions) {
        ++summary.sample_count;
        summary.avg_focus_score += prediction.focus_score;
        const int hour = timestamp_hour(prediction.timestamp);
        if (hour < 0 || hour >= 24) continue;
        auto& bucket = buckets[static_cast<std::size_t>(hour)];
        ++bucket.count;
        bucket.focus_sum += prediction.focus_score;
        if (prediction.focus_state == "DISTRACTED") ++bucket.distracted;
    }
    if (summary.sample_count > 0) {
        summary.avg_focus_score /= static_cast<double>(summary.sample_count);
    }
    for (int hour = 0; hour < 24; ++hour) {
        const auto& bucket = buckets[static_cast<std::size_t>(hour)];
        if (bucket.count == 0) continue;
        summary.hourly.push_back(AnalyticsHour{
            hour, bucket.count, bucket.focus_sum / static_cast<double>(bucket.count),
            static_cast<double>(bucket.distracted) / static_cast<double>(bucket.count)});
    }

    std::unordered_map<std::string, std::size_t> app_counts;
    for (const auto& session : const_cast<Storage&>(storage_).recent_sessions(200)) {
        for (const auto& snapshot : const_cast<Storage&>(storage_).list_context_snapshots(
                 session.session_id, 200)) {
            if (!snapshot.app_name.empty()) ++app_counts[snapshot.app_name];
        }
    }
    std::vector<AnalyticsApp> apps;
    apps.reserve(app_counts.size());
    for (const auto& [app, count] : app_counts) apps.push_back(AnalyticsApp{app, count});
    std::sort(apps.begin(), apps.end(), [](const auto& left, const auto& right) {
        if (left.window_count != right.window_count) return left.window_count > right.window_count;
        return left.app_name < right.app_name;
    });
    if (apps.size() > 5) apps.resize(5);
    summary.top_apps = std::move(apps);

    for (const auto& session : const_cast<Storage&>(storage_).recent_sessions(200)) {
        if (session.status != "COMPLETED") continue;
        if (const auto recap = const_cast<Storage&>(storage_).recap(session.session_id);
            recap.avg_focus_score >= 70.0) {
            ++summary.productive_session_streak;
        } else {
            break;
        }
    }
    return summary;
}

SummaryReport AppState::summary_report(const std::string& window) const {
    if (window != "day" && window != "week") {
        throw std::runtime_error("summary window must be day or week");
    }
    std::lock_guard lock(storage_mutex_);
    const auto cutoff = cutoff_rfc3339(window == "day" ? 1 : 7);
    SummaryReport report;
    report.window = window;
    report.generated_at = now_rfc3339();

    std::size_t distracted = 0;
    std::size_t current_streak = 0;
    std::unordered_map<std::string, std::size_t> context_counts;
    for (const auto& prediction : const_cast<Storage&>(storage_).recent_predictions(10000)) {
        if (prediction.timestamp < cutoff) continue;
        ++report.sample_count;
        report.avg_focus_score += prediction.focus_score;
        if (prediction.focus_state == "DISTRACTED") {
            ++distracted;
            current_streak = 0;
        } else {
            ++current_streak;
            report.longest_focus_streak =
                std::max(report.longest_focus_streak, current_streak);
        }
    }
    if (report.sample_count > 0) {
        report.avg_focus_score /= static_cast<double>(report.sample_count);
        report.distracted_fraction =
            static_cast<double>(distracted) / static_cast<double>(report.sample_count);
    }

    for (const auto& session : const_cast<Storage&>(storage_).recent_sessions(500)) {
        if (!session.started_at || *session.started_at < cutoff) continue;
        ++report.session_count;
        if (session.status == "COMPLETED") {
            report.focus_seconds += const_cast<Storage&>(storage_).recap(session.session_id).duration_secs;
        }
        for (const auto& snapshot : const_cast<Storage&>(storage_).list_context_snapshots(
                 session.session_id, 200)) {
            if (!snapshot.app_name.empty()) ++context_counts[snapshot.app_name];
        }
    }
    for (const auto& [app, count] : context_counts) {
        if (report.top_context_app.empty() || count > context_counts[report.top_context_app] ||
            (count == context_counts[report.top_context_app] && app < report.top_context_app)) {
            report.top_context_app = app;
        }
    }
    return report;
}

SummaryExportResult AppState::export_summary_report(const std::filesystem::path& out_dir,
                                                    const std::string& window) const {
    const auto report = summary_report(window);
    std::filesystem::create_directories(out_dir);
    const auto path = out_dir / ("summary_" + window + ".json");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("failed to write summary report");
    out << nlohmann::json(report).dump(2) << '\n';
    return SummaryExportResult{window, path.string()};
}

std::vector<GoalCategory> AppState::goal_categories() const {
    std::lock_guard lock(mutex_);
    return settings_.goal_categories.empty() ? snapback::default_goal_categories()
                                              : settings_.goal_categories;
}

void AppState::set_goal_categories(std::vector<GoalCategory> categories) {
    std::vector<GoalCategory> normalized;
    for (auto& category : categories) {
        category.name = trim_copy(std::move(category.name));
        if (category.name.empty()) continue;
        std::vector<std::string> keywords;
        for (auto& keyword : category.keywords) {
            keyword = trim_copy(std::move(keyword));
            if (!keyword.empty()) keywords.push_back(std::move(keyword));
        }
        if (!keywords.empty()) normalized.push_back(GoalCategory{std::move(category.name), std::move(keywords)});
    }
    std::lock_guard lock(mutex_);
    settings_.goal_categories = std::move(normalized);
    context_tracker_.set_goal_categories(settings_.goal_categories);
    if (!app_data_dir_.empty()) save_app_settings(app_data_dir_, settings_);
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

PermissionStatus AppState::request_permissions() {
    std::lock_guard lock(mutex_);
    // Prompt first, then re-probe so the returned status reflects the user's answer in the
    // same round trip (the macOS dialog is modal, so by the time this returns they've
    // decided). Re-probing rather than trusting the prompt's return value keeps one code
    // path — check_capture_permissions — as the single source of truth for the status DTO.
    request_capture_permissions();
    return check_capture_permissions(capture_.running());
}

void AppState::reload_app_rules_unlocked() {
    app_rules_ = storage_.list_app_rules();
}

std::vector<std::string> AppState::normalize_privacy_exclusions(
    std::vector<std::string> exclusions) {
    std::vector<std::string> out;
    for (auto& exclusion : exclusions) {
        exclusion = trim_copy(std::move(exclusion));
        if (exclusion.empty()) continue;
        const auto lowered = lower_copy(exclusion);
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const auto& existing) {
            return lower_copy(existing) == lowered;
        });
        if (!duplicate) out.push_back(std::move(exclusion));
    }
    return out;
}

bool AppState::is_private_event_unlocked(const CaptureEvent& event) const {
    if (settings_.private_mode) return true;
    const auto app = lower_copy(event.app_name);
    return std::any_of(settings_.excluded_apps.begin(), settings_.excluded_apps.end(),
                       [&](const auto& exclusion) {
                           return app.find(lower_copy(exclusion)) != std::string::npos;
                       });
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
    std::optional<PomodoroStatus> pomodoro_to_emit;
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
        const auto now_ms = steady_now_ms();
        idle_edge = update_idle_unlocked(now_ms, had_input);
        if (pomodoro_.poll(now_ms)) pomodoro_to_emit = pomodoro_.status(now_ms);
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
    if (pomodoro_to_emit) hook("pomodoro", nlohmann::json(*pomodoro_to_emit).dump());
}

std::optional<AppState::PersistJob> AppState::compute_event(const CaptureEvent& event) {
    // Requires mutex_. Pure in-memory: advances features/classifier/tracker + latest_*,
    // and returns the rows to write (nullopt if this event produced nothing to persist).
    // No storage I/O — persistence happens later under storage_mutex_.
    if (is_private_event_unlocked(event)) return std::nullopt;
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
    const auto scores = classifier_.predict(features, focus_mode_, goal, app_rules_,
                                             settings_.goal_categories);
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
