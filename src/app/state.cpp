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
#include "app/version.hpp"
#include "app/notification.hpp"
#include "app/training_deploy.hpp"
#include "engine/app_context.hpp"
#include "engine/focus_modes.hpp"
#include "engine/onnx_model.hpp"
#include "util/time.hpp"

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

bool is_app_name_word_char(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

bool contains_whole_app_name_phrase(const std::string& app, const std::string& phrase) {
    if (phrase.empty()) return false;
    for (std::size_t offset = app.find(phrase); offset != std::string::npos;
         offset = app.find(phrase, offset + 1)) {
        const bool starts_word = offset == 0 ||
                                 !is_app_name_word_char(static_cast<unsigned char>(app[offset - 1]));
        const auto end = offset + phrase.size();
        const bool ends_word = end == app.size() ||
                               !is_app_name_word_char(static_cast<unsigned char>(app[end]));
        if (starts_word && ends_word) return true;
    }
    return false;
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

void delete_activity_exports(const std::filesystem::path& app_data_dir) {
    if (app_data_dir.empty()) return;
    for (const auto* directory : {"training", "summaries"}) {
        std::error_code error;
        std::filesystem::remove_all(app_data_dir / "exports" / directory, error);
        if (error) {
            throw std::runtime_error("failed to delete activity exports from " +
                                     std::string(directory) + ": " + error.message());
        }
    }
}

}  // namespace

AppState::AppState(Storage storage, std::filesystem::path app_data_dir, Logger* logger,
                   Clock* clock)
    : storage_(std::move(storage)),
      app_data_dir_(std::move(app_data_dir)),
      logger_(logger),
      clock_(clock) {
    if (!app_data_dir_.empty()) {
        // Pass the logger: 7.19's whole point is that a settings file which failed to parse
        // used to become defaults with nothing written anywhere.
        settings_ = load_app_settings(app_data_dir_, &log());
    }
    // Hydrate persisted live values once at startup so the public live getters can treat
    // the published snapshot as authoritative, including when an optional is empty.
    active_session_ = storage_.active_session();
    latest_prediction_ = storage_.latest_prediction();
    focus_mode_ = settings_.default_focus_mode;
    context_tracker_.set_goal_categories(settings_.goal_categories);
    publish_live_read_unlocked();
}

AppState::~AppState() noexcept {
    set_emit_hook(nullptr);
    stop_engine();
}

std::string AppState::rfc3339_at(std::time_t when) const {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &when);
#else
    gmtime_r(&when, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string AppState::now_rfc3339() const { return rfc3339_at(clock().wall_time()); }

std::string AppState::rfc3339_secs_ago(std::int64_t secs) const {
    // Roadmap 7.23. A pause must be stamped when the user *stopped*, not when we noticed —
    // the idle threshold means we notice five minutes late, and stamping "now" would credit
    // those five minutes as attended on every single pause.
    if (secs <= 0) return now_rfc3339();
    return rfc3339_at(clock().wall_time() - static_cast<std::time_t>(secs));
}

std::int64_t AppState::steady_now_ms() const {
    return clock().steady_ms();
}

std::shared_ptr<const AppState::LiveReadSnapshot> AppState::live_read_snapshot() const noexcept {
    return std::atomic_load_explicit(&live_read_snapshot_, std::memory_order_acquire);
}

void AppState::publish_live_read_unlocked() {
    if (!live_read_dirty_) return;

    auto snapshot = std::make_shared<LiveReadSnapshot>();
    snapshot->active_session = active_session_;
    snapshot->latest_prediction = latest_prediction_;
    snapshot->latest_snapback = latest_snapback_;
    snapshot->last_prediction_at_ms = last_prediction_at_ms_;
    snapshot->private_mode = settings_.private_mode;
    snapshot->idle = idle_;
    snapshot->classifier.backend = classifier_.backend();
    snapshot->classifier.onnx_runtime_enabled = classifier_.backend() == "onnx";
    snapshot->classifier.model_path = OnnxModel::instance().model_path();
    snapshot->classifier.model_id = OnnxModel::instance().model_id();

    std::shared_ptr<const LiveReadSnapshot> published = std::move(snapshot);
    std::atomic_store_explicit(&live_read_snapshot_, std::move(published),
                               std::memory_order_release);
    live_read_dirty_ = false;
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
    // Roadmap 7.23 / ADR-0005. This is the action idle_detector.hpp has always documented
    // ("5 minutes of no input pauses the session", "callers act on the edges, not the level")
    // and never performed — until now the edges only emitted a UI event.
    //
    // Recorded here rather than acted on here: this runs under mutex_, which is deliberately
    // in-memory only, so engine_tick performs the write in its storage phase.
    if (active_session_ && edge != IdleTransition::None) {
        pending_span_session_ = active_session_->session_id;
        pending_span_opens_ = edge == IdleTransition::WokeUp;
        // An offset, not a timestamp: Storage stamps with its own clock, so handing it one of
        // ours would compare two clocks against each other. Back-dating matters because we
        // only notice a whole idle threshold late — stamping the pause at detection time
        // would credit those five minutes as attended on every single pause.
        pending_span_secs_ago_ =
            pending_span_opens_ ? 0 : idle_detector_.idle_for_ms(now_ms) / 1000;
    }

    // Roadmap 2.7 / ADR-0005. Watch for sustained work with no session running.
    const bool now_idle = idle_detector_.state() == IdleState::Idle;
    // Private mode means "do not record". Prompting someone to start recording while they
    // have said that is the wrong direction entirely, so the timer does not advance — and it
    // resets, so turning private mode off does not immediately fire a nudge earned while it
    // was on.
    if (active_session_ || now_idle || settings_.private_mode) {
        // A session is recording, or the stretch ended. Either way there is nothing to nudge
        // about, and the clock restarts from the next burst of activity.
        untracked_since_ms_.reset();
        untracked_latched_ = false;
    } else {
        if (!untracked_since_ms_) untracked_since_ms_ = now_ms;
        const auto minutes = (now_ms - *untracked_since_ms_) / (60 * 1000);
        if (!untracked_latched_ && minutes >= kUntrackedNudgeMinutes) {
            untracked_latched_ = true;
            untracked_minutes_ = static_cast<std::uint64_t>(minutes);
        }
    }

    const bool was_idle = idle_;
    idle_ = now_idle;
    if (idle_ != was_idle) live_read_dirty_ = true;
    return edge;
}

bool AppState::is_idle() const {
    return live_read_snapshot()->idle;
}

IdleTransition AppState::update_idle_for_test(std::int64_t now_ms, bool had_input) {
    std::lock_guard lock(mutex_);
    const auto transition = update_idle_unlocked(now_ms, had_input);
    publish_live_read_unlocked();
    return transition;
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
    start_engine_impl(nullptr);
}

void AppState::start_engine_for_test(InputHook* hook) {
    start_engine_impl(hook);
}

void AppState::start_engine_impl(InputHook* hook) {
    bool expected = false;
    if (!engine_running_.compare_exchange_strong(expected, true)) return;

    {
        std::lock_guard state_lock(mutex_);
        std::lock_guard store_lock(storage_mutex_);
        reload_app_rules_unlocked();  // seed the cache before the tick thread reads it
    }
    try {
        capture_.start(hook);
        engine_thread_ = std::thread([this] {
            while (engine_running_.load(std::memory_order_relaxed)) {
                try {
                    engine_tick();
                } catch (const std::exception& error) {
                    try {
                        std::ostringstream message;
                        message << "engine tick failed: " << error.what();
                        log().error(message.str());
                    } catch (...) {
                        // Logging must not turn a contained engine failure into an
                        // unhandled exception on this thread.
                    }
                } catch (...) {
                    try {
                        log().error("engine tick failed: unknown exception");
                    } catch (...) {
                        // Keep the thread boundary intact even if the logger fails.
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    } catch (...) {
        engine_running_.store(false, std::memory_order_release);
        capture_.stop();
        throw;
    }
}

void AppState::set_emit_hook(EmitHook hook) {
    std::lock_guard lock(mutex_);
    emit_hook_ = std::move(hook);
}

void AppState::stop_engine() noexcept {
    engine_running_.store(false, std::memory_order_relaxed);
    capture_.stop();
    if (engine_thread_.joinable()) engine_thread_.join();
}

SessionRecord AppState::start_session(const std::string& goal, FocusMode mode) {
    // Mixed (in-memory + storage): take both locks in the fixed order mutex_ -> storage_mutex_.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    focus_mode_ = mode;
    features_.begin_session();
    context_tracker_.reset();
    context_tracker_.set_goal_categories(settings_.goal_categories);
    pomodoro_.reset();

    // Roadmap 7.23. Starting a session *replaces* a running one — Storage::create_session
    // completes it (7.20). Its span has to be closed with it: an open span on a completed
    // session keeps counting to "now" forever, so a session replaced weeks ago would report
    // more attended time than it was ever open for.
    const std::optional<std::string> replaced =
        active_session_ ? std::optional<std::string>(active_session_->session_id) : std::nullopt;
    if (replaced) storage_.close_session_span_now(*replaced);

    active_session_ = storage_.create_session(goal, mode);
    // Starting a session is by definition the user attending it, so the first span opens with
    // it — stamped by Storage's clock, the same one that stamped started_at.
    storage_.begin_session_span_now(active_session_->session_id);
    last_prediction_secs_ = -1.0;
    reload_app_rules_unlocked();  // pick up any rules edited while idle
    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return *active_session_;
}

void AppState::stop_session() {
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    pomodoro_.reset();
    if (active_session_) {
        // Close the span before ending the session: once ended_at is set, "the span that is
        // still open" is no longer a meaningful thing to be holding.
        storage_.close_session_span_now(active_session_->session_id);
        storage_.end_session(active_session_->session_id);
        save_auto_session_label_unlocked(active_session_->session_id);
        active_session_.reset();
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
    }
    live_read_dirty_ = true;
    publish_live_read_unlocked();
}

SessionRecord AppState::stop_session(const std::string& session_id) {
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    // Closes nothing when the session was already stopped, or when it predates spans —
    // both ordinary outcomes, so the result is not checked.
    storage_.close_session_span_now(session_id);
    SessionRecord record = storage_.stop_session(session_id);
    save_auto_session_label_unlocked(session_id);
    if (active_session_ && active_session_->session_id == session_id) {
        pomodoro_.reset();
        active_session_.reset();
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
        live_read_dirty_ = true;
    }
    publish_live_read_unlocked();
    return record;
}

std::optional<SessionRecord> AppState::get_session(const std::string& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.get_session(session_id);
}

bool AppState::delete_session(const std::string& session_id) {
    // Same three locks and the same order as delete_all_activity_data, for the same reason:
    // the activity boundary fences off-lock persistence in engine_tick(), so a tick already
    // in flight cannot write rows back into a session between the delete and the state
    // reset. Bumping the epoch also invalidates any event already queued for the UI.
    std::lock_guard state_lock(mutex_);
    std::lock_guard activity_lock(activity_boundary_mutex_);
    std::lock_guard store_lock(storage_mutex_);

    const bool deleting_cached_prediction =
        latest_prediction_ && latest_prediction_->session_id == session_id;
    const bool deleted = storage_.delete_session(session_id);
    if (!deleted) return false;

    activity_epoch_.fetch_add(1, std::memory_order_release);

    // Deleting the session the engine is currently filling would otherwise leave the app
    // pointing at a row that no longer exists — the next tick would try to persist against
    // a missing foreign key, and the UI would keep rendering a session the user just
    // erased. Reset exactly what stop_session() resets, plus the derived prediction state.
    if (active_session_ && active_session_->session_id == session_id) {
        pomodoro_.reset();
        active_session_.reset();
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
        context_tracker_.set_goal_categories(settings_.goal_categories);
        latest_snapback_.reset();
        last_prediction_secs_ = -1.0;
        prediction_dirty_ = false;
        hyperfocus_latched_ = false;
        hyperfocus_minutes_.reset();
        live_read_dirty_ = true;
    }
    if (deleting_cached_prediction) {
        // The snapshot is authoritative, so replace a deleted cached prediction with the
        // newest retained row. Its monotonic-clock age is unknowable after this DB refresh.
        latest_prediction_ = storage_.latest_prediction();
        last_prediction_at_ms_.reset();
        prediction_dirty_ = false;
        live_read_dirty_ = true;
    }
    publish_live_read_unlocked();
    return true;
}

HealthStatus AppState::health() const {
    const auto live = live_read_snapshot();
    HealthStatus h;
    const bool capture_failed = capture_.failed();
    h.status = capture_failed
                   ? "capture_failed"
                   : engine_running_.load(std::memory_order_relaxed) ? "online" : "offline";
    h.capture_running = capture_.running();
    h.capture_failed = capture_failed;
    h.capture_failure_reason = capture_.failure_reason();
    h.capture_events_dropped = capture_.events_dropped();
    const auto event_age_ms = capture_.last_event_age_ms();
    h.capture_stalled = h.capture_running && live->active_session.has_value() && !live->idle &&
                        event_age_ms.has_value() &&
                        *event_age_ms >= kCaptureStallThresholdMs;
    if (live->last_prediction_at_ms) {
        h.last_prediction_age_secs = static_cast<double>(std::max<std::int64_t>(
            0, steady_now_ms() - *live->last_prediction_at_ms)) / 1000.0;
    }
    h.prediction_suppression_reason = live->private_mode
                                          ? "private_mode"
                                          : live->idle ? "idle"
                                          : !live->active_session ? "no_session"
                                                                  : "none";
    h.permissions =
        check_capture_permissions(capture_.running(), capture_.input_observed());
    h.classifier.backend = live->classifier.backend;
    h.classifier.onnx_runtime_enabled = live->classifier.onnx_runtime_enabled;
    h.classifier.model_path = live->classifier.model_path;
    return h;
}

DiagnosticsSnapshot AppState::diagnostics() const {
    return DiagnosticsSnapshot{kSnapbackVersion, health(), log().recent_lines()};
}

std::optional<PredictionRecord> AppState::latest_prediction() const {
    return live_read_snapshot()->latest_prediction;
}

std::optional<SessionRecord> AppState::active_session() const {
    return live_read_snapshot()->active_session;
}

std::optional<SnapbackPayload> AppState::latest_snapback() const {
    return live_read_snapshot()->latest_snapback;
}

std::optional<SnapbackPayload> AppState::take_snapback() {
    std::lock_guard lock(mutex_);
    auto out = std::move(latest_snapback_);
    latest_snapback_.reset();
    if (out) live_read_dirty_ = true;
    publish_live_read_unlocked();
    return out;
}

void AppState::dismiss_snapback() {
    std::lock_guard lock(mutex_);
    // Clear the pending payload and return the tracker from Recovering to Focused so it
    // doesn't keep the recovery state latched.
    latest_snapback_.reset();
    context_tracker_.dismiss_recovery(last_event_secs_);
    live_read_dirty_ = true;
    publish_live_read_unlocked();
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
    // Was 1 + 5N queries (recap() is five statements), all holding storage_mutex_ — which
    // the engine tick also takes to persist, so opening history could stall capture writes.
    return storage_.recent_session_summaries(limit);
}

void AppState::delete_all_activity_data() {
    // The boundary fences off-lock persistence in engine_tick(). Incrementing the epoch
    // also invalidates any event already queued for asynchronous UI dispatch.
    std::lock_guard state_lock(mutex_);
    std::lock_guard activity_lock(activity_boundary_mutex_);
    std::lock_guard store_lock(storage_mutex_);
    activity_epoch_.fetch_add(1, std::memory_order_release);
    // Exported CSVs and summary reports are copies of the rows below. Remove them
    // first so a successful "delete all" cannot leave activity elsewhere under
    // the app's data directory. Support bundles and deployed model/config files
    // are deliberately outside this privacy scope.
    delete_activity_exports(app_data_dir_);
    storage_.delete_all_activity_data();
    active_session_.reset();
    latest_prediction_.reset();
    latest_snapback_.reset();
    last_prediction_at_ms_.reset();
    last_prediction_secs_ = -1.0;
    last_event_secs_ = 0.0;
    prediction_dirty_ = false;
    hyperfocus_latched_ = false;
    hyperfocus_minutes_.reset();
    pomodoro_.reset();
    features_.reset_for_session(std::nullopt);
    context_tracker_.reset();
    context_tracker_.set_goal_categories(settings_.goal_categories);
    live_read_dirty_ = true;
    publish_live_read_unlocked();
}

ExportTrainingResult AppState::export_training_data(
    const std::filesystem::path& out_dir, const std::optional<std::string>& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.export_training_csv(out_dir, session_id);
}

PersonalArchiveExport AppState::export_personal_data(const std::filesystem::path& out_dir,
                                                     std::size_t session_limit,
                                                     std::size_t windows_per_session) {
    PersonalArchive archive;
    archive.generated_at = now_rfc3339();
    archive.app_version = SNAPBACK_VERSION;

    {
        std::lock_guard lock(storage_mutex_);
        // One extra of each so truncation is *observed* rather than assumed. Asking for
        // limit+1 and finding it means "there is more"; asking for limit and finding limit
        // cannot tell a database with exactly `limit` rows from one with a million.
        auto summaries = storage_.recent_session_summaries(session_limit + 1);
        archive.sessions_truncated = summaries.size() > session_limit;
        if (archive.sessions_truncated) summaries.resize(session_limit);

        archive.sessions.reserve(summaries.size());
        for (auto& summary : summaries) {
            PersonalArchiveSession session;
            auto context =
                storage_.list_context_snapshots(summary.record.session_id, windows_per_session + 1);
            session.context_truncated = context.size() > windows_per_session;
            if (session.context_truncated) context.resize(windows_per_session);

            session.record = std::move(summary.record);
            session.recap = std::move(summary.recap);
            session.context = std::move(context);
            archive.sessions.push_back(std::move(session));
        }
    }
    // Rendering and file IO happen outside the storage lock: the engine tick takes the same
    // mutex to persist predictions, and writing a multi-megabyte archive under it would stall
    // capture writes into a bounded ring buffer — the stall-becomes-dropped-events path that
    // recent_session_summaries exists to avoid.

    PersonalArchiveExport result;
    result.session_count = archive.sessions.size();
    for (const auto& session : archive.sessions) result.window_count += session.context.size();
    result.truncated = archive.sessions_truncated;

    std::filesystem::create_directories(out_dir);
    const auto path = out_dir / "snapback_my_data.md";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("failed to write the data export");
    out << render_personal_archive(archive);
    if (!out) throw std::runtime_error("failed to write the data export");

    result.output_path = path.string();
    return result;
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
    live_read_dirty_ = true;
    publish_live_read_unlocked();
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
    const auto predictions = const_cast<Storage&>(storage_).predictions_since();
    for (const auto& prediction : predictions) {
        ++summary.sample_count;
        summary.avg_focus_score += prediction.focus_score;
        const int hour = local_hour_from_rfc3339(prediction.timestamp);
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

    // Was recent_sessions(200) x list_context_snapshots(..., 200) — up to 40,000 fully
    // materialized rows read under the storage lock to compute a group-by. Same caps, same
    // answer, one query.
    const auto app_counts = const_cast<Storage&>(storage_).context_app_counts(200, 200);
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
    for (const auto& prediction : const_cast<Storage&>(storage_).predictions_since(cutoff)) {
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

    // One pass over sessions that already carry their recap, instead of a recap() (five
    // queries) per completed session. The cutoff filter stays here rather than moving into
    // SQL so the 500-session cap keeps applying to the *most recent* sessions first, which
    // is what the original loop did.
    for (const auto& summary : const_cast<Storage&>(storage_).recent_session_summaries(500)) {
        const auto& session = summary.record;
        if (!session.started_at || *session.started_at < cutoff) continue;
        ++report.session_count;
        if (session.status == "COMPLETED") {
            ++report.completed_session_count;
            report.focus_seconds += summary.recap.duration_secs;
        }
    }
    // Same 500/200 caps as the loop above used, aggregated in SQL rather than by reading up
    // to 100,000 snapshot rows into memory.
    const auto context_counts =
        const_cast<Storage&>(storage_).context_app_counts(500, 200, cutoff);
    // Highest count wins, ties broken by the lexicographically smaller app name — same rule
    // as before, but tracking the running best directly instead of re-looking-it-up, since
    // the counts now arrive in a const map.
    std::size_t top_count = 0;
    for (const auto& [app, count] : context_counts) {
        if (report.top_context_app.empty() || count > top_count ||
            (count == top_count && app < report.top_context_app)) {
            report.top_context_app = app;
            top_count = count;
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
    return live_read_snapshot()->classifier;
}

ClassifierStatus AppState::reload_classifier_model() {
    std::lock_guard lock(mutex_);
    training_deploy::recover_model_deployment(app_data_dir_);
    if (const auto model = OnnxModel::resolve_model_path(app_data_dir_)) {
        OnnxModel::instance().init(*model);
    } else {
        OnnxModel::instance().unload();
    }

    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return live_read_snapshot()->classifier;
}

PermissionStatus AppState::refresh_permissions() {
    // CaptureThread owns synchronization for these fields. Joining the engine mutation lock
    // here made a read-only permission poll wait behind feature/classifier work for no gain.
    return check_capture_permissions(capture_.running(), capture_.input_observed());
}

PermissionStatus AppState::request_permissions() {
    std::lock_guard lock(mutex_);
    // Prompt first, then re-probe so the returned status reflects the user's answer in the
    // same round trip (the macOS dialog is modal, so by the time this returns they've
    // decided). Re-probing rather than trusting the prompt's return value keeps one code
    // path — check_capture_permissions — as the single source of truth for the status DTO.
    request_capture_permissions();
    return check_capture_permissions(capture_.running(), capture_.input_observed());
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
                           return contains_whole_app_name_phrase(app, lower_copy(exclusion));
                       });
}

void AppState::submit_label(FocusLabel label, const std::string& source,
                            std::optional<std::string> notes) {
    // Mixed: reads the startup-hydrated active session cache then writes storage.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
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
    std::uint64_t activity_epoch = 0;
    {
        std::lock_guard lock(mutex_);
        activity_epoch = activity_epoch_.load(std::memory_order_acquire);
        job = compute_event(event);
        publish_live_read_unlocked();
    }
    if (job) {
        std::lock_guard activity_lock(activity_boundary_mutex_);
        if (activity_epoch != activity_epoch_.load(std::memory_order_acquire)) return;
        std::lock_guard lock(storage_mutex_);
        Storage::Transaction txn(storage_);
        persist(*job);
        txn.commit();
    }
}

void AppState::engine_tick() {
    // Three phases with different locks so a disk write never blocks an ordinary UI read:
    //   1) drain + classify under mutex_ (in-memory only), collecting persist jobs;
    //   2) flush them under storage_mutex_ in ONE transaction, after releasing mutex_;
    //   3) queue epoch-tagged events holding no lock (the hook hops to the UI thread).
    EmitHook hook;
    std::optional<PredictionRecord> pred_to_emit;
    std::optional<SnapbackPayload> snap_to_emit;
    std::optional<PomodoroStatus> pomodoro_to_emit;
    std::optional<std::uint64_t> hyper_to_emit;
    std::optional<std::uint64_t> untracked_to_emit;
    std::vector<PersistJob> jobs;
    IdleTransition idle_edge = IdleTransition::None;
    // Roadmap 7.23. The span write cannot happen in phase 1: that phase holds mutex_ and is
    // deliberately in-memory only, so a disk write there would block every UI read. Phase 1
    // decides *what* to record; phase 2 records it under storage_mutex_.
    std::optional<std::string> span_session_id;
    std::int64_t span_secs_ago = 0;
    bool span_opens = false;
    std::uint64_t tick_activity_epoch = 0;
    {
        std::lock_guard lock(mutex_);
        tick_activity_epoch = activity_epoch_.load(std::memory_order_acquire);
        bool had_input = false;
        while (auto ev = capture_.next_event()) {
            if (is_input_event(ev->event_type)) had_input = true;
            if (auto job = compute_event(*ev)) jobs.push_back(std::move(*job));
        }
        // Idle timing runs off the tick's monotonic clock, not event timestamps: true AFK
        // means no events arrive at all, so we must measure wall time, not the last event.
        const auto now_ms = steady_now_ms();
        idle_edge = update_idle_unlocked(now_ms, had_input);
        // The span decision was made inside update_idle_unlocked, beside the idle logic that
        // knows why. Take it here and write it in phase 2 — phase 1 holds mutex_ and must not
        // touch the disk.
        span_session_id = std::exchange(pending_span_session_, std::nullopt);
        span_secs_ago = pending_span_secs_ago_;
        span_opens = pending_span_opens_;
        if (pomodoro_.poll(now_ms)) pomodoro_to_emit = pomodoro_.status(now_ms);
        hook = emit_hook_;
        if (prediction_dirty_) {
            pred_to_emit = latest_prediction_;
            prediction_dirty_ = false;
        }
        if (latest_snapback_) {
            snap_to_emit = std::move(latest_snapback_);
            latest_snapback_.reset();
            live_read_dirty_ = true;
        }
        if (hyperfocus_minutes_) {
            hyper_to_emit = hyperfocus_minutes_;
            hyperfocus_minutes_.reset();
        }
        if (untracked_minutes_) {
            untracked_to_emit = untracked_minutes_;
            untracked_minutes_.reset();
        }
        publish_live_read_unlocked();
    }

    {
        // If deletion won the boundary after phase 1, discard every buffered row and
        // event. If this tick won, deletion waits until persistence has completed.
        std::lock_guard activity_lock(activity_boundary_mutex_);
        if (tick_activity_epoch != activity_epoch_.load(std::memory_order_acquire)) return;
        if (!jobs.empty() || span_session_id) {
            std::lock_guard lock(storage_mutex_);
            Storage::Transaction txn(storage_);  // one commit for the whole drain
            for (const auto& job : jobs) persist(job);
            if (span_session_id) {
                if (span_opens) {
                    storage_.begin_session_span_now(*span_session_id);
                } else {
                    storage_.close_session_span_now(*span_session_id, span_secs_ago);
                }
            }
            txn.commit();
        }
    }

    if (!hook) return;
    if (idle_edge == IdleTransition::WentIdle) {
        hook("idle", "{\"idle\":true}", tick_activity_epoch);
    }
    if (idle_edge == IdleTransition::WokeUp) {
        hook("idle", "{\"idle\":false}", tick_activity_epoch);
    }
    if (pred_to_emit) {
        hook("prediction", nlohmann::json(*pred_to_emit).dump(), tick_activity_epoch);
    }
    if (snap_to_emit) {
        hook("snapback", nlohmann::json(*snap_to_emit).dump(), tick_activity_epoch);
    }
    if (pomodoro_to_emit) {
        hook("pomodoro", nlohmann::json(*pomodoro_to_emit).dump(), tick_activity_epoch);
    }
    if (hyper_to_emit) {
        const auto note = build_hyperfocus_notification(*hyper_to_emit);
        hook("hyperfocus", nlohmann::json{{"message", note.body},
                                          {"minutes", *hyper_to_emit}}
                               .dump(),
             tick_activity_epoch);
    }
    if (untracked_to_emit) {
        const auto note = build_untracked_work_notification(*untracked_to_emit);
        hook("untracked_work", nlohmann::json{{"message", note.body},
                                              {"minutes", *untracked_to_emit}}
                                   .dump(),
             tick_activity_epoch);
    }
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
                app_rules_, event.timestamp_secs, [this] { return now_rfc3339(); });
        }
        // The tracker latches a snapback payload on the return-from-distraction edge;
        // drain it into the field the tick loop emits.
        if (auto snapback = context_tracker_.take_pending_snapback()) {
            latest_snapback_ = *snapback;
            live_read_dirty_ = true;
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

    // Hyperfocus guardrail: nudge the user to break after the mode's continuous-work
    // window. Latched, so one stretch produces one nudge rather than one per tick; the
    // latch clears when a break resets minutes_since_last_break below the threshold.
    if (have_session) {
        const auto minutes = static_cast<std::uint64_t>(features.minutes_since_last_break());
        if (evaluate_hyperfocus(focus_mode_, minutes)) {
            if (!hyperfocus_latched_) {
                hyperfocus_latched_ = true;
                hyperfocus_minutes_ = minutes;
            }
        } else {
            hyperfocus_latched_ = false;
        }
    }

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
    record.model_id = classifier_.model_id();
    record.state_source = scores.state_source;
    latest_prediction_ = record;
    last_prediction_at_ms_ = steady_now_ms();
    prediction_dirty_ = true;  // engine_tick emits this after unlocking
    live_read_dirty_ = true;

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

void AppState::save_auto_session_label_unlocked(const std::string& session_id) {
    try {
        storage_.save_auto_session_label(session_id);
    } catch (const std::exception& err) {
        std::ostringstream msg;
        msg << "failed to save automatic session label: " << err.what();
        log().warn(msg.str());
    }
}

}  // namespace snapback
