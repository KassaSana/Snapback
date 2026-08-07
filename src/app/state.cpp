#include "app/state.hpp"

#include <chrono>
#include <algorithm>
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

// Roadmap 8.12. The artifact/privacy matrix, written down in one place because it was
// previously spread across whichever call sites happened to remember.
//
// **Activity-bearing** — a copy of what the user did, and therefore in scope for "delete all
// activity":
//
//   exports/training   feature matrices derived from captured input
//   exports/summaries  aggregate reports over sessions
//   exports/personal   the readable archive; it contains window titles verbatim
//   focoflow.db.pre-v<N>.bak   a *complete* copy of the database, written before each
//                              migration (7.22) and kept indefinitely
//
// The last two were both missed. `exports/personal` is the worst of the omissions: it is the
// most legible copy of the user's history that exists, and the command reported success while
// leaving it on disk.
//
// **Configuration and diagnostics** — deliberately preserved, because erasing activity is not
// the same request as resetting the app:
//
//   settings.json (+ .bak/.tmp)   the user's own choices
//   model.onnx, model.onnx.previous, model_quality.json, training_repo.txt
//   snapback.log and its rotations
//   exports/support               diagnostics bundles
//   snapback.lock
//
// The log is the one genuinely arguable entry, and it decides the support bundle with it. The
// log records operational events, not captured content — no window titles, no keystrokes —
// though it can name a session id, which is an identifier rather than activity. It is also the
// file a user needs *most* right after using a destructive feature. A support bundle is a copy
// of that log plus health and version, and its own privacy notice states that it contains no
// database, session history, window titles, or captured input; classifying the bundle as
// activity while keeping the log it was copied from would be incoherent. Both are kept, and
// **reported as kept**, so the decision is visible rather than an omission someone has to read
// the source to find. **8.5**'s threat model owns revisiting it, exactly as it owns the SQLite
// free-page/WAL question this item defers to it.

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

    // Enumerated rather than derived from the schema version: a database that has been through
    // two upgrades has two backups, and a build that has since bumped kSchemaVersion would not
    // know to look for the older one. Anything matching the shape gets removed.
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

// The threshold lives in two headers for layering reasons — types.hpp cannot include the
// engine, and idle_detector.hpp must stay dependency-free — so the one place that owns both
// asserts they agree. Without this, changing one default silently leaves the other behind.
static_assert(kDefaultIdleThresholdSecs * 1000 == kDefaultIdleThresholdMs,
              "AppSettings' default idle threshold must match the detector's");

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
    idle_detector_.set_threshold_ms(settings_.idle_threshold_secs * 1000);
    context_tracker_.set_goal_categories(settings_.goal_categories);
    hydrate_active_session_unlocked();
    hydrate_session_attendance_unlocked();
    publish_live_read_unlocked();
}

void AppState::hydrate_active_session_unlocked() {
    // Roadmap 7.25. A restart used to hydrate the active row and stop there: the session came
    // back, but the *live state belonging to it* did not. Two consequences the user could see.
    //
    // The saved focus mode was replaced by the default. Focus mode sets the risk threshold and
    // the hyperfocus window, so a Deep session silently continued under Normal's rules — the
    // classifier answering a different question than the one the session was started to ask.
    //
    // And `seconds_since_session_start` restarted at zero while the recap beside it kept
    // reporting hours: one session, two contradictory numbers, one of them a model input.
    if (!active_session_) return;
    focus_mode_ = focus_mode_from_string(active_session_->focus_mode);
    const auto elapsed = storage_.session_elapsed_secs(active_session_->session_id);
    features_.resume_session(static_cast<double>(elapsed.value_or(0)));
}

void AppState::hydrate_session_attendance_unlocked() {
    // Roadmap 7.23. A span still open when the process starts belongs to a process that never
    // closed it — a crash, a kill, a power loss. Left alone it keeps counting to "now", so an
    // app that crashed on Friday would report the weekend as attended.
    //
    // Attendance restarts from zero rather than being inherited: the previous stretch ended
    // at whatever the database last saw, and whether the user is here *now* is a question the
    // idle detector answers on the first tick, not one startup should assume.
    session_attended_ = false;
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
    const bool now_idle = idle_detector_.state() == IdleState::Idle;

    // Roadmap 7.23 / ADR-0005. This is the action idle_detector.hpp has always documented
    // ("5 minutes of no input pauses the session", "callers act on the edges, not the level")
    // and never performed — until now the edges only emitted a UI event.
    //
    // Driven by the *level* rather than by `edge`, because attendance changes for reasons an
    // idle edge cannot express. A process that crashed mid-session reopens with its span
    // already closed by hydration and no edge on the way; an edge-driven rule would then wait
    // for the user to go idle and come back before recording another second of attended time.
    // Comparing "should there be an open span" against "is there one" covers the edges and
    // those gaps with the same two lines.
    //
    // Recorded here rather than acted on here: this runs under mutex_, which is deliberately
    // in-memory only, so engine_tick performs the write in its storage phase.
    const bool should_attend = active_session_.has_value() && !now_idle;
    if (should_attend != session_attended_) {
        if (active_session_) {
            pending_span_session_ = active_session_->session_id;
            pending_span_opens_ = should_attend;
            // An offset, not a timestamp: Storage stamps with its own clock, so handing it one
            // of ours would compare two clocks against each other. Back-dating matters because
            // we only notice a whole idle threshold late — stamping the pause at detection
            // time would credit those five minutes as attended on every single pause.
            pending_span_secs_ago_ =
                should_attend ? 0 : idle_detector_.idle_for_ms(now_ms) / 1000;
        }
        session_attended_ = should_attend;
    }

    // Roadmap 2.7 / ADR-0005. Watch for sustained work with no session running.
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
    close_open_span_on_shutdown();
}

void AppState::close_open_span_on_shutdown() noexcept {
    // Roadmap 7.23. A clean exit is the last moment we know the user was attending, so the
    // span is closed here rather than left for the next launch's crash hydration to guess at.
    // Closing it after the engine thread has joined means nothing can reopen it behind us.
    //
    // noexcept and best-effort: this runs on the shutdown path and from the destructor, where
    // an escaping exception would call std::terminate over a bookkeeping write. A span left
    // open by a failure here is exactly the case hydration already handles on next start.
    try {
        std::lock_guard state_lock(mutex_);
        std::lock_guard store_lock(storage_mutex_);
        if (!active_session_ || !session_attended_) return;
        storage_.close_session_span_now(active_session_->session_id);
        session_attended_ = false;
    } catch (...) {
        // Deliberately swallowed; see above.
    }
}

SessionRecord AppState::start_session(const std::string& goal, FocusMode mode) {
    // Mixed (in-memory + storage): take both locks in the fixed order mutex_ -> storage_mutex_.
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);

    // Roadmap 7.25. **Persist first, mutate second.** This function used to change focus mode
    // and reset the extractor, tracker, and Pomodoro *before* the storage write that can
    // throw, so a failed insert left the old database session running underneath brand-new
    // in-memory state — the app believing it was recording a session that does not exist.
    // Everything above the release() below is undone as a unit on failure; everything after it
    // is in-memory and cannot fail.
    //
    // Roadmap 7.23 / 7.20. Starting a session *replaces* a running one, and
    // `Storage::create_session` completes it atomically. Its span has to close with it: an
    // open span on a completed session keeps counting to "now" forever, so a session replaced
    // weeks ago would report more attended time than it was ever open for. That close belongs
    // inside the same savepoint — a rolled-back replacement must leave the old session both
    // running *and* still attended.
    const std::optional<std::string> replaced =
        active_session_ ? std::optional<std::string>(active_session_->session_id) : std::nullopt;

    SessionRecord created;
    {
        Storage::Savepoint savepoint(storage_, "app_start_session");
        if (replaced) storage_.close_session_span_now(*replaced);
        created = storage_.create_session(goal, mode);
        // Starting a session is by definition the user attending it, so the first span opens
        // with it — stamped by Storage's clock, the same one that stamped started_at.
        storage_.begin_session_span_now(created.session_id);
        savepoint.release();
    }

    // Committed; the session exists. Roadmap 7.25: a replaced session gets the same automatic
    // label an explicitly stopped one does. Replacement used to complete the old row silently,
    // so the only difference between "I pressed Stop" and "I started something else" was
    // whether your finished session got a verdict. It stays outside the savepoint and stays
    // best-effort for the reason the stop path does: a label that could not be written must
    // not cost you the session it describes.
    if (replaced) save_auto_session_label_unlocked(*replaced);

    active_session_ = created;
    focus_mode_ = mode;
    features_.begin_session();
    context_tracker_.reset();
    context_tracker_.set_goal_categories(settings_.goal_categories);
    pomodoro_.reset();
    session_attended_ = true;
    // Pressing Start *is* input. Without this the detector can still be Idle from before the
    // session existed, and the next tick would read "should not be attended" and immediately
    // close the span that was just opened.
    idle_detector_.on_activity(steady_now_ms());
    last_prediction_secs_ = -1.0;
    reload_app_rules_unlocked();  // pick up any rules edited while idle
    live_read_dirty_ = true;
    publish_live_read_unlocked();
    return *active_session_;
}

void AppState::stop_session() {
    std::lock_guard state_lock(mutex_);
    std::lock_guard store_lock(storage_mutex_);
    if (active_session_) {
        const std::string session_id = active_session_->session_id;
        {
            // Roadmap 7.25. Close the span before ending the session — once ended_at is set,
            // "the span that is still open" stops being a meaningful thing to hold — and do
            // both or neither, so a failure cannot leave a completed session still accruing
            // attended time.
            Storage::Savepoint savepoint(storage_, "app_stop_session");
            storage_.close_session_span_now(session_id);
            storage_.end_session(session_id);
            savepoint.release();
        }
        save_auto_session_label_unlocked(session_id);
        session_attended_ = false;
        pomodoro_.reset();
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
    SessionRecord record;
    {
        Storage::Savepoint savepoint(storage_, "app_stop_session_by_id");
        // Closes nothing when the session was already stopped, or when it predates spans —
        // both ordinary outcomes, so the result is not checked.
        storage_.close_session_span_now(session_id);
        record = storage_.stop_session(session_id);
        savepoint.release();
    }
    // Idempotent at the storage layer (7.25): a second Stop on an already-completed session
    // returns the label already written rather than appending a second, contradictory one.
    save_auto_session_label_unlocked(session_id);
    if (active_session_ && active_session_->session_id == session_id) {
        pomodoro_.reset();
        session_attended_ = false;
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
        // Its spans went with the row, so there is nothing left to close and nothing to
        // reconcile against. Leaving this true would make the next tick's level check see a
        // change that is not there.
        session_attended_ = false;
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
    auto rows = storage_.recent_predictions(limit);
    // Roadmap 10.13. `recent_predictions` returns newest-first, which was fine while the
    // summary only counted rows. The focus *duration* is measured between neighbours, so the
    // order is now load-bearing: reversed, every interval comes out negative and the longest
    // run reports zero.
    std::reverse(rows.begin(), rows.end());
    return summarize_predictions(rows);
}

std::vector<SessionSummary> AppState::session_history(std::size_t limit) {
    std::lock_guard lock(storage_mutex_);
    // Was 1 + 5N queries (recap() is five statements), all holding storage_mutex_ — which
    // the engine tick also takes to persist, so opening history could stall capture writes.
    return storage_.recent_session_summaries(limit);
}

ActivityDeletionResult AppState::delete_all_activity_data() {
    // The boundary fences off-lock persistence in engine_tick(). Incrementing the epoch
    // also invalidates any event already queued for asynchronous UI dispatch.
    std::lock_guard state_lock(mutex_);
    std::lock_guard activity_lock(activity_boundary_mutex_);
    std::lock_guard store_lock(storage_mutex_);
    activity_epoch_.fetch_add(1, std::memory_order_release);

    ActivityDeletionResult result;

    // Roadmap 8.12. **The source first, then every replica.** This used to delete the exports
    // first and throw on the first failure, so one stale file held open by another program
    // meant the database was never cleared at all — the user asked to erase their history,
    // was shown an error, and kept everything. The authoritative copy now goes first, and
    // every replica is attempted regardless of what happened to the one before it.
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
            // Absence counts as removed: an export that was never created is not a copy of
            // anything, and listing it as a failure would make an ordinary success read as a
            // partial one.
            result.deleted.emplace_back(artifact.label);
        }
    }
    for (const char* retained : kRetainedArtifacts) result.retained.emplace_back(retained);

    active_session_.reset();
    session_attended_ = false;  // every span was deleted with the rows above
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
    return result;
}

ExportTrainingResult AppState::export_training_data(
    const std::filesystem::path& out_dir, const std::optional<std::string>& session_id) {
    std::lock_guard lock(storage_mutex_);
    return storage_.export_training_csv(out_dir, session_id);
}

PersonalArchiveExport AppState::export_personal_data(const std::filesystem::path& out_dir,
                                                     std::size_t page_size) {
    // Roadmap 9.16. **The archive is streamed, not built.**
    //
    // This used to hard-cap at 200 sessions and 500 windows per session while the document
    // itself said it contained "every session". Worse, the `truncated` flag it reported came
    // from the session cap alone, so a file that dropped the 501st window of an included
    // session was reported to the UI as complete. The user was told they were holding
    // everything, twice, by two different mechanisms.
    //
    // Raising the caps is not the fix: a complete export of a long history is unbounded, and
    // materializing it under `storage_mutex_` is the stall-becomes-dropped-events path 7.12
    // exists to avoid. So rows are read a page at a time, each page under the lock, and each
    // page is written and released before the next is fetched. Peak memory is one page, not
    // one history.
    //
    // Pages are **keyset** cursors rather than OFFSET. OFFSET re-scans from the start on every
    // page and, worse, silently skips or repeats rows when the table changes underneath it —
    // the export would corrupt itself simply because the engine kept recording during it.
    //
    // Progress reporting, cancellation, and atomic temp -> final publication are deliberately
    // **not** here: they belong to 14.6, and a half-built version of them would be harder to
    // replace than none.
    if (page_size == 0) page_size = 1;

    PersonalArchive archive;
    archive.generated_at = now_rfc3339();
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
            session_cursor = Storage::SessionCursor{records.back().started_at.value_or(""),
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

            std::vector<SnapbackEpisode> episodes;
            std::size_t window_total = 0;
            {
                std::lock_guard lock(storage_mutex_);
                // Roadmap 2.15's episodes. Bounded per session by their nature — an
                // interruption is a rare event, not a per-second sample — so they are read in
                // one go rather than paged.
                episodes = storage_.list_snapback_episodes(session_id, 10000);
                window_total = storage_.count_context_snapshots(session_id);
            }
            session.episodes = episodes;

            emit(render_archive_session_header(session, ++index));
            emit(render_archive_episodes(session.episodes));
            result.episode_count += session.episodes.size();
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

    // Nothing is capped, so nothing is omitted. The fields are still computed rather than
    // assumed zero, because `truncated()` is derived from them and a reintroduced cap must
    // show up in the footer instead of being silently absorbed.
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
    // Roadmap 7.26. **Persist the candidate, then commit and publish it.** Requires mutex_.
    //
    // Every setter used to mutate `settings_` — and in some cases live behaviour — *before*
    // `save_app_settings` ran. If that write threw because the directory was read-only or the
    // disk was full, IPC reported failure and the process kept the new behaviour anyway.
    //
    // The privacy case is what gives this teeth: a failed request to turn private mode **off**
    // resumed recording while the UI said the change had failed. The user's belief about
    // whether they were being recorded and the app's behaviour disagreed, in the direction
    // that matters.
    //
    // The write goes first and takes a *copy*, so a throw here leaves both `settings_` and
    // every live field exactly as they were. Nothing below it can fail: assignment of an
    // already-constructed value, and a publish step whose only job is to copy committed
    // settings into live state.
    if (!app_data_dir_.empty()) save_app_settings(app_data_dir_, candidate);
    settings_ = std::move(candidate);
    if (publish) publish();
}

void AppState::set_focus_mode(FocusMode mode) {
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.default_focus_mode = mode;
    commit_settings_unlocked(std::move(candidate), [this, mode] { focus_mode_ = mode; });
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
    // Roadmap 7.23. Rejected before anything is mutated, so a bad value leaves both the live
    // detector and settings.json exactly as they were.
    if (seconds < kMinIdleThresholdSecs || seconds > kMaxIdleThresholdSecs) {
        throw std::runtime_error("idle threshold must be between " +
                                 std::to_string(kMinIdleThresholdSecs) + " and " +
                                 std::to_string(kMaxIdleThresholdSecs) + " seconds");
    }
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.idle_threshold_secs = seconds;
    commit_settings_unlocked(std::move(candidate), [this, seconds] {
        // Applied to the running detector, not just stored: a threshold that takes effect on
        // the next launch is not a setting, it is a note to self. After the commit, so a
        // failed save cannot leave the detector running on a value that was never written.
        idle_detector_.set_threshold_ms(seconds * 1000);
    });
}

void AppState::set_private_mode(bool enabled) {
    std::lock_guard lock(mutex_);
    AppSettings candidate = settings_;
    candidate.private_mode = enabled;
    commit_settings_unlocked(std::move(candidate), [this] {
        live_read_dirty_ = true;
        publish_live_read_unlocked();
    });
}

void AppState::set_privacy_exclusions(std::vector<std::string> exclusions) {
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

AnalyticsSummary AppState::analytics() const {
    std::lock_guard lock(storage_mutex_);
    AnalyticsSummary summary;
    // Roadmap 7.12. This used to read **every retained prediction** into a vector and fold it
    // here — under the storage lock the engine tick needs to persist, so opening the analytics
    // tab on a mature database could stall capture writes and, with a bounded ring buffer,
    // drop events. Both numbers now come back final, from one query each.
    const auto stats = const_cast<Storage&>(storage_).prediction_stats();
    summary.sample_count = stats.sample_count;
    summary.avg_focus_score = stats.avg_focus_score;
    summary.hourly = const_cast<Storage&>(storage_).hourly_focus_buckets();

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

    // Roadmap 7.12. Was a recent_sessions(200) loop calling recap() — five queries — per
    // completed session, to read one field from each. Same cap, same 70.0 bar, same
    // skip-the-running-ones rule, one query.
    summary.productive_session_streak =
        const_cast<Storage&>(storage_).productive_session_streak(200, 70.0);
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

    // Roadmap 7.12. Every prediction in the window used to be materialized here to compute
    // four numbers; the longest non-distracted run is now a gaps-and-islands query rather than
    // a C++ loop over the rows.
    const auto stats = const_cast<Storage&>(storage_).prediction_stats(cutoff);
    report.sample_count = stats.sample_count;
    report.avg_focus_score = stats.avg_focus_score;
    report.longest_focus_secs = stats.longest_focus_secs;
    if (report.sample_count > 0) {
        report.distracted_fraction =
            static_cast<double>(stats.distracted_count) / static_cast<double>(report.sample_count);
    }

    // Roadmap 7.12. Was `recent_session_summaries(500)` — three queries, but 500 fully built
    // recaps — to read a count and one duration field. The cap still applies to recency
    // *before* the window filter, which is what the loop it replaces did and what keeps the
    // answer stable on a database with more than 500 sessions inside the window.
    const auto totals = const_cast<Storage&>(storage_).session_window_totals(500, cutoff);
    report.session_count = totals.session_count;
    report.completed_session_count = totals.completed_session_count;
    report.focus_seconds = totals.focus_seconds;
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
            // Roadmap 2.15. This edge is also the only moment an episode exists as a fact, and
            // until now it was shown once and dropped. `recap()` has counted
            // `snapback_events` rows since the baseline schema and nothing ever wrote one, so
            // the Snapback count every user saw was zero.
            //
            // The payload has a duration but no start: `rfc3339_secs_ago` turns it into one on
            // the same clock as `ended_at`, so the two are comparable to each other and to the
            // session's spans.
            SnapbackEpisode episode;
            episode.session_id = active_session_->session_id;
            episode.summary = snapback->summary;
            episode.app_name = snapback->app_name;
            episode.file_hint = snapback->file_hint;
            episode.duration_secs = snapback->distraction_duration_secs;
            episode.ended_at = now_rfc3339();
            episode.started_at =
                rfc3339_secs_ago(static_cast<std::int64_t>(snapback->distraction_duration_secs));
            job.snapback_episode = std::move(episode);

            latest_snapback_ = *snapback;
            live_read_dirty_ = true;
        }
    }

    // AFK freeze: while idle we skip ingest + prediction so an empty window doesn't get
    // scored as "distracted" and idle minutes don't dilute the feature windows. Context
    // snapshots (window changes) still persist so the recovery timeline stays intact.
    if (idle_) {
        // Roadmap 2.15: `|| job.snapback_episode`. An episode is produced on the same window
        // change that usually produces a snapshot, but nothing guarantees both — the snapshot
        // is suppressed for an unnamed window — and dropping the job here would silently lose
        // the episode rather than defer it.
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
