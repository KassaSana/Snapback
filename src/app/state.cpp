#include "app/state.hpp"

#include "app/events.hpp"

#include <chrono>
#include <sqlite3.h>
#include <exception>
#include <algorithm>
#include <limits>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "capture/permissions.hpp"
#include "app/alert_routing.hpp"
#include "app/frontend_assets.hpp"
#include "app/version.hpp"
#include "app/notification.hpp"
#include "app/training_deploy.hpp"
#include "engine/app_context.hpp"
#include "engine/focus_modes.hpp"
#include "engine/onnx_model.hpp"
#include "snapback/focus_window.hpp"
#include "util/process_cpu.hpp"
#include "util/text.hpp"
#include "util/time.hpp"

namespace snapback {
namespace {

// How many of the newest sessions the Summary report's session aggregates read. Reported on
// the wire as SummaryReport::session_limit so the UI can say when it was binding.
constexpr std::size_t kSummarySessionLimit = 500;

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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

std::int64_t cutoff_unix_ms(int days_ago) {
    using namespace std::chrono;
    const auto cutoff = system_clock::now() - hours(24 * days_ago);
    return duration_cast<milliseconds>(cutoff.time_since_epoch()).count();
}

// What "delete all activity" removes, and what it deliberately keeps.
//
// Activity-bearing (deleted): exports/training, exports/summaries, exports/personal (contains
// window titles verbatim), and focoflow.db.pre-v<N>.bak (full database copies from migrations).
//
// Configuration and diagnostics (kept, and reported as kept): settings.json (+ .bak/.tmp),
// model files, training_repo.txt, snapback.log and rotations, exports/support, snapback.lock.
// The log holds operational events, not captured content, and a support bundle is a copy of it.

// One activity-bearing target and the human name the result reports it under.
struct ActivityArtifact {
    std::filesystem::path path;
    const char* label;
    bool is_directory;
};

std::vector<ActivityArtifact> activity_artifacts(const std::filesystem::path& app_data_dir) {
    std::vector<ActivityArtifact> targets{
        {app_data_dir / "exports" / "training", "training exports", true},
        {app_data_dir / "exports" / "summaries", "summary reports", true},
        {app_data_dir / "exports" / "personal", "personal data exports", true},
    };

    // Matched by shape rather than derived from kSchemaVersion: several upgrades leave several
    // backups.
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(app_data_dir, error)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("focoflow.db.pre-v", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".bak") == 0) {
            targets.push_back({entry.path(), "pre-migration database backup", false});
        }
    }
    // A directory that cannot be listed is reported through the caller's result rather than
    // thrown: a backup we could not find must not stop the database being cleared.
    return targets;
}

const char* const kRetainedArtifacts[] = {
    "your settings",
    "the classifier model and its metadata",
    "the app log and any support bundles",
};

}  // namespace

// The default lives in two headers for layering reasons; keep them in sync.
static_assert(kDefaultIdleThresholdSecs * 1000 == kDefaultIdleThresholdMs,
              "AppSettings' default idle threshold must match the detector's");

AppState::AppState(Storage storage, std::filesystem::path app_data_dir, Logger* logger,
                   Clock* clock, ModelDeploymentHealth model_deployment)
    : storage_(std::move(storage)),
      app_data_dir_(std::move(app_data_dir)),
      logger_(logger),
      clock_(clock),
      model_deployment_health_(std::move(model_deployment)) {
    if (!app_data_dir_.empty()) {
        // Pass the logger so a settings file that fails to parse is reported, not silently
        // reset.
        settings_ = load_app_settings(app_data_dir_, &log());
    }
    // Hydrate persisted live values once at startup so the public live getters can treat
    // the published snapshot as authoritative, including when an optional is empty.
    active_session_ = storage_.active_session();
    latest_prediction_ = storage_.latest_prediction();
    focus_mode_ = settings_.default_focus_mode;
    idle_detector_.set_threshold_ms(settings_.idle_threshold_secs * 1000);
    context_tracker_.set_goal_categories(settings_.goal_categories);
    // Restore rhythm and position together. A deadline that passed while closed waits for the
    // user rather than chaining through phases; see PomodoroTimer::restore.
    pomodoro_ = PomodoroTimer(settings_.pomodoro);
    pomodoro_.restore(settings_.pomodoro_state, now_unix_ms(), steady_now_ms());
    hydrate_active_session_unlocked();
    hydrate_session_attendance_unlocked();
    last_prune_steady_ms_.store(steady_now_ms());  // Readiness queues the initial pass.
    maintenance_paused_.store(active_session_.has_value(), std::memory_order_release);
    publish_live_read_unlocked();
    capture_.set_wake_signal(&engine_wake_);
    maintenance_thread_ = std::thread([this] { run_retention_maintenance(); });
}

void AppState::hydrate_active_session_unlocked() {
    // Restore the live state that belongs to the hydrated session: its focus mode (which sets
    // the risk threshold and hyperfocus window) and the extractor's session origin.
    if (!active_session_) return;
    focus_mode_ = focus_mode_from_string(active_session_->focus_mode);
    const auto elapsed = storage_.session_elapsed_secs(active_session_->session_id);
    features_.resume_session(static_cast<double>(elapsed.value_or(0)));
}

void AppState::hydrate_session_attendance_unlocked() {
    // A span still open at startup was left by a crash; close it at the session's last activity
    // so the downtime is not counted as attended. The idle detector decides attendance anew.
    session_attended_ = false;
    committed_session_attended_ = false;
    if (!active_session_) return;
    const auto closed_at = storage_.close_dangling_session_span(active_session_->session_id);
    if (!closed_at) return;
    std::ostringstream message;
    message << "session " << active_session_->session_id
            << ": closed a span left open by a previous run at " << *closed_at
            << " (its last recorded activity)";
    log().warn(message.str());
}

AppState::~AppState() noexcept {
    set_emit_hook(nullptr);
    stop_engine();
}

std::int64_t AppState::now_unix_ms() const { return clock().wall_ms(); }

AlertRoute AppState::alert_route_unlocked(AlertEvent event) const {
    const auto now = now_unix_ms();
    return route_alert(event, settings_.alerts, now, local_minute_of_day_from_unix_ms(now));
}

std::int64_t AppState::issue_alert_id_unlocked(AlertEvent event, const AlertRoute& route) {
    // Requires mutex_. Ids are issued at emit time, not route time, so an unemitted alert does
    // not retire a click. A route with no destination clears the slot, which is what stops a
    // pre-quiet-hours toast from acting later.
    if (!route.visible() || route.action == AlertAction::None) {
        actionable_alert_ids_[alert_event_index(event)] = 0;
        return 0;
    }
    const auto id = ++next_alert_id_;
    // Overwrites rather than accumulates: only the newest alert of a kind is worth acting on,
    // and a queue of claimable ids would mean an old toast in notification history still works.
    actionable_alert_ids_[alert_event_index(event)] = id;
    return id;
}

std::int64_t AppState::outstanding_alert_id(AlertEvent event) const {
    std::lock_guard lock(mutex_);
    return actionable_alert_ids_[alert_event_index(event)];
}

bool AppState::claim_alert_action(AlertEvent event, std::int64_t alert_id) {
    std::lock_guard lock(mutex_);
    // 0 never matches: ids start at 1 and a cleared slot is 0, so neither a payload that lost
    // its alertId nor a kind with nothing outstanding can claim the first alert ever issued.
    if (alert_id <= 0) return false;
    auto& outstanding = actionable_alert_ids_[alert_event_index(event)];
    if (outstanding != alert_id) return false;
    outstanding = 0;
    return true;
}

std::int64_t AppState::unix_ms_secs_ago(std::int64_t secs) const {
    // Stamp a pause when the user stopped, not when the idle threshold noticed.
    if (secs <= 0) return now_unix_ms();
    return now_unix_ms() - secs * 1000;
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
    snapshot->persistence_failure_reason = persistence_failure_reason_;
    snapshot->private_mode = settings_.private_mode;
    snapshot->idle = idle_;
    snapshot->classifier.backend = classifier_.backend();
    snapshot->classifier.onnx_runtime_enabled = classifier_.backend() == "onnx";
    snapshot->classifier.inference_degraded = classifier_.inference_degraded();
    snapshot->classifier.inference_failures = classifier_.inference_failures();
    snapshot->classifier.model_path = OnnxModel::instance().model_path();
    snapshot->classifier.model_id = classifier_.model_id();
    snapshot->model_deployment = model_deployment_health_;

    std::shared_ptr<const LiveReadSnapshot> published = std::move(snapshot);
    std::atomic_store_explicit(&live_read_snapshot_, std::move(published),
                               std::memory_order_release);
    live_read_dirty_ = false;
}

bool AppState::is_input_event(EventType type) {
    return type == EventType::KeyPress || type == EventType::KeyRelease ||
           type == EventType::MouseMove || type == EventType::MouseClick || type == EventType::MouseScroll;
}

IdleTransition AppState::update_idle_unlocked(std::int64_t now_ms, bool had_input,
                                              const CaptureEvent* waking_event) {
    // on_activity resets the clock (and wakes us); poll then checks the threshold. A tick
    // with input can only ever wake us, never sleep us; a tick without input can only sleep.
    IdleTransition edge = IdleTransition::None;
    if (had_input) edge = idle_detector_.on_activity(now_ms);
    if (const auto poll_edge = idle_detector_.poll(now_ms); poll_edge != IdleTransition::None) {
        edge = poll_edge;
    }
    const bool now_idle = idle_detector_.state() == IdleState::Idle;
    if (edge == IdleTransition::WentIdle) {
        foreground_context_needs_resync_ = true;
        idle_foreground_context_.reset();
    }

    // Attendance follows the idle *level*, not the edge: a crash reopens with the span already
    // closed and no edge on the way. Recorded here (under mutex_, in-memory only); engine_tick
    // writes it in its storage phase.
    const bool should_attend = active_session_.has_value() && !now_idle;
    if (should_attend != session_attended_) {
        if (active_session_) {
            // Resolved against Storage's clock in phase 2, then kept across retries so the
            // boundary does
            // not slide forward.
            pending_span_transitions_.push_back(PendingSpanTransition{
                ++next_span_transition_id_, active_session_->session_id, should_attend,
                should_attend ? 0 : idle_detector_.idle_for_ms(now_ms), std::nullopt, now_ms});
        }
        session_attended_ = should_attend;
    }

    // Untracked-work nudge (ADR-0005). Private mode and excluded apps reset the stretch rather
    // than pause it, so leaving them starts fresh.
    const bool in_excluded_app =
        !last_capture_app_.empty() && app_matches_exclusion_unlocked(last_capture_app_);
    if (active_session_ || now_idle || settings_.private_mode || in_excluded_app) {
        // A session is recording, or the stretch ended. Either way there is nothing to nudge
        // about, and the clock restarts from the next burst of activity.
        untracked_since_ms_.reset();
        untracked_latched_ = false;
    } else {
        if (!untracked_since_ms_) untracked_since_ms_ = now_ms;
        const auto minutes = (now_ms - *untracked_since_ms_) / (60 * 1000);
        const bool dismissal_active = now_unix_ms() < settings_.untracked_nudge_until_wall_ms;
        if (!dismissal_active && !untracked_latched_ && minutes >= kUntrackedNudgeMinutes) {
            // Latched even when suppressed: one stretch earns one prompt, and quiet hours must
            // not queue
            // one to fire when they end.
            untracked_latched_ = true;
            if (alert_route_unlocked(AlertEvent::UntrackedWork).visible()) {
                untracked_minutes_ = static_cast<std::uint64_t>(minutes);
            }
        }
    }

    const bool was_idle = idle_;
    idle_ = now_idle;
    if (idle_ != was_idle) live_read_dirty_ = true;

    // Hand the idle stretch to the feature extractor as one IdleEnd carrying the whole duration
    // (the shape fixtures/feature_parity describes), so minutes_since_last_break resets on a
    // real
    // break. Stamped with the event clock, which the extractor's windows use. Only while a
    // session
    // is running.
    const double wake_event_secs = waking_event ? waking_event->timestamp_secs : last_event_secs_;
    if (edge == IdleTransition::WokeUp && active_session_ && wake_event_secs > 0.0) {
        CaptureEvent idle_end;
        idle_end.event_type = EventType::IdleEnd;
        idle_end.timestamp_secs = wake_event_secs;
        idle_end.idle_duration_ms = static_cast<std::uint32_t>(std::min<std::int64_t>(
            idle_detector_.last_idle_duration_ms(), std::numeric_limits<std::uint32_t>::max()));
        // A private/excluded waking event may end AFK, but its app/title must not enter the
        // feature windows. The next permitted event will reconcile foreground context.
        if (waking_event && !is_private_event_unlocked(*waking_event)) {
            idle_end.app_name = waking_event->app_name;
            idle_end.window_title = waking_event->window_title;
            idle_end.wall_clock_secs = waking_event->wall_clock_secs;
        } else if (!waking_event) {
            idle_end.app_name = last_capture_app_;
        }
        features_.ingest(idle_end);
    }
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

FeatureVector AppState::extract_features_for_test(double now_secs) {
    std::lock_guard lock(mutex_);
    return features_.extract(now_secs, app_rules_);
}

PomodoroStatus AppState::start_pomodoro_unlocked(std::int64_t now_ms) {
    if (!active_session_) throw std::runtime_error("no active session");
    pomodoro_.start(now_ms);
    return pomodoro_.status(now_ms);
}

PomodoroStatus AppState::start_pomodoro() {
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    return start_pomodoro_unlocked(steady_now_ms());
}

// Write the timer position after every change. Best-effort: a failed save costs resume-on-
// relaunch, never the running phase.
void AppState::persist_pomodoro_unlocked() {
    if (app_data_dir_.empty()) return;
    AppSettings candidate = settings_;
    candidate.pomodoro_state = pomodoro_.snapshot(now_unix_ms(), steady_now_ms());
    try {
        save_app_settings(app_data_dir_, candidate);
        settings_ = std::move(candidate);
    } catch (const std::exception& error) {
        log().warn(std::string("pomodoro: could not persist timer state: ") + error.what());
    }
}

// Move the timer, persist it, report it. `apply` runs under mutex_ so status and snapshot
// describe one instant.
PomodoroStatus AppState::mutate_pomodoro(const std::function<void(std::int64_t)>& apply) {
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    const auto now = steady_now_ms();
    apply(now);
    persist_pomodoro_unlocked();
    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return pomodoro_.status(now);
}

PomodoroStatus AppState::pause_pomodoro() {
    return mutate_pomodoro([this](std::int64_t now) { pomodoro_.pause(now); });
}

PomodoroStatus AppState::resume_pomodoro() {
    return mutate_pomodoro([this](std::int64_t now) { pomodoro_.resume(now); });
}

PomodoroStatus AppState::skip_pomodoro_phase() {
    return mutate_pomodoro([this](std::int64_t now) { pomodoro_.skip(now); });
}

PomodoroStatus AppState::restart_pomodoro_phase() {
    return mutate_pomodoro([this](std::int64_t now) { pomodoro_.restart_phase(now); });
}

PomodoroStatus AppState::acknowledge_pomodoro_phase() {
    return mutate_pomodoro([this](std::int64_t now) { pomodoro_.acknowledge(now); });
}

PomodoroStatus AppState::set_pomodoro_config(const PomodoroConfig& config) {
    WakeOnExit wake{engine_wake_};
    // Validated before anything moves, so a rejected rhythm leaves both the live timer and
    // settings.json exactly as they were.
    if (config.work_ms <= 0 || config.short_break_ms <= 0 || config.long_break_ms <= 0) {
        throw std::runtime_error("pomodoro phase lengths must be greater than zero");
    }
    if (config.intervals_before_long_break < 0) {
        throw std::runtime_error("intervals before a long break cannot be negative");
    }
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.pomodoro = config;
    // Takes effect from the next phase; rebuilding the timer would restart the current one.
    commit_settings_unlocked(std::move(candidate), [this, config] {
        pomodoro_.set_config(config);
    });
    return pomodoro_.status(steady_now_ms());
}

PomodoroStatus AppState::stop_pomodoro() {
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    pomodoro_.stop();
    return pomodoro_.status(steady_now_ms());
}

// Plan from settings, actuals from durable spans -- never from session duration, predictions,
// or scores.
AttendedProgress AppState::attended_progress() {
    // Settings under mutex_, spans under storage_mutex_. Copy settings and release before the
    // storage read so a slow query does not hold the state lock.
    AppSettings snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
    }
    const auto now = now_unix_ms();
    std::lock_guard store_lock(storage_mutex_);
    AttendedProgress out;
    out.daily_target_mins = snapshot.attended_target_daily_mins;
    out.weekly_target_mins = snapshot.attended_target_weekly_mins;
    // Whole minutes, rounded down: a plan is not improved by claiming a minute that has not
    // finished, and rounding up would let 59 seconds read as a minute of attendance.
    out.daily_actual_mins = storage_.attended_secs_in_local_day(now) / 60;
    out.weekly_actual_mins = storage_.attended_secs_in_local_week(now) / 60;
    return out;
}

AttendedProgress AppState::set_attended_targets(std::uint32_t daily_mins,
                                                std::uint32_t weekly_mins) {
    // A week cannot hold more than seven days of minutes, and a day cannot hold more than
    // itself. Rejected before anything is written, so a bad plan leaves the old one intact.
    constexpr std::uint32_t kMinsPerDay = 24 * 60;
    if (daily_mins > kMinsPerDay) {
        throw std::runtime_error("a daily target cannot exceed 24 hours");
    }
    if (weekly_mins > 7 * kMinsPerDay) {
        throw std::runtime_error("a weekly target cannot exceed 7 days");
    }
    {
        std::lock_guard lock(mutex_);
        AppSettings candidate = settings_;
        candidate.attended_target_daily_mins = daily_mins;
        candidate.attended_target_weekly_mins = weekly_mins;
        commit_settings_unlocked(std::move(candidate), [] {});
    }
    return attended_progress();
}

PomodoroConfig AppState::pomodoro_config() const {
    std::lock_guard lock(mutex_);
    return settings_.pomodoro;
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
            // True when the last tick stopped on a budget; the backlog is then worked off
            // across short
            // ticks. On shutdown the loop keeps going while the ring still has events (the
            // producer is
            // already stopped, so it terminates). do-while so one tick always runs even if
            // stop_engine()
            // flipped the flag first. The exit check asks the ring, not `backlog`, which can be
            // stale.
            bool backlog = false;
            // Bound consecutive failed ticks only, so a healthy drain of a full ring always
            // finishes.
            int shutdown_failures = 0;
            constexpr int kMaxShutdownFailures = 64;
            do {
                engine_wakeups_.fetch_add(1, std::memory_order_relaxed);
                bool tick_failed = false;
                try {
                    backlog = engine_tick();
                } catch (const std::exception& error) {
                    // A tick that threw may have left events queued; ask the ring.
                    tick_failed = true;
                    backlog = capture_.has_pending_events();
                    try {
                        std::ostringstream message;
                        message << "engine tick failed: " << error.what();
                        log().error(message.str());
                    } catch (...) {
                        // Logging must not turn a contained engine failure into an
                        // unhandled exception on this thread.
                    }
                } catch (...) {
                    tick_failed = true;
                    backlog = capture_.has_pending_events();
                    try {
                        log().error("engine tick failed: unknown exception");
                    } catch (...) {
                        // Keep the thread boundary intact even if the logger fails.
                    }
                }
                // Checked before sleeping so a stop with an empty queue exits now rather than
                // waiting out a tick interval nobody is waiting for.
                const bool stopping = !engine_running_.load(std::memory_order_relaxed);
                if (stopping && tick_failed) {
                    if (++shutdown_failures >= kMaxShutdownFailures) {
                        try {
                            log().error("engine shutdown stopped after repeated tick failures");
                        } catch (...) {
                            // Keep the thread boundary intact even if the logger fails.
                        }
                        break;
                    }
                } else {
                    shutdown_failures = 0;
                }
                // Only shutdown consults the ring. Pacing uses `backlog`, or a few leftover
                // keystrokes would
                // spin the loop at 1 ms.
                if (stopping && !backlog && !capture_.has_pending_events()) break;
                if (backlog || stopping) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kEngineBacklogTickIntervalMs));
                } else {
                    engine_wake_.wait(next_engine_delay_ms());
                }
            } while (engine_running_.load(std::memory_order_relaxed) ||
                     backlog || capture_.has_pending_events());
        });
    } catch (...) {
        engine_running_.store(false, std::memory_order_release);
        capture_.stop();
        throw;
    }
}

std::optional<std::int64_t> AppState::next_engine_delay_ms() const {
    std::lock_guard lock(mutex_);
    const auto now = steady_now_ms();
    const auto wall_now = now_unix_ms();
    EngineDeadlines deadlines(now, wall_now);
    if (auto deadline = idle_detector_.next_deadline_ms()) deadlines.monotonic(*deadline);
    if (auto deadline = pomodoro_.next_deadline_ms()) deadlines.monotonic(*deadline);
    if (!active_session_ && !idle_ && untracked_since_ms_ && !untracked_latched_) {
        const auto dismissal = settings_.untracked_nudge_until_wall_ms;
        if (dismissal > wall_now) deadlines.wall(dismissal);
        else deadlines.monotonic(*untracked_since_ms_ + kUntrackedNudgeMinutes * 60000);
    }
    if (settings_.private_mode && settings_.private_until_wall_ms > 0) {
        if (settings_.private_until_wall_ms > wall_now) deadlines.wall(settings_.private_until_wall_ms);
        else deadlines.monotonic(std::max(now, privacy_lapse_retry_at_ms_));
    }
    if (settings_.alerts.snoozed_until_wall_ms > wall_now)
        deadlines.wall(settings_.alerts.snoozed_until_wall_ms);
    if (persistence_failure_reason_ &&
        (!pending_span_transitions_.empty() || pending_snapback_episode_))
        deadlines.monotonic(persistence_retry_at_ms_);
    if (frontend_ready_.load(std::memory_order_acquire) &&
        !maintenance_pending_.load(std::memory_order_acquire))
        deadlines.monotonic(last_prune_steady_ms_.load(std::memory_order_acquire) + kRetentionPruneIntervalMs);
    return deadlines.delay_ms();
}

void AppState::set_emit_hook(EmitHook hook) {
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    emit_hook_ = std::move(hook);
}

void AppState::emit_event(const char* event, const std::string& json_payload) {
    EmitHook emit_to_frontend;
    ActivityEpoch epoch = 0;
    {
        // Copy out and call unlocked, as the tick does: the hook only queues a UI closure,
        // but nothing here should depend on that staying true.
        std::lock_guard lock(mutex_);
        emit_to_frontend = emit_hook_;
        epoch = activity_epoch_.load(std::memory_order_acquire);
    }
    if (emit_to_frontend) emit_to_frontend(event, json_payload, epoch);
}

void AppState::stop_engine() noexcept {
    signal_maintenance([this] { maintenance_stopping_.store(true, std::memory_order_release); });
    // Stop the producer first so the final empty-ring check is a real quiescence barrier.
    capture_.stop();
    engine_running_.store(false, std::memory_order_relaxed);
    engine_wake_.notify();
    if (engine_thread_.joinable()) engine_thread_.join();
    if (maintenance_thread_.joinable()) maintenance_thread_.join();
    maintenance_running_.store(false, std::memory_order_release);
    if (maintenance_pending_.exchange(false, std::memory_order_acq_rel))
        maintenance_result_.store(5, std::memory_order_release);
    close_open_span_on_shutdown();
}

void AppState::close_open_span_on_shutdown() noexcept {
    // Close the open span on clean exit, after the engine has joined. noexcept and best-effort:
    // a span left open here is what startup hydration already handles.
    try {
        std::lock_guard state_lock(mutex_);
        std::lock_guard store_lock(storage_mutex_);
        if (!active_session_ || !committed_session_attended_) return;
        storage_.close_session_span_now(active_session_->session_id);
        session_attended_ = false;
        committed_session_attended_ = false;
    } catch (...) {
        // Deliberately swallowed; see above.
    }
}

void AppState::discard_pending_span_unlocked(const std::optional<std::string>& session_id) {
    std::erase_if(pending_span_transitions_, [&](const PendingSpanTransition& transition) {
        return !session_id || transition.session_id == *session_id;
    });
}

void AppState::clear_snapback_unlocked(bool discard_pending_recording) {
    latest_snapback_.reset();
    if (discard_pending_recording) pending_snapback_episode_.reset();
    snapback_emitted_ = false;
    ++snapback_generation_;
}

SessionRecord AppState::start_session(const std::string& goal, FocusMode mode) {
    WakeOnExit wake{engine_wake_};
    // Mixed (in-memory + storage): take both locks in the fixed order mutex_ -> storage_mutex_.
    std::lock_guard state_lock(mutex_);

    // Persist first, mutate second: everything before release() below is undone as a unit on
    // failure; everything after is in-memory and cannot fail. Replacing a running session
    // closes
    // its span inside the same savepoint, so a rolled-back replacement leaves the old session
    // running and attended.
    const std::optional<std::string> replaced =
        active_session_ ? std::optional<std::string>(active_session_->session_id) : std::nullopt;
    maintenance_paused_.store(true, std::memory_order_release);

    std::lock_guard store_lock(storage_mutex_);
    SessionRecord created;
    try {
        Storage::Savepoint savepoint(storage_, "app_start_session");
        if (replaced) storage_.close_session_span_now(*replaced);
        created = storage_.create_session(goal, mode);
        // Starting a session is by definition the user attending it, so the first span opens
        // with it — stamped by Storage's clock, the same one that stamped started_at.
        storage_.begin_session_span_now(created.session_id);
        savepoint.release();
    } catch (...) {
        signal_maintenance([this, &replaced] {
            maintenance_paused_.store(replaced.has_value(), std::memory_order_release);
        });
        throw;
    }

    // A replaced session gets the same auto-label as a stopped one. Best-effort, outside the
    // savepoint.
    if (replaced) save_auto_session_label_unlocked(*replaced);

    active_session_ = created;
    focus_mode_ = mode;
    features_.begin_session();
    context_tracker_.reset();
    context_tracker_.set_goal_categories(settings_.goal_categories);
    pomodoro_.reset();
    // A snapback names a window from the previous session, and the tracker was just reset;
    // "Take me back" must not act on it.
    clear_snapback_unlocked();
    // Pending span decisions belong to the session just completed.
    discard_pending_span_unlocked();
    // Fence queued UI emissions from the previous session. Persistence is not fenced: drained
    // jobs still belong to the session they name.
    activity_epoch_.fetch_add(1, std::memory_order_release);
    // Drop a pending emit only; the cached prediction may still name the previous session.
    prediction_dirty_ = false;
    session_attended_ = true;
    committed_session_attended_ = true;
    // Pressing Start is input; otherwise a stale Idle state would close the new span at once.
    idle_detector_.on_activity(steady_now_ms());
    last_prediction_secs_ = -1.0;
    reload_app_rules_unlocked();  // pick up any rules edited while idle
    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return *active_session_;
}

void AppState::stop_session() {
    WakeOnExit wake{engine_wake_};
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    if (active_session_) {
        const std::string session_id = active_session_->session_id;
        {
            // Close the span and end the session together, so a failure cannot leave a
            // completed session
            // accruing attended time.
            Storage::Savepoint savepoint(storage_, "app_stop_session");
            storage_.close_session_span_now(session_id);
            storage_.end_session(session_id);
            savepoint.release();
        }
        // Same rule as the by-id overload: drop span decisions for the ended session.
        discard_pending_span_unlocked(session_id);
        save_auto_session_label_unlocked(session_id);
        session_attended_ = false;
        committed_session_attended_ = false;
        pomodoro_.reset();
        active_session_.reset();
        signal_maintenance(
            [this] { maintenance_paused_.store(false, std::memory_order_release); });
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
        // Same reason as start_session: the payload names a window from the session being
        // closed, and there is no session left for "Take me back" to be about.
        clear_snapback_unlocked();
        // Same generation fence as start_session: a prediction or snapback already queued for
        // the UI must not land after Stop has cleared the native payload.
        activity_epoch_.fetch_add(1, std::memory_order_release);
        prediction_dirty_ = false;
    }
    live_read_dirty_ = true;
    publish_live_read_unlocked();
}

SessionRecord AppState::stop_session(const std::string& session_id) {
    WakeOnExit wake{engine_wake_};
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    SessionRecord record;
    {
        Storage::Savepoint savepoint(storage_, "app_stop_session_by_id");
        // Closes nothing when the session was already stopped, or when it predates spans —
        // both ordinary outcomes, so the result is not checked.
        storage_.close_session_span_now(session_id);
        record = storage_.stop_session(session_id);
        savepoint.release();
    }
    // Drop span decisions for this session so an in-flight tick cannot re-attend it. Keyed on
    // the id because the stopped session need not be the active one.
    discard_pending_span_unlocked(session_id);

    // Idempotent at the storage layer: a second Stop returns the existing label.
    save_auto_session_label_unlocked(session_id);
    if (active_session_ && active_session_->session_id == session_id) {
        pomodoro_.reset();
        session_attended_ = false;
        committed_session_attended_ = false;
        active_session_.reset();
        signal_maintenance(
            [this] { maintenance_paused_.store(false, std::memory_order_release); });
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
        // Only in the active-session branch: stopping another session must not cancel a live
        // card.
        clear_snapback_unlocked();
        activity_epoch_.fetch_add(1, std::memory_order_release);
        prediction_dirty_ = false;
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
    WakeOnExit wake{engine_wake_};
    // Same locks and order as delete_all_activity_data: the activity boundary fences in-flight
    // persistence, and the epoch bump invalidates queued UI events.
    std::lock_guard state_lock(mutex_);
    std::lock_guard activity_lock(activity_boundary_mutex_);
    std::lock_guard store_lock(storage_mutex_);

    const bool deleting_cached_prediction =
        latest_prediction_ && latest_prediction_->session_id == session_id;
    const bool deleted = storage_.delete_session(session_id);
    if (!deleted) return false;

    activity_epoch_.fetch_add(1, std::memory_order_release);
    // The epoch bump already fences a tick's off-lock persistence, but a decision recorded
    // before this delete and drained after it names a row that is gone.
    discard_pending_span_unlocked(session_id);

    // Deleting the session being filled: reset what stop_session() resets, plus prediction
    // state, so nothing persists against the missing row.
    if (active_session_ && active_session_->session_id == session_id) {
        // Queued events belong to the erased session. Only here: another session's deletion
        // must
        // leave the live queue alone.
        capture_.discard_pending_events();
        pomodoro_.reset();
        // Its spans went with the row; nothing left to close.
        session_attended_ = false;
        committed_session_attended_ = false;
        active_session_.reset();
        signal_maintenance(
            [this] { maintenance_paused_.store(false, std::memory_order_release); });
        features_.reset_for_session(std::nullopt);
        context_tracker_.reset();
        context_tracker_.set_goal_categories(settings_.goal_categories);
        clear_snapback_unlocked();
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
    const bool engine_running = engine_running_.load(std::memory_order_relaxed);
    if (capture_failed) {
        h.status = "capture_failed";
    } else if (!engine_running) {
        h.status = "offline";
    } else if (live->persistence_failure_reason || live->model_deployment.state == "degraded") {
        h.status = "degraded";
    } else {
        h.status = "online";
    }
    h.persistence_failure_reason = live->persistence_failure_reason;
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
    // Only private mode and the AFK freeze skip prediction. Without a session predictions still
    // run (the Now surface previews them); only persistence stops, so that is what's reported.
    h.prediction_suppression_reason = live->private_mode
                                          ? "private_mode"
                                          : live->idle ? "idle"
                                          : !live->active_session ? "not_recorded"
                                                                  : "none";
    h.permissions =
        check_capture_permissions(capture_.running(), capture_.input_observed());
    h.classifier.backend = live->classifier.backend;
    h.classifier.onnx_runtime_enabled = live->classifier.onnx_runtime_enabled;
    h.classifier.model_path = live->classifier.model_path;
    h.model_deployment = live->model_deployment;
    h.runtime = runtime_metrics();
    h.developer_tools_enabled = developer_tools_enabled();
    return h;
}

RuntimeMetrics AppState::runtime_metrics() const {
    // Relaxed atomics and OS calls only: health() is what a stalled app is asked for.
    RuntimeMetrics out;
    out.persistence_failures = persistence_failures_.load(std::memory_order_relaxed);
    out.persistence_dropped_predictions = persistence_dropped_predictions_.load(std::memory_order_relaxed);
    out.engine_wakeups = engine_wakeups();
    out.engine_max_drain_ms = engine_max_drain_ms_.load(std::memory_order_relaxed);
    out.maintenance_pending = maintenance_pending_.load(std::memory_order_acquire);
    out.maintenance_running = maintenance_running_.load(std::memory_order_acquire);
    out.maintenance_rows_deleted = maintenance_rows_deleted_.load(std::memory_order_relaxed);
    out.maintenance_elapsed_ms = maintenance_elapsed_ms_.load(std::memory_order_relaxed);
    static constexpr const char* results[] = {"waiting_for_ui", "pending", "running", "succeeded", "failed", "cancelled"};
    out.maintenance_result = results[maintenance_result_.load(std::memory_order_acquire)];
    out.process_cpu_ms = process_cpu_ms();
    out.capture_ring_high_water = static_cast<std::uint64_t>(capture_ring_high_water());
    out.capture_ring_capacity = static_cast<std::uint64_t>(CaptureThread::kCapacity);

    const auto storage_lock = lock_metrics(LockRank::Storage);
    out.storage_lock_acquisitions = storage_lock.acquisitions;
    out.storage_lock_contended = storage_lock.contended;
    out.storage_lock_hold_p50_us = storage_lock.hold_p50_us;
    out.storage_lock_hold_p95_us = storage_lock.hold_p95_us;
    out.storage_lock_max_hold_us = storage_lock.max_hold_us;
    out.storage_lock_wait_p95_us = storage_lock.wait_p95_us;
    out.storage_lock_max_wait_us = storage_lock.max_wait_us;

    const auto busy = storage_busy_stats();
    out.sqlite_busy_waits = busy.waits;
    out.sqlite_busy_exhausted = busy.exhausted;
    out.sqlite_busy_max_wait_ms = busy.max_wait_ms;
    return out;
}

SqliteBusySnapshot AppState::storage_busy_stats() const {
    // No storage_mutex_: the counters are atomics with a stable address, and a diagnostic must
    // not queue behind the contention it measures.
    return storage_.busy_stats();
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
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    auto out = std::move(latest_snapback_);
    clear_snapback_unlocked(false);
    if (out) live_read_dirty_ = true;
    publish_live_read_unlocked();
    return out;
}

void AppState::dismiss_snapback() {
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    // Clear the pending payload and return the tracker from Recovering to Focused so it
    // doesn't keep the recovery state latched.
    clear_snapback_unlocked(false);
    context_tracker_.dismiss_recovery(last_event_secs_);
    live_read_dirty_ = true;
    publish_live_read_unlocked();
}

FocusTargetResult AppState::restore_snapback_target() {
    // Read the target, attempt activation, and only then decide what to keep, so a failed
    // activation leaves something to retry.
    std::string app_name;
    std::string window_title;
    {
        std::lock_guard lock(mutex_);
        if (latest_snapback_) {
            app_name = latest_snapback_->app_name;
            window_title = latest_snapback_->window_title;
        }
    }

    if (app_name.empty() && window_title.empty()) {
        return FocusTargetResult{false, "No active snapback context to restore"};
    }

    // Outside the lock: focus_window() talks to the window system and can block.
    const auto result = focus_window(app_name, window_title);

    {
        std::lock_guard lock(mutex_);
        // The tracker leaves Recovering either way (dismiss_recovery() is its only exit). A
        // failed
        // activation keeps the payload so "Take me back" can be retried.
        if (result.ok) clear_snapback_unlocked(false);
        context_tracker_.dismiss_recovery(last_event_secs_);
        live_read_dirty_ = true;
        publish_live_read_unlocked();
    }
    return result;
}

SessionRecap AppState::session_recap(const std::string& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.recap(session_id);
}

std::optional<FocusLabel> AppState::session_auto_label(const std::string& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.session_auto_label(session_id);
}

std::vector<FocusCurvePoint> AppState::session_focus_curve(const std::string& session_id,
                                                          std::size_t buckets) {
    // A Review read: yields to a waiting persist.
    storage_priority_.yield_to_writers();
    std::lock_guard lock(storage_mutex_);
    return storage_.session_focus_curve(session_id, buckets);
}

std::optional<SnapbackEpisode> AppState::session_longest_snapback(const std::string& session_id) {
    storage_priority_.yield_to_writers();
    std::lock_guard lock(storage_mutex_);
    return storage_.longest_snapback_episode(session_id);
}

std::optional<SessionRecord> AppState::save_session_reflection(
    const std::string& session_id, const std::optional<std::string>& done,
    const std::optional<std::string>& next_step) {
    // Writes storage, then updates the cached active session. Both locks, in LockRank order.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    auto saved = storage_.save_session_reflection(session_id, done, next_step);
    if (saved && active_session_ && active_session_->session_id == session_id) {
        active_session_ = saved;
        // Republish the snapshot active_session() reads from.
        live_read_dirty_ = true;
        publish_live_read_unlocked();
    }
    return saved;
}

std::vector<PredictionRecord> AppState::prediction_history(std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    return storage_.recent_predictions(limit);
}

FocusSummary AppState::focus_summary_for_window(const std::string& window,
                                                const std::optional<std::string>& since) {
    const auto cutoff = review_window_cutoff(window, since, cutoff_unix_ms);
    // Same aggregate and cutoff as summary_report, so both "Longest focus" figures agree.
    Storage::PredictionStats stats;
    {
        storage_priority_.yield_to_writers();  // a waiting persist goes first
        std::lock_guard lock(storage_mutex_);
        stats = storage_.prediction_stats(cutoff);
    }
    FocusSummary summary;
    summary.sample_count = stats.sample_count;
    summary.avg_focus_score = stats.avg_focus_score;
    summary.peak_focus_score = stats.peak_focus_score;
    summary.distracted_samples = stats.distracted_count;
    summary.distracted_fraction =
        stats.sample_count == 0 ? 0.0
                                : static_cast<double>(stats.distracted_count) /
                                      static_cast<double>(stats.sample_count);
    summary.longest_focus_secs = stats.longest_focus_secs;
    return summary;
}

std::vector<SessionSummary> AppState::session_history_for_window(
    const std::string& window, const std::optional<std::string>& since) {
    const auto cutoff = review_window_cutoff(window, since, cutoff_unix_ms);
    storage_priority_.yield_to_writers();  // a waiting persist goes first
    std::lock_guard lock(storage_mutex_);
    return storage_.recent_session_summaries(500, cutoff);
}

std::vector<SessionSummary> AppState::session_history(std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    // Was 1 + 5N queries (recap() is five statements), all holding storage_mutex_ — which
    // the engine tick also takes to persist, so opening history could stall capture writes.
    return storage_.recent_session_summaries(limit);
}

ActivityDeletionResult AppState::delete_all_activity_data() {
    WakeOnExit wake{engine_wake_};
    // The boundary fences off-lock persistence in engine_tick(). Incrementing the epoch
    // also invalidates any event already queued for asynchronous UI dispatch.
    std::lock_guard state_lock(mutex_);
    std::lock_guard activity_lock(activity_boundary_mutex_);
    std::lock_guard store_lock(storage_mutex_);
    activity_epoch_.fetch_add(1, std::memory_order_release);
    // Drop events already queued: they were captured before the delete and must not be filed
    // afterwards. (Bounded; see CaptureThread::discard_pending_events.)
    capture_.discard_pending_events();

    ActivityDeletionResult result;

    // Database first, then every replica, each attempted regardless of earlier failures.
    storage_.delete_all_activity_data();
    result.deleted.emplace_back("your recorded sessions, predictions, and captured windows");

    for (const auto& artifact : activity_artifacts(app_data_dir_)) {
        if (app_data_dir_.empty()) break;
        std::error_code error;
        if (artifact.is_directory) {
            std::filesystem::remove_all(artifact.path, error);
        } else {
            std::filesystem::remove(artifact.path, error);
        }
        if (error) {
            result.failed.emplace_back(std::string(artifact.label) + " (" + error.message() + ")");
        } else {
            // An export that never existed counts as removed.
            result.deleted.emplace_back(artifact.label);
        }
    }
    for (const char* retained : kRetainedArtifacts) result.retained.emplace_back(retained);

    active_session_.reset();
    signal_maintenance([this] { maintenance_paused_.store(false, std::memory_order_release); });
    session_attended_ = false;  // every span was deleted with the rows above
    committed_session_attended_ = false;
    discard_pending_span_unlocked();  // and there is no session left for one to name
    latest_prediction_.reset();
    clear_snapback_unlocked();
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
    return result;
}

ExportTrainingResult AppState::export_training_data(
    const std::filesystem::path& out_dir, const std::optional<std::string>& session_id) {
    std::filesystem::path db_path;
    {
        std::lock_guard lock(storage_mutex_);
        db_path = storage_.database_path();
        if (db_path.empty()) {
            // In-memory databases cannot be observed by a second SQLite connection. Keep the
            // shared locked path for unit tests and explicitly in-memory instances only.
            return storage_.export_training_csv(out_dir, session_id);
        }
    }
    auto reader = Storage::open_read_only(db_path);
    return reader.export_training_csv(out_dir, session_id);
}

PersonalArchiveExport AppState::export_personal_data(const std::filesystem::path& out_dir,
                                                     std::size_t page_size) {
    // Streamed, not built: rows are read a page at a time under the lock and written before
    // the next page, so peak memory is one page. Keyset cursors rather than OFFSET, which skips
    // or repeats rows while the engine keeps recording.
    if (page_size == 0) page_size = 1;

    PersonalArchive archive;
    archive.generated_at_ms = now_unix_ms();
    archive.app_version = SNAPBACK_VERSION;

    std::filesystem::create_directories(out_dir);
    const auto path = out_dir / "snapback_my_data.md";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("failed to write the data export");

    // The checksum accumulates over the body as it is written, so the whole document never
    // has to exist in memory at once just to be hashed.
    std::string body;
    const auto emit = [&](const std::string& chunk) {
        body += chunk;
        out << chunk;
    };

    PersonalArchiveExport result;
    emit(render_archive_header(archive));

    std::optional<Storage::SessionCursor> session_cursor;
    std::size_t index = 0;
    for (;;) {
        std::vector<SessionSummary> page;
        {
            std::lock_guard lock(storage_mutex_);
            const auto records = storage_.sessions_after(session_cursor, page_size);
            if (records.empty()) break;
            session_cursor = Storage::SessionCursor{records.back().started_at_ms.value_or(0),
                                                    records.back().session_id};
            page.reserve(records.size());
            for (const auto& record : records) {
                SessionSummary summary;
                summary.record = record;
                summary.recap = storage_.recap(record.session_id);
                page.push_back(std::move(summary));
            }
        }

        for (auto& summary : page) {
            const auto session_id = summary.record.session_id;
            PersonalArchiveSession session;
            session.record = std::move(summary.record);
            session.recap = std::move(summary.recap);

            std::size_t window_total = 0;
            {
                std::lock_guard lock(storage_mutex_);
                window_total = storage_.count_context_snapshots(session_id);
            }

            emit(render_archive_session_header(session, ++index));
            std::optional<Storage::EpisodeCursor> episode_cursor;
            bool wrote_episode_header = false;
            for (;;) {
                Storage::EpisodePage episode_page;
                {
                    std::lock_guard lock(storage_mutex_);
                    episode_page =
                        storage_.snapback_episodes_after(session_id, episode_cursor, page_size);
                }
                if (episode_page.rows.empty()) break;
                if (!wrote_episode_header) {
                    emit(render_archive_episode_table_header());
                    wrote_episode_header = true;
                }
                for (const auto& episode : episode_page.rows) {
                    emit(render_archive_episode_row(episode));
                    ++result.episode_count;
                }
                episode_cursor = episode_page.next;
            }
            ++result.session_count;

            if (window_total == 0) {
                emit("No windows were captured during this session.\n\n");
                continue;
            }

            emit(render_archive_window_table_header());
            std::optional<Storage::ContextCursor> context_cursor;
            for (;;) {
                Storage::ContextPage window_page;
                {
                    std::lock_guard lock(storage_mutex_);
                    window_page =
                        storage_.context_snapshots_after(session_id, context_cursor, page_size);
                }
                if (window_page.rows.empty()) break;
                for (const auto& snapshot : window_page.rows) {
                    emit(render_archive_window_row(snapshot));
                    ++result.window_count;
                }
                context_cursor = window_page.next;
            }
            emit("\n");
        }
    }

    if (result.session_count == 0) {
        emit("No sessions have been recorded yet, so there is nothing here about you.\n");
    }

    // Computed rather than assumed zero, so a reintroduced cap shows up in the footer.
    result.checksum = archive_checksum(body);
    out << render_archive_footer(result);
    if (!out) throw std::runtime_error("failed to write the data export");
    out.close();
    if (!out) throw std::runtime_error("failed to write the data export");

    result.output_path = path.string();
    return result;
}

void AppState::commit_settings_unlocked(AppSettings candidate,
                                        const std::function<void()>& publish) {
    // Persist the candidate, then commit and publish it. Requires mutex_. The write takes a
    // copy, so a throw leaves settings_ and every live field untouched -- e.g. a failed
    // "private
    // mode off" must not resume recording. Nothing after the write can fail.
    if (!app_data_dir_.empty()) save_app_settings(app_data_dir_, candidate);
    settings_ = std::move(candidate);
    if (publish) publish();
}

void AppState::set_focus_mode(FocusMode mode) {
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.default_focus_mode = mode;
    commit_settings_unlocked(std::move(candidate), [this, mode] { focus_mode_ = mode; });
}

bool AppState::claim_tray_close_notice() {
    std::lock_guard lock(mutex_);
    if (settings_.tray_close_notice_shown) return false;
    AppSettings candidate = settings_;
    candidate.tray_close_notice_shown = true;
    // If the write throws, the notice shows again next close rather than being spent.
    commit_settings_unlocked(std::move(candidate), nullptr);
    return true;
}

AppSettings AppState::settings() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

PrivacySettings AppState::privacy_settings() const {
    std::lock_guard lock(mutex_);
    return PrivacySettings{settings_.private_mode, settings_.excluded_apps, true};
}

void AppState::set_idle_threshold_secs(std::int64_t seconds) {
    WakeOnExit wake{engine_wake_};
    // Validated before anything is mutated.
    if (seconds < kMinIdleThresholdSecs || seconds > kMaxIdleThresholdSecs) {
        throw std::runtime_error("idle threshold must be between " +
                                 std::to_string(kMinIdleThresholdSecs) + " and " +
                                 std::to_string(kMaxIdleThresholdSecs) + " seconds");
    }
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.idle_threshold_secs = seconds;
    commit_settings_unlocked(std::move(candidate), [this, seconds] {
        // Applied to the running detector, after the commit.
        idle_detector_.set_threshold_ms(seconds * 1000);
    });
}

// Lapses a timed pause whose deadline has passed; reports whether it did. Requires mutex_.
bool AppState::lapse_private_pause_unlocked() {
    if (!settings_.private_mode || settings_.private_until_wall_ms == 0) return false;
    if (now_unix_ms() < settings_.private_until_wall_ms) return false;
    // Clear the mode and the deadline together.
    AppSettings candidate = settings_;
    candidate.private_mode = false;
    candidate.private_until_wall_ms = 0;
    try {
        commit_settings_unlocked(std::move(candidate), [] {});
    } catch (const std::exception& error) {
        // Fail *closed*: if the resume cannot be written down, stay paused rather than
        // resuming on a promise the next launch will not remember making.
        log().warn(std::string("privacy: could not lapse the timed pause: ") + error.what());
        return false;
    }
    return true;
}

RecordingStatus AppState::recording_status() {
    RecordingInputs inputs;
    std::int64_t private_until_wall_ms = 0;
    std::int64_t snoozed_until_wall_ms = 0;
    bool capture_running = false;
    bool input_observed = false;
    {
        std::lock_guard lock(mutex_);
        lapse_private_pause_unlocked();

        // Same sources as health(), so the two cannot disagree.
        inputs.capture_failed = capture_.failed();
        inputs.private_mode = settings_.private_mode;
        inputs.has_active_session = active_session_.has_value();
        inputs.idle = idle_;
        private_until_wall_ms = settings_.private_until_wall_ms;
        snoozed_until_wall_ms = settings_.alerts.snoozed_until_wall_ms;
        capture_running = capture_.running();
        input_observed = capture_.input_observed();
    }
    // OS permission probes (and on Linux, `command -v` subprocesses) must not run under
    // mutex_: they would stall the engine drain for the duration of the probe.
    inputs.capture_permitted =
        check_capture_permissions(capture_running, input_observed).capture_available;

    RecordingStatus status;
    status.state = derive_recording_state(inputs);
    // The countdown describes the pause even when a higher-priority state (e.g. Blocked) is
    // shown.
    if (inputs.private_mode && private_until_wall_ms > 0) {
        const auto left = private_until_wall_ms - now_unix_ms();
        status.private_pause_remaining_ms = left > 0 ? left : 0;
    }
    // Reported alongside `state`: a snooze is about delivery, not recording.
    if (snoozed_until_wall_ms > 0) {
        const auto left = snoozed_until_wall_ms - now_unix_ms();
        status.alert_snooze_remaining_ms = left > 0 ? left : 0;
    }
    return status;
}

// Every write that changes "am I being recorded?" announces the new answer, since tray
// actions bypass the page. Same shape as get_recording_status.
RecordingStatus AppState::announce_recording_status() {
    RecordingStatus status = recording_status();
    emit_event(events::kRecordingStatus, dump_json(nlohmann::json(status)));
    return status;
}

RecordingStatus AppState::pause_privately_for(std::int64_t minutes) {
    WakeOnExit wake{engine_wake_};
    if (minutes < 0) throw std::runtime_error("a privacy pause cannot be negative");
    {
        std::lock_guard lock(mutex_);
        AppSettings candidate = settings_;
        candidate.private_mode = true;
        // 0 means indefinite. A timed pause stores its end instant, so closing the window does
        // not
        // restart it.
        candidate.private_until_wall_ms = minutes > 0 ? now_unix_ms() + minutes * 60 * 1000 : 0;
        commit_settings_unlocked(std::move(candidate), [] {});
    }
    return announce_recording_status();
}

// Snooze and its undo. An expired snooze needs no repair write: route_alert already treats a
// passed deadline as lapsed.
RecordingStatus AppState::snooze_alerts_for(std::int64_t minutes) {
    WakeOnExit wake{engine_wake_};
    if (minutes < 0) throw std::runtime_error("an alert snooze cannot be negative");
    if (minutes > kMaxAlertSnoozeMins) {
        throw std::runtime_error("an alert snooze cannot exceed " +
                                 std::to_string(kMaxAlertSnoozeMins) + " minutes");
    }
    {
        std::lock_guard lock(mutex_);
        AppSettings candidate = settings_;
        const auto span = minutes > 0 ? minutes : kDefaultAlertSnoozeMins;
        // An instant, not a duration -- the same reasoning as the privacy pause above. A
        // duration would restart every time the window was hidden and reopened.
        candidate.alerts.snoozed_until_wall_ms = now_unix_ms() + span * 60 * 1000;
        commit_settings_unlocked(std::move(candidate), [] {});
    }
    return announce_recording_status();
}

AppSettings AppState::set_alert_delivery(AlertDeliverySettings alerts) {
    WakeOnExit wake{engine_wake_};
    const auto in_day = [](std::int32_t minute) {
        return minute >= 0 && minute < kMinutesPerDay;
    };
    // Rejected rather than clamped, matching set_idle_threshold_secs: a rejected setting is
    // visible to the user, a clamped one is a value they never chose quietly taking effect.
    if (!in_day(alerts.quiet_hours_start_min) || !in_day(alerts.quiet_hours_end_min)) {
        throw std::runtime_error("quiet hours must be minutes of the day (0-1439)");
    }
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    // The snooze deadline is kept from live settings; a Settings save must not overwrite it.
    alerts.snoozed_until_wall_ms = settings_.alerts.snoozed_until_wall_ms;
    candidate.alerts = std::move(alerts);
    commit_settings_unlocked(std::move(candidate), [] {});
    return settings_;
}

RecordingStatus AppState::resume_alerts() {
    WakeOnExit wake{engine_wake_};
    {
        std::lock_guard lock(mutex_);
        AppSettings candidate = settings_;
        candidate.alerts.snoozed_until_wall_ms = 0;
        commit_settings_unlocked(std::move(candidate), [] {});
    }
    return announce_recording_status();
}

RecordingStatus AppState::resume_from_private_pause() {
    WakeOnExit wake{engine_wake_};
    {
        std::lock_guard lock(mutex_);
        AppSettings candidate = settings_;
        candidate.private_mode = false;
        candidate.private_until_wall_ms = 0;
        commit_settings_unlocked(std::move(candidate), [] {});
    }
    return announce_recording_status();
}

void AppState::dismiss_untracked_nudge(std::int64_t minutes) {
    WakeOnExit wake{engine_wake_};
    if (minutes <= 0 || minutes > 24 * 60) {
        throw std::runtime_error("nudge dismissal must be between 1 minute and 24 hours");
    }
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.untracked_nudge_until_wall_ms = now_unix_ms() + minutes * 60 * 1000;
    commit_settings_unlocked(std::move(candidate), [this] {
        untracked_minutes_.reset();
        // The deadline is the latch. Keeping the ordinary per-stretch latch set would make
        // expiry ineffective until the user went idle.
        untracked_latched_ = false;
    });
}

void AppState::set_private_mode(bool enabled) {
    WakeOnExit wake{engine_wake_};
    {
        std::lock_guard lock(mutex_);
        AppSettings candidate = settings_;
        candidate.private_mode = enabled;
        commit_settings_unlocked(std::move(candidate), [this] {
            live_read_dirty_ = true;
            publish_live_read_unlocked();
        });
    }
    // Outside the lock: recording_status() probes OS permissions, and the header must learn
    // about a toggle flipped in Settings the same way it learns about one from the tray.
    announce_recording_status();
}

void AppState::set_privacy_exclusions(std::vector<std::string> exclusions) {
    WakeOnExit wake{engine_wake_};
    // Normalized outside the lock: it is pure string work on the caller's argument, and the
    // candidate has to be complete before anything is committed.
    auto normalized = normalize_privacy_exclusions(std::move(exclusions));
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.excluded_apps = std::move(normalized);
    // No live mirror: is_private_event_unlocked reads settings_ directly, so committing the
    // candidate *is* publishing it.
    commit_settings_unlocked(std::move(candidate), nullptr);
}

AnalyticsSummary AppState::analytics(const std::string& window,
                                     const std::optional<std::string>& since) const {
    const auto cutoff = review_window_cutoff(window, since, cutoff_unix_ms);
    storage_priority_.yield_to_writers();  // a waiting persist goes first
    std::lock_guard lock(storage_mutex_);
    AnalyticsSummary summary;
    const auto stats = const_cast<Storage&>(storage_).prediction_stats(cutoff);
    summary.sample_count = stats.sample_count;
    summary.avg_focus_score = stats.avg_focus_score;
    summary.hourly = const_cast<Storage&>(storage_).hourly_focus_buckets(cutoff);

    const auto app_counts = const_cast<Storage&>(storage_).context_app_counts(200, 200, cutoff);
    std::vector<AnalyticsApp> apps;
    apps.reserve(app_counts.size());
    for (const auto& [app, count] : app_counts) apps.push_back(AnalyticsApp{app, count});
    std::sort(apps.begin(), apps.end(), [](const auto& left, const auto& right) {
        if (left.window_count != right.window_count) return left.window_count > right.window_count;
        return left.app_name < right.app_name;
    });
    if (apps.size() > 5) apps.resize(5);
    summary.top_apps = std::move(apps);

    summary.productive_session_streak =
        const_cast<Storage&>(storage_).productive_session_streak(200, 70.0, cutoff);
    return summary;
}

DailySummary AppState::daily_summary(const std::string& window,
                                     const std::optional<std::string>& since) const {
    const auto cutoff = review_window_cutoff(window, since, cutoff_unix_ms);
    DailySummary out;
    out.window = window;
    out.generated_at_ms = now_unix_ms();

    // Bounded by the retention window (older rows are pruned); `capped` says the range was cut
    // short rather than empty.
    const std::int64_t retention_floor_ms =
        out.generated_at_ms
        - static_cast<std::int64_t>(kDefaultRetentionDays) * 24 * 60 * 60 * 1000;
    std::int64_t since_ms = retention_floor_ms;
    if (!cutoff || *cutoff < retention_floor_ms) {
        out.capped = true;
    } else {
        since_ms = *cutoff;
    }

    storage_priority_.yield_to_writers();  // a waiting persist goes first
    std::lock_guard lock(storage_mutex_);
    out.days = const_cast<Storage&>(storage_).daily_summary(out.generated_at_ms, since_ms);
    return out;
}

SummaryReport AppState::summary_report(const std::string& window,
                                       const std::optional<std::string>& since) const {
    const auto cutoff_opt = review_window_cutoff(window, since, cutoff_unix_ms);

    // Same lock discipline as attended_progress().
    AppSettings snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
    }

    storage_priority_.yield_to_writers();  // a waiting persist goes first
    std::lock_guard lock(storage_mutex_);
    SummaryReport report;
    report.window = window;
    report.generated_at_ms = now_unix_ms();

    const auto stats = const_cast<Storage&>(storage_).prediction_stats(cutoff_opt);
    report.sample_count = stats.sample_count;
    report.avg_focus_score = stats.avg_focus_score;
    report.longest_focus_secs = stats.longest_focus_secs;
    if (report.sample_count > 0) {
        report.distracted_fraction =
            static_cast<double>(stats.distracted_count) / static_cast<double>(report.sample_count);
    }

    const std::int64_t session_floor_ms = cutoff_opt.value_or(0);
    const auto totals = const_cast<Storage&>(storage_).session_window_totals(
        kSummarySessionLimit, session_floor_ms);
    report.session_count = totals.session_count;
    report.completed_session_count = totals.completed_session_count;
    report.focus_seconds = totals.focus_seconds;
    report.session_limit = kSummarySessionLimit;
    report.sessions_truncated = totals.limit_reached;
    const auto context_counts = const_cast<Storage&>(storage_).context_app_counts(
        kSummarySessionLimit, 200, cutoff_opt);
    // Highest count wins; ties go to the lexicographically smaller name.
    std::size_t top_count = 0;
    for (const auto& [app, count] : context_counts) {
        if (report.top_context_app.empty() || count > top_count ||
            (count == top_count && app < report.top_context_app)) {
            report.top_context_app = app;
            top_count = count;
        }
    }

    // today/7d reuse the calendar day/week helpers so this agrees with the Now surface; other
    // ranges report attended only.
    const auto now = report.generated_at_ms;
    if (window == "day" || window == "today") {
        report.attended_seconds =
            const_cast<Storage&>(storage_).attended_secs_in_local_day(now);
        report.planned_mins = snapshot.attended_target_daily_mins;
    } else if (window == "week" || window == "7d") {
        report.attended_seconds =
            const_cast<Storage&>(storage_).attended_secs_in_local_week(now);
        report.planned_mins = snapshot.attended_target_weekly_mins;
    } else {
        report.attended_seconds =
            const_cast<Storage&>(storage_).attended_secs_since(now, cutoff_opt);
        report.planned_mins = 0;
    }
    return report;
}

SummaryExportResult AppState::export_summary_report(const std::filesystem::path& out_dir,
                                                    const std::string& window,
                                                    const std::optional<std::string>& since) const {
    const auto report = summary_report(window, since);
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
        category.name = trim(category.name);
        if (category.name.empty()) continue;
        std::vector<std::string> keywords;
        for (auto& keyword : category.keywords) {
            keyword = trim(keyword);
            if (!keyword.empty()) keywords.push_back(std::move(keyword));
        }
        if (!keywords.empty()) normalized.push_back(GoalCategory{std::move(category.name), std::move(keywords)});
    }
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.goal_categories = std::move(normalized);
    commit_settings_unlocked(std::move(candidate), [this] {
        // The tracker classifies against these names, so a failed save must not leave the
        // live classifier scoring by categories that are not on disk.
        context_tracker_.set_goal_categories(settings_.goal_categories);
    });
}

std::vector<AppRuleRecord> AppState::app_rules() {
    std::lock_guard lock(storage_mutex_);
    return storage_.list_app_rules();
}

AppRuleRecord AppState::upsert_app_rule(const std::string& pattern, AppRuleKind rule_type,
                                        std::optional<std::string> note) {
    WakeOnExit wake{engine_wake_};
    // Mixed: writes storage_ AND refreshes the in-memory app_rules_ cache the tick reads.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    auto record = storage_.upsert_app_rule(pattern, rule_type, std::move(note));
    reload_app_rules_unlocked();  // the live classifier picks up the change next tick
    return record;
}

void AppState::delete_app_rule(std::int64_t id) {
    WakeOnExit wake{engine_wake_};
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
    WakeOnExit wake{engine_wake_};
    std::lock_guard lock(mutex_);
    model_deployment_health_ = training_deploy::to_model_deployment_health(
        training_deploy::recover_model_deployment_for_startup(app_data_dir_));
    if (const auto model = OnnxModel::resolve_model_path(app_data_dir_)) {
        OnnxModel::instance().init(*model);
    } else {
        OnnxModel::instance().unload();
    }

    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return live_read_snapshot()->classifier;
}

ModelDeploymentHealth AppState::retry_model_deployment_cleanup() {
    std::lock_guard lock(mutex_);
    model_deployment_health_ = training_deploy::to_model_deployment_health(
        training_deploy::retry_model_deployment_cleanup(app_data_dir_));
    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return model_deployment_health_;
}

PermissionStatus AppState::refresh_permissions() {
    // CaptureThread owns synchronization for these fields. Joining the engine mutation lock
    // here made a read-only permission poll wait behind feature/classifier work for no gain.
    return check_capture_permissions(capture_.running(), capture_.input_observed());
}

PermissionStatus AppState::request_permissions() {
    // Without mutex_: the macOS dialog is modal and can take seconds.
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
        exclusion = trim(exclusion);
        if (exclusion.empty()) continue;
        const auto lowered = lower_copy(exclusion);
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const auto& existing) {
            return lower_copy(existing) == lowered;
        });
        if (!duplicate) out.push_back(std::move(exclusion));
    }
    return out;
}

bool AppState::app_matches_exclusion_unlocked(const std::string& app_name) const {
    const auto app = lower_copy(app_name);
    return std::any_of(settings_.excluded_apps.begin(), settings_.excluded_apps.end(),
                       [&](const auto& exclusion) {
                           return contains_whole_app_name_phrase(app, lower_copy(exclusion));
                       });
}

bool AppState::is_private_event_unlocked(const CaptureEvent& event) const {
    return settings_.private_mode || app_matches_exclusion_unlocked(event.app_name);
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

void AppState::notify_frontend_ready() {
    WakeOnExit wake{engine_wake_};
    signal_maintenance([this] {
        if (maintenance_stopping_.load(std::memory_order_acquire) ||
            frontend_ready_.exchange(true, std::memory_order_acq_rel)) return;
        maintenance_result_.store(1, std::memory_order_release);
        maintenance_pending_.store(true, std::memory_order_release);
    });
}

void AppState::request_retention_maintenance() {
    if (maintenance_stopping_.load(std::memory_order_acquire) ||
        !frontend_ready_.load(std::memory_order_acquire)) return;
    // Notifies even when the flag was already set: the wake is unconditional now, and a
    // spurious one costs the worker a single predicate evaluation.
    signal_maintenance([this] {
        if (!maintenance_stopping_.load(std::memory_order_acquire))
            maintenance_pending_.store(true, std::memory_order_release);
    });
}

void AppState::run_retention_maintenance() noexcept {
    for (;;) {
        {
            std::unique_lock lock(maintenance_mutex_);
            auto runnable = [this] {
                return maintenance_pending_.load(std::memory_order_acquire) &&
                       !maintenance_paused_.load(std::memory_order_acquire);
            };
            while (!maintenance_stopping_.load(std::memory_order_acquire)) {
                const auto retry = maintenance_retry_at_ms_.load(std::memory_order_acquire);
                if (runnable() && steady_now_ms() >= retry) break;
                if (runnable()) {
                    maintenance_ready_.wait_for(lock, std::chrono::milliseconds(
                        std::max<std::int64_t>(1, retry - steady_now_ms())));
                } else maintenance_ready_.wait(lock);
            }
        }
        if (maintenance_stopping_.load(std::memory_order_acquire)) return;

        maintenance_running_.store(true, std::memory_order_release);
        maintenance_result_.store(2, std::memory_order_release);
        const auto started = steady_now_ms();
        PruneSummary total;
        bool failed = false;
        try {
            const auto cutoff =
                now_unix_ms() - static_cast<std::int64_t>(kDefaultRetentionDays) *
                                    24 * 60 * 60 * 1000;
            for (;;) {
                if (maintenance_stopping_.load(std::memory_order_acquire)) return;
                if (maintenance_paused_.load(std::memory_order_acquire)) {
                    std::unique_lock lock(maintenance_mutex_);
                    maintenance_ready_.wait(lock, [this] {
                        return maintenance_stopping_.load(std::memory_order_acquire) ||
                               !maintenance_paused_.load(std::memory_order_acquire);
                    });
                    continue;
                }

                PruneBatchSummary batch;
                {
                    std::lock_guard lock(storage_mutex_);
                    // Starting a session raises the pause flag before waiting for this lock.
                    // Recheck after acquiring it so a queued start prevents another batch.
                    if (maintenance_paused_.load(std::memory_order_acquire)) continue;
                    if (maintenance_test_hook_) maintenance_test_hook_();
                    batch = storage_.prune_runtime_data_batch(cutoff,
                                                              kRetentionPruneBatchRows);
                }
                maintenance_rows_deleted_.fetch_add(batch.total(), std::memory_order_relaxed);
                total.predictions_deleted += batch.predictions_deleted;
                total.context_snapshots_deleted += batch.context_snapshots_deleted;
                total.feature_snapshots_deleted += batch.feature_snapshots_deleted;
                if (!batch.has_more) break;

                std::unique_lock lock(maintenance_mutex_);
                maintenance_ready_.wait_for(
                    lock, std::chrono::milliseconds(kRetentionPruneYieldMs), [this] {
                        return maintenance_stopping_.load(std::memory_order_acquire) ||
                               maintenance_paused_.load(std::memory_order_acquire);
                    });
            }
        } catch (const std::exception& err) {
            failed = true;
            try {
                log().warn(std::string("storage: periodic retention prune failed after ") +
                           std::to_string(total.total()) + " rows: " + err.what());
            } catch (...) {
            }
        } catch (...) {
            failed = true;
            try {
                log().warn("storage: periodic retention prune failed: unknown error");
            } catch (...) {
            }
        }

        if (!failed && total.total() > 0) {
            try {
                std::ostringstream msg;
                msg << "storage: periodically pruned " << total.total() << " rows older than "
                    << kDefaultRetentionDays << "d (predictions=" << total.predictions_deleted
                    << ", context_snapshots=" << total.context_snapshots_deleted
                    << ", feature_snapshots=" << total.feature_snapshots_deleted << ")";
                log().info(msg.str());
            } catch (...) {
            }
        }
        maintenance_elapsed_ms_.store(static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, steady_now_ms() - started)), std::memory_order_relaxed);
        maintenance_running_.store(false, std::memory_order_release);
        if (failed) {
            maintenance_retry_at_ms_.store(steady_now_ms() + 30000, std::memory_order_release);
            maintenance_result_.store(4, std::memory_order_release);
        } else {
            maintenance_retry_at_ms_.store(0, std::memory_order_release);
            last_prune_steady_ms_.store(steady_now_ms(), std::memory_order_release);
            maintenance_result_.store(3, std::memory_order_release);
            maintenance_pending_.store(false, std::memory_order_release);
        }
        engine_wake_.notify();
    }
}

bool AppState::engine_tick() {
    // Three phases with different locks so a disk write never blocks an ordinary UI read:
    //   1) drain + classify under mutex_ (in-memory only), collecting persist jobs;
    //   2) flush them under storage_mutex_ in ONE transaction, after releasing mutex_;
    //   3) queue epoch-tagged events holding no lock (the hook hops to the UI thread).
    EmitHook emit_to_frontend;
    std::function<void(const char*)> persistence_test_hook;
    std::optional<PredictionRecord> pred_to_emit;
    std::optional<SnapbackPayload> snap_to_emit;
    AlertRoute snap_route;
    AlertRoute pomodoro_route;
    AlertRoute hyper_route;
    AlertRoute untracked_route;
    // Issued beside each route in phase 1, used in phase 3. 0 means not clickable.
    std::int64_t snap_alert_id = 0;
    std::int64_t pomodoro_alert_id = 0;
    std::int64_t hyper_alert_id = 0;
    std::int64_t untracked_alert_id = 0;
    std::optional<PomodoroStatus> pomodoro_to_emit;
    std::optional<std::uint64_t> hyper_to_emit;
    std::optional<std::uint64_t> untracked_to_emit;
    std::vector<PersistJob> jobs;
    IdleTransition idle_edge = IdleTransition::None;
    // Decided in phase 1, written in phase 2 under storage_mutex_.
    std::vector<PendingSpanTransition> span_transitions;
    // The tick only schedules retention. An owned worker performs bounded storage batches.
    bool prune_due = false;
    bool recording_status_changed = false;
    bool backoff = false;
    bool wrote_data = false;
    bool recovered = false;
    bool report_failure = false;
    std::string failure_category;
    std::optional<SnapbackEpisode> episode_to_persist;
    std::exception_ptr persistence_error;
    std::uint64_t tick_activity_epoch = 0;
    std::uint64_t snapback_generation = 0;
    // Whether phase 1 gave up on a budget rather than on an empty ring. Returned to the engine
    // loop, which then re-ticks immediately instead of sleeping out the tick interval.
    bool drain_truncated = false;
    // Set under mutex_ when the throttle allows a backlog line; written to the log after the
    // lock is released.
    std::optional<std::size_t> backlog_to_log;
    {
        std::lock_guard lock(mutex_);
        tick_activity_epoch = activity_epoch_.load(std::memory_order_acquire);
        bool had_input = false;
        // Bounded so sustained input cannot hold mutex_ indefinitely. Leftovers stay in the
        // ring for
        // the next tick.
        std::size_t drained = 0;
        const auto drain_started_ms = steady_now_ms();
        while (drained < kEngineDrainBudget) {
            auto ev = capture_.next_event();
            if (!ev) break;
            ++drained;
            if (is_input_event(ev->event_type)) {
                had_input = true;
                // Apply the wake edge before compute_event so the waking event is not dropped
                // by the AFK
                // freeze.
                if (idle_) {
                    if (!is_private_event_unlocked(*ev)) idle_foreground_context_ = *ev;
                    idle_edge = update_idle_unlocked(steady_now_ms(), true, &*ev);
                }
            }
            if (auto job = compute_event(*ev)) jobs.push_back(std::move(*job));
            // The count budget needs no clock (keeps ManualClock ticks deterministic); this
            // covers heavy
            // per-event work.
            if (drained % kEngineDrainClockCheckStride == 0 &&
                steady_now_ms() - drain_started_ms >= kEngineDrainBudgetMs) {
                drain_truncated = true;
                break;
            }
        }
        // Ask the ring rather than inferring a backlog from the counter.
        if (drained >= kEngineDrainBudget && capture_.has_pending_events()) {
            drain_truncated = true;
        }
        if (drain_truncated) {
            // Throttled. Decided under mutex_, written after release so a slow log sink does
            // not extend
            // the critical section.
            const auto log_now_ms = steady_now_ms();
            // 0 is a valid steady-clock reading, hence the optional.
            if (!last_drain_backlog_log_ms_ ||
                log_now_ms - *last_drain_backlog_log_ms_ >= kEngineBacklogLogIntervalMs) {
                last_drain_backlog_log_ms_ = log_now_ms;
                backlog_to_log = drained;
            }
        }
        // Idle timing runs off the tick's monotonic clock, not event timestamps: true AFK
        // means no events arrive at all, so we must measure wall time, not the last event.
        const auto now_ms = steady_now_ms();
        if (settings_.private_mode && settings_.private_until_wall_ms > 0 &&
            now_unix_ms() >= settings_.private_until_wall_ms && now_ms >= privacy_lapse_retry_at_ms_) {
            recording_status_changed = lapse_private_pause_unlocked();
            privacy_lapse_retry_at_ms_ = recording_status_changed ? 0 : now_ms + 30000;
        }
        const auto snooze = settings_.alerts.snoozed_until_wall_ms;
        if (snooze > 0 && snooze <= now_unix_ms() && snooze_expiry_reported_wall_ms_ != snooze) {
            snooze_expiry_reported_wall_ms_ = snooze;
            recording_status_changed = true;
        }
        if (drained > 0) {
            const auto elapsed = static_cast<std::uint64_t>(std::max<std::int64_t>(0, now_ms - drain_started_ms));
            auto maximum = engine_max_drain_ms_.load(std::memory_order_relaxed);
            while (elapsed > maximum && !engine_max_drain_ms_.compare_exchange_weak(maximum, elapsed)) {}
        }
        const auto final_idle_edge = update_idle_unlocked(now_ms, had_input);
        if (final_idle_edge != IdleTransition::None) idle_edge = final_idle_edge;
        // Copy without consuming: a failed transaction must leave the queue intact.
        backoff = persistence_failure_reason_ && steady_now_ms() < persistence_retry_at_ms_;
        episode_to_persist = pending_snapback_episode_;
        span_transitions.assign(pending_span_transitions_.begin(),
                                pending_span_transitions_.end());
        if (frontend_ready_.load(std::memory_order_acquire) &&
            now_ms - last_prune_steady_ms_.load(std::memory_order_acquire) >=
            kRetentionPruneIntervalMs) {
            prune_due = true;
        }
        if (pomodoro_.poll(now_ms)) {
            pomodoro_to_emit = pomodoro_.status(now_ms);
            // Computed under mutex_ because settings_ needs it; phase 3 holds no lock.
            pomodoro_route = alert_route_unlocked(AlertEvent::Pomodoro);
            pomodoro_alert_id = issue_alert_id_unlocked(AlertEvent::Pomodoro, pomodoro_route);
        }
        emit_to_frontend = emit_hook_;
        persistence_test_hook = persistence_test_hook_;
        if (prediction_dirty_) {
            pred_to_emit = latest_prediction_;
            prediction_dirty_ = false;
        }
        // Emit once but keep the payload: "Take me back" reads it later. Only arm the one-shot
        // once a
        // listener exists; the engine starts before main installs the hook.
        if (emit_to_frontend && latest_snapback_ && !snapback_emitted_) {
            snap_to_emit = *latest_snapback_;
            snap_route = latest_snapback_route_;
            snapback_generation = snapback_generation_;
        }
        if (hyperfocus_minutes_) {
            hyper_to_emit = hyperfocus_minutes_;
            hyperfocus_minutes_.reset();
            // Computed under mutex_. Always visible here: the latch refused suppressed nudges.
            hyper_route = alert_route_unlocked(AlertEvent::Hyperfocus);
            hyper_alert_id = issue_alert_id_unlocked(AlertEvent::Hyperfocus, hyper_route);
        }
        if (untracked_minutes_) {
            untracked_to_emit = untracked_minutes_;
            untracked_minutes_.reset();
            untracked_route = alert_route_unlocked(AlertEvent::UntrackedWork);
            untracked_alert_id =
                issue_alert_id_unlocked(AlertEvent::UntrackedWork, untracked_route);
        }
        publish_live_read_unlocked();
    }

    if (backlog_to_log) {
        log().info("engine: capture backlog, drained " + std::to_string(*backlog_to_log) +
                   " events this tick and yielded the state lock");
    }

    if (prune_due) request_retention_maintenance();
    if (recording_status_changed) announce_recording_status();

    try {
        // If deletion won the boundary after phase 1, discard every buffered row and
        // event. If this tick won, deletion waits until persistence has completed.
        std::lock_guard activity_lock(activity_boundary_mutex_);
        if (tick_activity_epoch != activity_epoch_.load(std::memory_order_acquire)) {
            return drain_truncated;
        }
        if (!backoff && (!jobs.empty() || !span_transitions.empty() || episode_to_persist || snap_to_emit)) {
            // Announced only while waiting for the lock.
            WriterPriority::Announce announce(storage_priority_);
            std::lock_guard lock(storage_mutex_);
            announce.release();
            // Resolve each boundary before BEGIN. Even a BEGIN failure therefore leaves the
            // retry carrying the original Storage-clock instant rather than a fresh "now".
            for (auto& transition : span_transitions) {
                if (!transition.timestamp_ms) {
                    transition.timestamp_ms =
                        storage_.session_span_timestamp_now(transition.millis_ago + std::max<std::int64_t>(0,
                            steady_now_ms() - transition.decided_at_steady_ms.value_or(steady_now_ms())));
                }
            }
            if (persistence_test_hook) persistence_test_hook("begin");
            Storage::Transaction txn(storage_);  // one commit for the whole drain
            if (persistence_test_hook) persistence_test_hook("write");
            for (const auto& job : jobs) persist(job);
            if (episode_to_persist) storage_.insert_snapback_episode(*episode_to_persist);
            for (const auto& transition : span_transitions) {
                if (transition.opens) {
                    storage_.begin_session_span(transition.session_id,
                                                *transition.timestamp_ms);
                } else {
                    storage_.close_session_span(transition.session_id,
                                                *transition.timestamp_ms);
                }
            }
            if (persistence_test_hook) persistence_test_hook("commit");
            txn.commit();
            wrote_data = !span_transitions.empty() || episode_to_persist.has_value() ||
                std::any_of(jobs.begin(), jobs.end(), [](const PersistJob& job) {
                    return !job.session_id.empty();
                });
        }
    } catch (...) {
        // Take the state lock only after releasing the storage and activity locks (ranked
        // order).
        std::lock_guard lock(mutex_);
        for (const auto& attempted : span_transitions) {
            const auto pending = std::find_if(
                pending_span_transitions_.begin(), pending_span_transitions_.end(),
                [&](const PendingSpanTransition& item) { return item.id == attempted.id; });
            if (pending != pending_span_transitions_.end() && !pending->timestamp_ms) {
                pending->timestamp_ms = attempted.timestamp_ms;
            }
        }
        persistence_error = std::current_exception();
        failure_category = "write_failed";
        try { std::rethrow_exception(persistence_error); }
        catch (const SqliteError& error) {
            switch (error.code() & 0xff) {
                case SQLITE_BUSY: case SQLITE_LOCKED: failure_category = "database_busy"; break;
                case SQLITE_FULL: failure_category = "disk_full"; break;
                case SQLITE_IOERR: case SQLITE_CANTOPEN: case SQLITE_READONLY:
                case SQLITE_PERM: failure_category = "filesystem_io"; break;
                default: break;
            }
        } catch (...) {}
        report_failure = !persistence_failure_reason_ || persistence_failure_category_ != failure_category;
        persistence_failure_category_ = failure_category;
        persistence_failure_reason_ = "Some activity could not be saved. Retrying automatically; lost samples cannot be restored.";
        persistence_retry_delay_ms_ = persistence_retry_delay_ms_ == 0 ? 1000 :
            std::min<std::int64_t>(30000, persistence_retry_delay_ms_ * 2);
        persistence_retry_at_ms_ = steady_now_ms() + persistence_retry_delay_ms_;
        persistence_failures_.fetch_add(1, std::memory_order_relaxed);
        live_read_dirty_ = true;
        publish_live_read_unlocked();
    }
    if (backoff || persistence_error) {
        const auto lost = std::count_if(jobs.begin(), jobs.end(), [](const PersistJob& job) {
            return !job.session_id.empty() && job.prediction.has_value();
        });
        persistence_dropped_predictions_.fetch_add(static_cast<std::uint64_t>(lost), std::memory_order_relaxed);
        snap_to_emit.reset();
    }

    {
        std::lock_guard lock(mutex_);
        if (wrote_data && !persistence_error) {
            recovered = persistence_failure_reason_.has_value();
            persistence_failure_reason_.reset();
            persistence_failure_category_.clear();
            persistence_retry_delay_ms_ = 0;
            persistence_retry_at_ms_ = 0;
            if (episode_to_persist && pending_snapback_episode_ &&
                pending_snapback_episode_->started_at_ms == episode_to_persist->started_at_ms &&
                pending_snapback_episode_->session_id == episode_to_persist->session_id) {
                pending_snapback_episode_.reset();
            }
            if (recovered) { live_read_dirty_ = true; publish_live_read_unlocked(); }
        }
        if (!backoff && !persistence_error) for (const auto& committed : span_transitions) {
            const auto pending = std::find_if(
                pending_span_transitions_.begin(), pending_span_transitions_.end(),
                [&](const PendingSpanTransition& item) { return item.id == committed.id; });
            if (pending == pending_span_transitions_.end()) continue;
            if (active_session_ && active_session_->session_id == committed.session_id) {
                committed_session_attended_ = committed.opens;
            }
            pending_span_transitions_.erase(pending);
        }

        // Persistence owns acknowledgement. If the transaction threw, control never reaches
        // this block and the same payload remains eligible on the next tick.
        if (snap_to_emit) {
            if (latest_snapback_ && !snapback_emitted_ &&
                snapback_generation_ == snapback_generation) {
                snap_alert_id = issue_alert_id_unlocked(AlertEvent::Snapback, snap_route);
                snapback_emitted_ = true;
            } else {
                snap_to_emit.reset();
            }
        }
    }

    if (!emit_to_frontend) {
        if (persistence_error) std::rethrow_exception(persistence_error);
        return drain_truncated;
    }
    if (report_failure) {
        emit_to_frontend(events::kPersistenceFailed,
            dump_json(nlohmann::json{{"reason", failure_category}, {"message",
                "Some activity could not be saved. Retrying automatically; lost samples cannot be restored."}}),
            tick_activity_epoch);
    }
    if (recovered) emit_to_frontend(events::kPersistenceRecovered, "{}", tick_activity_epoch);
    if (idle_edge == IdleTransition::WentIdle) {
        emit_to_frontend(events::kIdle, "{\"idle\":true}", tick_activity_epoch);
    }
    if (idle_edge == IdleTransition::WokeUp) {
        emit_to_frontend(events::kIdle, "{\"idle\":false}", tick_activity_epoch);
    }
    if (pred_to_emit) {
        emit_to_frontend(events::kPrediction, dump_json(nlohmann::json(*pred_to_emit)),
                         tick_activity_epoch);
    }
    if (snap_to_emit) {
        // The route rides on the payload so delivery reads a flag instead of re-deriving
        // policy.
        auto payload = nlohmann::json(*snap_to_emit);
        payload["delivery"] = nlohmann::json(snap_route);
        payload["alertId"] = snap_alert_id;
        emit_to_frontend(events::kSnapback, dump_json(payload), tick_activity_epoch);
    }
    if (pomodoro_to_emit) {
        // Never suppressed: the event also drives the timer card. The preference governs the
        // alert.
        auto payload = nlohmann::json(*pomodoro_to_emit);
        payload["delivery"] = nlohmann::json(pomodoro_route);
        payload["alertId"] = pomodoro_alert_id;
        emit_to_frontend(events::kPomodoro, dump_json(payload), tick_activity_epoch);
    }
    if (hyper_to_emit) {
        // In-app copy stays detailed; preview mode governs lock-screen wording, chosen in
        // main.cpp.
        const auto note = build_hyperfocus_notification(*hyper_to_emit);
        emit_to_frontend(events::kHyperfocus,
                         dump_json(nlohmann::json{{"message", note.body},
                                                  {"minutes", *hyper_to_emit},
                                                  {"alertId", hyper_alert_id},
                                                  {"delivery", nlohmann::json(hyper_route)}}),
                         tick_activity_epoch);
    }
    if (untracked_to_emit) {
        const auto note = build_untracked_work_notification(*untracked_to_emit);
        emit_to_frontend(events::kUntrackedWork,
                         dump_json(nlohmann::json{{"message", note.body},
                                                  {"minutes", *untracked_to_emit},
                                                  {"alertId", untracked_alert_id},
                                                  {"delivery", nlohmann::json(untracked_route)}}),
                         tick_activity_epoch);
    }
    if (persistence_error) std::rethrow_exception(persistence_error);
    return drain_truncated;
}

std::optional<AppState::PersistJob> AppState::compute_event(const CaptureEvent& event) {
    // Requires mutex_. In-memory only: advances features/classifier/tracker and returns the
    // rows
    // to persist. The foreground app is remembered even for dropped events so excluded-app time
    // cannot earn an untracked nudge.
    last_capture_app_ = event.app_name;
    if (is_private_event_unlocked(event)) return std::nullopt;
    last_event_secs_ = event.timestamp_secs;

    // Foreground edges while AFK stay out of the rolling windows; keep their context to
    // reconcile
    // once after wake.
    if (idle_) idle_foreground_context_ = event;
    if (!idle_ && foreground_context_needs_resync_) {
        if (!idle_foreground_context_) idle_foreground_context_ = event;
        features_.resynchronize_foreground(*idle_foreground_context_);
        foreground_context_needs_resync_ = false;
        idle_foreground_context_.reset();
    }

    PersistJob job;
    const bool have_session = active_session_.has_value();
    if (have_session) job.session_id = active_session_->session_id;

    if (have_session) {
        if (event.event_type == EventType::WindowFocusChange ||
            event.event_type == EventType::WindowTitleChange) {
            // Window changes drive the distraction state machine; eager timestamp is fine
            // here since these events are infrequent.
            job.context_snapshot = context_tracker_.observe_window_change(
                event.app_name, event.window_title, app_rules_, event.timestamp_secs, now_unix_ms());
        } else {
            // Defer the RFC3339 formatting until the tracker actually checkpoints (rare),
            // so the ~99% of key/mouse events don't pay for a timestamp they won't use.
            job.context_snapshot = context_tracker_.maybe_checkpoint_snapshot(
                app_rules_, event.timestamp_secs, [this] { return now_unix_ms(); });
        }
        // The tracker latches a snapback payload on the return-from-distraction edge;
        // drain it into the field the tick loop emits.
        if (auto snapback = context_tracker_.take_pending_snapback()) {
            if (persistence_failure_reason_ && pending_snapback_episode_) {
                // Keep the original episode/alert pair until its retry commits. Later
                // drift still classifies, but cannot replace an unsaved restore target.
                context_tracker_.dismiss_recovery(event.timestamp_secs);
            } else {
                // Record the episode. Its start is derived from the duration on the same clock as
                // `ended_at`.
                SnapbackEpisode episode;
                episode.session_id = active_session_->session_id;
                episode.summary = snapback->summary;
                episode.app_name = snapback->app_name;
                episode.file_hint = snapback->file_hint;
                episode.duration_secs = snapback->distraction_duration_secs;
                episode.ended_at_ms = now_unix_ms();
                episode.started_at_ms =
                    unix_ms_secs_ago(static_cast<std::int64_t>(snapback->distraction_duration_secs));
                job.snapback_episode = std::move(episode);
                pending_snapback_episode_ = job.snapback_episode;

                // Delivery is decided at the latch, the only place that can also acknowledge a
                // suppressed
                // snapback.
                const auto route = alert_route_unlocked(AlertEvent::Snapback);
                if (route.visible()) {
                    latest_snapback_ = *snapback;
                    latest_snapback_route_ = route;
                    snapback_emitted_ = false;  // replaces any predecessor, restored or not
                    ++snapback_generation_;
                    live_read_dirty_ = true;
                } else {
                    // A suppressed card can never be dismissed, and dismiss_recovery() is the
                    // tracker's only exit
                    // from Recovering; skipping this would disable every later snapback. The
                    // episode is still
                    // persisted (the alert was suppressed, not the recording); the stale restore
                    // target is
                    // dropped.
                    context_tracker_.dismiss_recovery(event.timestamp_secs);
                    log().info(std::string("alerts: snapback suppressed (") +
                               alert_suppression_as_str(route.suppressed_by) + ")");
                }
            }
        }
    }

    // AFK freeze: while idle we skip ingest + prediction so an empty window doesn't get
    // scored as "distracted" and idle minutes don't dilute the feature windows. Context
    // snapshots (window changes) still persist so the recovery timeline stays intact.
    if (idle_) {
        // Keep the job if it carries an episode even without a snapshot.
        if (job.context_snapshot || job.snapback_episode) return job;
        return std::nullopt;
    }

    // Always ingest (cheap bookkeeping); defer the O(window) extract() until we actually
    // produce a prediction. ~99% of events are throttled, so this skips almost all scans.
    features_.ingest(event);
    const double now = event.timestamp_secs;

    if (last_prediction_secs_ >= 0.0 && now - last_prediction_secs_ < 1.0) {
        // Throttled: no new prediction this event. Persist the context snapshot and/or the
        // episode if either was produced; otherwise there's nothing to write.
        if (job.context_snapshot || job.snapback_episode) return job;
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
                // Latched even when suppressed, so quiet hours cannot queue a nudge. Clears
                // when a break
                // resets minutes_since_last_break.
                hyperfocus_latched_ = true;
                if (alert_route_unlocked(AlertEvent::Hyperfocus).visible()) {
                    hyperfocus_minutes_ = minutes;
                }
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
    record.timestamp_ms = now_unix_ms();
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
    if (job.snapback_episode) {
        storage_.insert_snapback_episode(*job.snapback_episode);
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
