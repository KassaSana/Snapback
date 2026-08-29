// SQLite persistence.
//
// Another "easier in C++" case: SQLite is a C library, so you call sqlite3_* directly
// through the SQLite C API. The DB filename stays focoflow.db for install compatibility.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/features.hpp"
#include "types.hpp"
#include "util/logger.hpp"

struct sqlite3;       // forward decl; real sqlite3.h included in the .cpp
struct sqlite3_stmt;  // forward decl; cached prepared statements are held as pointers

namespace snapback {

inline constexpr int kDefaultRetentionDays = 90;
inline constexpr std::size_t kVacuumMinDeletedRows = 500;

// Roadmap 7.22. The copy taken immediately before a schema migration alters the database,
// named for the version it was taken *from* so a user with two upgrades behind them can tell
// which is which. Formatted as `focoflow.db.pre-v<N>.bak`.
std::string pre_migration_backup_name(int from_version);

// The schema version this build writes and understands, stored in `PRAGMA user_version`.
//
// Bump this and append to the migration list in storage.cpp whenever the schema changes.
// Two rules that the migration runner depends on and cannot check for you:
//
//   1. **Every migration must be idempotent.** `user_version` 0 is ambiguous — it means
//      either a brand-new file or an install from before versioning existed, which already
//      has the full schema. Nothing can tell those apart after the fact, so the runner
//      replays from 0 on both and relies on each step being a no-op when its work is
//      already done (`CREATE TABLE IF NOT EXISTS`, PRAGMA-check-then-`ALTER`).
//   2. **Never edit a released migration.** Append a new one. Editing one changes what an
//      already-upgraded database was built from, which is precisely the drift versioning
//      exists to prevent.
inline constexpr int kSchemaVersion = 7;

// The two retention DELETEs, named so a test can plan the statement production actually runs.
//
// Roadmap 5.5's second half is a performance claim -- that the prune uses `idx_predictions_ts`
// rather than scanning the largest table in the database on every startup -- and a query plan
// is the only place that claim is visible: the wrapped and unwrapped forms return identical
// rows. A test that planned its own copy of the SQL would assert only that *some* indexable
// statement exists, and would keep passing if this one regressed, which is the mistake these
// constants exist to prevent.
inline constexpr const char* kPrunePredictionsSql =
    "DELETE FROM predictions WHERE timestamp < ?1";
inline constexpr const char* kPruneContextSnapshotsSql =
    "DELETE FROM context_snapshots WHERE timestamp < ?1";

struct PruneSummary {
    std::size_t predictions_deleted = 0;
    std::size_t context_snapshots_deleted = 0;
    std::size_t feature_snapshots_deleted = 0;
    [[nodiscard]] std::size_t total() const {
        return predictions_deleted + context_snapshots_deleted + feature_snapshots_deleted;
    }
};

inline bool should_vacuum_after_prune(std::size_t rows_deleted) {
    return rows_deleted >= kVacuumMinDeletedRows;
}

class Storage {
public:
    // Opens focoflow.db and initializes the schema.
    // `logger` is optional (defaults to null) so every existing call site keeps compiling
    // unchanged; pass one to route the startup prune/vacuum messages somewhere other than
    // stderr (main.cpp passes its rotating-file logger).
    static std::optional<Storage> open(const std::filesystem::path& app_data_dir,
                                       Logger* logger = nullptr);
    static std::optional<Storage> open_memory();
    ~Storage();
    Storage(Storage&&) noexcept;
    Storage& operator=(Storage&&) noexcept;
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // RAII transaction: wraps a batch of writes in one BEGIN IMMEDIATE / COMMIT so they
    // commit (and, under synchronous=NORMAL, sync) once instead of per statement. Rolls
    // back in the destructor if commit() was never called (e.g. an exception escaped).
    class Transaction {
    public:
        explicit Transaction(Storage& storage);
        ~Transaction();
        void commit();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

    private:
        sqlite3* db_;
        bool done_ = false;
    };

    // RAII savepoint: Transaction, but nestable.
    //
    // SQLite has no nested BEGIN, so a method that needs its own atomicity *and* may be
    // called from inside a caller's Transaction cannot use Transaction — it would throw
    // "cannot start a transaction within a transaction". create_session is exactly that
    // case, as the large storage fixture proves by seeding dozens of sessions inside one
    // outer transaction.
    //
    // SAVEPOINT opens a transaction when none is active and nests when one is, so the same
    // code is correct either way. Releasing the outermost savepoint commits.
    class Savepoint {
    public:
        // `name` is a SQL identifier and is interpolated, so it must be a literal from our
        // own source — never user input. Every call site passes a string literal.
        Savepoint(Storage& storage, const char* name);
        ~Savepoint();
        void release();
        Savepoint(const Savepoint&) = delete;
        Savepoint& operator=(const Savepoint&) = delete;

    private:
        sqlite3* db_;
        const char* name_;
        bool done_ = false;
    };

    // Sessions
    std::optional<SessionRecord> get_session(const std::string& session_id);
    SessionRecord create_session(const std::string& goal, FocusMode mode);
    void end_session(const std::string& session_id);

    // --- Attended time (Roadmap 7.23 / ADR-0005) ---------------------------------------
    //
    // A session's *elapsed* time is wall clock. Its *active* time is the sum of spans during
    // which the user was actually present, which is what these maintain. Idle opens no span,
    // so time spent away is never counted rather than counted and later subtracted.

    // Opens a span at `started_at`. Closes any span still open for the session first, so a
    // missed pause cannot leave two overlapping spans double-counting the same minutes.
    //
    // Refuses — returns false, changing nothing — unless the session is ACTIVE. A caller
    // deciding to open a span and writing it are two separate moments (the engine tick
    // decides under one lock and writes under another), and a stop in between would
    // otherwise leave an open span on a completed session that every attendance query
    // measures to `now` and nothing ever closes.
    bool begin_session_span(const std::string& session_id, std::int64_t started_at_ms);

    // The same, stamped from Storage's own clock.
    //
    // Callers must use these rather than passing their own "now". Storage stamps
    // `sessions.started_at` and `ended_at` itself, and AppState carries a separately
    // injectable clock (11.4) — so an AppState-supplied timestamp and a Storage-supplied one
    // can come from different clocks entirely, which makes a span's arithmetic against its
    // own session meaningless. `secs_ago` expresses a back-dated pause as an offset so it
    // stays on this clock.
    bool begin_session_span_now(const std::string& session_id);
    bool close_session_span_now(const std::string& session_id, std::int64_t secs_ago = 0);

    // Closes the session's open span at `ended_at`. Returns false when none was open, which
    // is an ordinary outcome (already paused, or a session that predates this table) rather
    // than an error. A span is never closed earlier than it started.
    bool close_session_span(const std::string& session_id, std::int64_t ended_at_ms);

    // Closes a span that a previous process left open — a crash, a kill, a power loss.
    //
    // The moment the user stopped attending is unknowable after the fact, so this closes at
    // the last time the session has *evidence* of them: the newest prediction, context
    // snapshot, or snapback event it recorded. Closing at "now" instead would credit every
    // offline hour as attended, which is the one answer that is certainly wrong. A session
    // with an open span and no recorded evidence collapses to a zero-length span rather than
    // guessing.
    //
    // `feature_snapshots` is deliberately not consulted: its `timestamp` column is monotonic
    // uptime seconds (Roadmap 7.24), not wall clock, so it cannot be compared with a span.
    //
    // Returns the timestamp it closed at, or nullopt when no span was open.
    std::optional<std::int64_t> close_dangling_session_span(const std::string& session_id);

    // Sum of closed spans, plus the open one measured to `now`. Returns nullopt when the
    // session has no spans at all — meaning "never measured", not "zero" — so callers can
    // fall back to elapsed instead of reporting a fabricated 0.
    std::optional<std::uint64_t> active_secs(const std::string& session_id,
                                             std::int64_t now_ms);

    // True if the session has a span open, i.e. the user is currently attending it.
    bool has_open_span(const std::string& session_id);

    // Wall-clock seconds from the session's `started_at` to now, or nullopt if there is no
    // such session. Roadmap 7.25 uses it to resume the feature extractor's session origin
    // after a restart; computed in SQL so it is measured against the same clock that stamped
    // `started_at`, and never negative if that clock moved backwards.
    std::optional<std::int64_t> session_elapsed_secs(const std::string& session_id);
    // Completes the session and returns the row. Idempotent if already COMPLETED.
    SessionRecord stop_session(const std::string& session_id);
    std::optional<SessionRecord> active_session();
    std::vector<SessionRecord> recent_sessions(std::size_t limit);
    SessionRecap recap(const std::string& session_id);

    // Roadmap 2.19. Attended seconds inside the local day / ISO week containing `now`.
    //
    // Summed from `session_spans` and nothing else — never session-open duration, prediction
    // rows, or a classifier score. A span is clipped to the window rather than counted whole,
    // so an evening that runs past midnight contributes its real minutes to each day instead
    // of all of them to one. Sessions recorded before spans existed have none and contribute
    // zero, which is the honest answer: their attendance was never measured.
    //
    // `now` is an RFC3339 UTC stamp; it bounds still-open spans and selects the window.
    std::uint64_t attended_secs_in_local_day(std::int64_t now_ms);
    std::uint64_t attended_secs_in_local_week(std::int64_t now_ms);
    // Roadmap 2.19 Review half. Attended seconds in [since, now], clipped per span the same
    // way the day/week helpers clip. `since` nullopt means "all retained spans" — there is no
    // earlier bound other than the data itself. Used for 30d / all / custom Review ranges,
    // where a daily or weekly *plan* does not apply.
    std::uint64_t attended_secs_since(std::int64_t now_ms,
                                      const std::optional<std::int64_t>& since_ms);

    // Roadmap 2.14. Writes (or clears) the session's optional reflection and returns the
    // updated row; nullopt when no such session exists, so a caller cannot mistake a typo'd id
    // for a successful save. Either field may be nullopt to leave that answer unrecorded —
    // clearing is how an edit removes an answer, and is distinct from never having answered
    // only in that the user chose it. Returns the row so the caller does not re-read.
    std::optional<SessionRecord> save_session_reflection(
        const std::string& session_id, const std::optional<std::string>& done,
        const std::optional<std::string>& next_step);

    // recent_sessions(limit) + recap() for each, in three queries instead of 1 + 5N.
    //
    // recap() issues five statements per session, so the loop it replaces cost 1 + 5N
    // round trips — all of them under AppState's storage mutex, which the engine tick also
    // takes to persist. Opening a history view could therefore stall capture writes, and a
    // bounded ring buffer turns a stall into dropped events. Results are identical to the
    // per-session path, which a test pins by comparing the two.
    std::vector<SessionSummary> recent_session_summaries(
        std::size_t limit, const std::optional<std::int64_t>& started_after_ms = std::nullopt);

    // --- Aggregates for the analytics and summary surfaces (Roadmap 7.12) ----------------
    //
    // These exist because `analytics()` and `summary_report()` used to read **every retained
    // prediction** into a `std::vector<PredictionRecord>` and fold it in C++ — under
    // `storage_mutex_`, on the thread answering the UI, while the engine tick needs the same
    // lock to persist. With a bounded ring buffer, a stalled persist phase means dropped
    // capture events, so an analytics tab open on a mature database costs the user data.
    //
    // Each returns final numbers in one query. The definitions below are copied from the C++
    // loops they replace rather than re-derived, and a test pins them field for field against
    // the 12,000-row fixture.

    struct PredictionStats {
        std::size_t sample_count{};
        double avg_focus_score{};
        std::size_t distracted_count{};
        // Roadmap 10.13. The **duration** of the longest unbroken focused stretch, in seconds.
        // Replaced a count of consecutive non-DISTRACTED rows shown as "Best streak": rows are
        // not time, and predictions arrive on input rather than on a clock, so two people
        // doing identical work got different values from typing cadence alone. A run breaks on
        // a distracted verdict or on a gap longer than `kFocusRunGapSecs`.
        std::uint64_t longest_focus_secs{};
    };

    // Stats over every retained prediction, or only those at/after `cutoff`.
    PredictionStats prediction_stats(const std::optional<std::int64_t>& cutoff_ms = std::nullopt);

    // Per-local-hour focus buckets, ascending by hour, omitting hours with no samples.
    //
    // The hour is **local**, matching `local_hour_from_rfc3339`, via SQLite's `localtime`
    // modifier — the same C library conversion, so DST is handled identically. Rows whose
    // timestamp will not convert are skipped, which is what the C++ loop's `hour < 0` did.
    // Roadmap 7.16 may change how a timestamp is represented; until it does, local hour is
    // what the UI has always shown and this preserves it.
    std::vector<AnalyticsHour> hourly_focus_buckets(
        const std::optional<std::int64_t>& cutoff_ms = std::nullopt);

    // How many of the most recently *completed* sessions, counting back from the newest, have
    // an average focus score at or above `min_avg_focus`. Stops at the first one that does
    // not, which is what makes it a streak rather than a count. Only the newest `limit`
    // sessions are considered; sessions still running are skipped rather than breaking it.
    //
    // Replaces a `recent_sessions(limit)` loop that called `recap()` — five queries — per
    // completed session, to read one field from each.
    std::size_t productive_session_streak(std::size_t limit, double min_avg_focus,
                                          const std::optional<std::int64_t>& started_after_ms =
                                              std::nullopt);

    // One row per local calendar day between `since_ms` and `now_ms`, ascending, omitting
    // days with no data (matching hourly_focus_buckets). The window is snapped down to the
    // local midnight of `since_ms`'s day so buckets are whole days — the one place this
    // family of queries uses calendar semantics rather than a rolling cutoff.
    //
    // Attended seconds come from session_spans clipped per day by a recursive day axis, so a
    // span crossing midnight splits exactly. Focused/deep seconds reuse prediction_stats'
    // run arithmetic (gap between consecutive qualifying rows, capped at kFocusRunGapSecs)
    // summed per day instead of MAX'd per run; a gap is attributed to the local date of the
    // *later* row, so a run crossing midnight misplaces at most kFocusRunGapSecs per
    // midnight — accepted and documented, versus spans which split exactly.
    std::vector<DailySummaryDay> daily_summary(std::int64_t now_ms, std::int64_t since_ms);

    struct SessionWindowTotals {
        std::size_t session_count{};
        std::size_t completed_session_count{};
        std::uint64_t focus_seconds{};  // summed elapsed of the completed ones
    };

    // Totals for sessions started at/after `started_after`, within the newest `limit`
    // sessions. The cap applies to recency *before* the window filter, exactly as the loop it
    // replaces did — the two orders give different answers on a database with more than
    // `limit` sessions newer than the window.
    SessionWindowTotals session_window_totals(std::size_t limit,
                                              std::int64_t started_after_ms);

    // Counts context snapshots per app across the most recent `session_limit` sessions,
    // taking at most `per_session_limit` snapshots from each (the oldest ones, matching
    // list_context_snapshots' ORDER BY timestamp ASC LIMIT). When `started_after` is set,
    // only sessions started at or after it are counted.
    //
    // The per-session cap is preserved rather than dropped because it changes the answer:
    // it is what stops one very long session from dominating the app ranking. Doing it in
    // SQL needs a window function, which is worth it — the loop this replaces materialized
    // up to session_limit x per_session_limit full rows to compute a group-by.
    std::unordered_map<std::string, std::size_t> context_app_counts(
        std::size_t session_limit, std::size_t per_session_limit,
        const std::optional<std::int64_t>& started_after_ms = std::nullopt);
    // Atomically removes every session and its collected activity while preserving
    // user configuration such as app rules.
    void delete_all_activity_data();

    // Atomically removes one session and everything collected during it. Returns false if
    // no such session exists, so a caller can tell "already gone" from "deleted" rather
    // than reporting success for a typo'd id.
    //
    // Deliberately not gated on the session being finished: a user who wants a session gone
    // may well want it gone *because* it is running. Callers own stopping it first — see
    // AppState::delete_session, which is where the live in-memory state is also cleared.
    bool delete_session(const std::string& session_id);

    // Infers and saves an automatic session label on stop.
    static FocusLabel infer_session_label(const SessionRecap& recap);
    FocusLabel save_auto_session_label(const std::string& session_id);

    // Predictions + feature snapshots (write path from the engine tick)
    void insert_prediction(const PredictionRecord& p);
    std::optional<PredictionRecord> latest_prediction();
    std::vector<PredictionRecord> recent_predictions(std::size_t limit);
    // Returns every prediction at or after `cutoff`, or all predictions when the cutoff is
    // absent. The timestamp range stays in SQL so idx_predictions_ts can serve analytics
    // windows without silently dropping older rows.
    std::vector<PredictionRecord> predictions_since(
        const std::optional<std::int64_t>& cutoff_ms = std::nullopt);
    void insert_feature_snapshot(const std::string& session_id, const FeatureVector& f);

    // Labels (one-tap feedback)
    void insert_label(const std::string& session_id, FocusLabel label,
                      const std::string& source,
                      std::optional<std::string> notes = std::nullopt);

    // App rules (allow/block overrides).
    std::vector<AppRuleRecord> list_app_rules();
    AppRuleRecord upsert_app_rule(const std::string& pattern, AppRuleKind rule_type,
                                  std::optional<std::string> note);
    void delete_app_rule(std::int64_t id);

    // --- Distraction episodes (Roadmap 2.15) --------------------------------------------
    //
    // `recap()` has counted rows in `snapback_events` since the baseline schema, and until now
    // nothing anywhere wrote one. These are the write and read paths that make that count real.

    // Records one episode. Returns false when an episode with the same session and start time
    // already exists, which is the ordinary outcome of a retry rather than an error — the
    // episode is identified by when it began, so recording it twice must not double the count
    // the user is shown.
    bool insert_snapback_episode(const SnapbackEpisode& episode);

    // A session's episodes, oldest first. Rows written before 2.15 have no start time or
    // duration and come back with empty/zero values rather than being hidden: they are real
    // interruptions that were counted, and nothing can reconstruct their detail.
    std::vector<SnapbackEpisode> list_snapback_episodes(const std::string& session_id,
                                                        std::size_t limit);

    // Context snapshots (the "where you left off" timeline).
    void save_context_snapshot(const std::string& session_id, const ContextSnapshotDto& snap);
    std::vector<ContextSnapshotDto> list_context_snapshots(const std::string& session_id,
                                                           std::size_t limit);

    // --- Keyset paging for the ownership export (Roadmap 9.16) ---------------------------
    //
    // "Export my data" claimed to contain every session and silently stopped at 200 of them,
    // and at 500 windows within each. Removing the caps by raising them is not a fix: an
    // unbounded `list_context_snapshots` would materialize the whole history in memory under
    // `storage_mutex_`, which is the stall-becomes-dropped-events path 7.12 exists to avoid.
    //
    // These page instead. A **keyset** cursor rather than OFFSET, because OFFSET re-scans from
    // the start on every page (quadratic over a long history) and, worse, silently skips or
    // repeats rows when the table changes underneath it. Ordering on a total key means a page
    // boundary is a value, not a position, so a row inserted during the export cannot shift
    // one that was already written.

    // Sessions newest-first, strictly after `after` in `(started_at DESC, session_id DESC)`
    // order. Pass nullopt for the first page. The pair is the previous page's last row.
    struct SessionCursor {
        std::int64_t started_at_ms{};
        std::string session_id;
    };
    std::vector<SessionRecord> sessions_after(const std::optional<SessionCursor>& after,
                                              std::size_t limit);

    // Context snapshots for one session, oldest-first, strictly after `after` in
    // `(timestamp ASC, id ASC)` order. `id` stays in the key: milliseconds are finer than
    // the whole seconds 7.16 inherited, but a busy tick still writes several rows inside one,
    // so time alone is not a total order and a page boundary inside a tied group would drop or
    // repeat rows.
    struct ContextCursor {
        std::int64_t timestamp_ms{};
        std::int64_t id{};
    };
    struct ContextPage {
        std::vector<ContextSnapshotDto> rows;
        ContextCursor next;  // the last row's key; meaningless when `rows` is empty
    };
    ContextPage context_snapshots_after(const std::string& session_id,
                                        const std::optional<ContextCursor>& after,
                                        std::size_t limit);

    // Total context rows for a session, so the export can state what it holds without
    // counting as it goes and without a second full pass.
    std::size_t count_context_snapshots(const std::string& session_id);

    // Export features.csv + labels.csv for the training pipeline.
    ExportTrainingResult export_training_csv(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);

    // Deletes old runtime rows on open.
    //
    // One cutoff, in UTC epoch milliseconds. It used to take two, because the tables did not
    // agree on a time format -- predictions/context_snapshots held RFC3339 TEXT while
    // feature_snapshots.timestamp held REAL epoch seconds. ADR-0007 ended that disagreement,
    // and the doubled parameter went with it.
    PruneSummary prune_runtime_data(std::int64_t cutoff_unix_ms);

    // The same prune against the retention window, in one transaction, with the cutoff
    // computed here.
    PruneSummary prune_to_retention(int retention_days = kDefaultRetentionDays);
    void vacuum();

    // Test seam: index names in the current schema, sorted. A dropped index is a silent
    // perf regression — the query still returns correct rows, just via a full scan — so
    // it needs an explicit assertion to be catchable.
    std::vector<std::string> index_names();

    // Test seam: the SQLite query plan for `sql`, one line per step. Lets a test assert an
    // index is actually *used*, not merely present.
    std::vector<std::string> query_plan(const std::string& sql);

    // The `PRAGMA user_version` this database currently carries. Equals kSchemaVersion for
    // any database this build has opened successfully.
    int schema_version();

    // Test seam: force a session's started_at. Sessions created in one test body all land in
    // the same wall-clock second — now_rfc3339() has whole-second resolution — so anything
    // asserting on `ORDER BY started_at` ordering needs a way to separate them or it flakes
    // on tied rows. See ROADMAP 7.16, which is about fixing the representation itself.
    void backdate_session_for_test(const std::string& session_id,
                                   std::int64_t started_at_ms);

    // Test seam: run ANALYZE, so the query planner has real row statistics instead of its
    // structural defaults. Snapback never runs this in production, and that is exactly why a
    // test wants it: without stats the planner cannot decide a full scan is cheaper than an
    // index, so an empty-database plan assertion cannot fail for the reason we care about.
    // With stats over a large table it can, which makes it the adversarial case for the
    // indexes 7.13 added. See ROADMAP 7.11.
    void analyze_for_test();

    // Test seam: how many statements SQLite *starts executing* while `body` runs, counted by
    // SQLite itself through `sqlite3_trace_v2` rather than by bookkeeping we could forget to
    // update. Roadmap 7.12's acceptance is "constant query count", and that is a claim no
    // correctness test can make — a per-session loop returns the same numbers as one
    // aggregate, just N times more slowly, which is precisely how the N+1 path survived being
    // called fixed. A wall-clock bound would buy flakes on a shared runner; this counts the
    // thing that actually grows.
    //
    // The counter lives in this function's frame and is handed to SQLite as the trace context,
    // so there is no new member — Storage's move operations carry only `db_` and `stmt_cache_`,
    // and a member added to one and not the other fails silently.
    std::size_t count_statements_for_test(const std::function<void()>& body);

    // Test seam: run one statement against this connection.
    //
    // Roadmap 2.19's window arithmetic is about spans at specific instants, and the production
    // path only ever creates them at "now" through idle transitions. Seeding them directly is
    // what lets a test ask about midnight, a Monday, or last week at all. Deliberately narrow:
    // it takes SQL, not data, and nothing outside tests calls it.
    void execute_for_test(const std::string& sql);

private:
    explicit Storage(sqlite3* db) : db_(db) {}

    // Applies pending migrations, backing the database up first (Roadmap 7.22).
    //
    // `db_path` is passed rather than stored because Storage's move operations carry only
    // `db_` and `stmt_cache_`; a new member would have to be added to both, and forgetting
    // one fails silently. An empty path means there is no file to back up (`open_memory`).
    void migrate(const std::filesystem::path& db_path, Logger* logger);
    // Prepare-once / reset-on-reuse cache for hot statements (per-tick inserts). Returns a
    // statement owned by stmt_cache_; wrap it in the borrowed Stmt ctor to bind + step.
    sqlite3_stmt* cached_stmt(const char* sql);
    void ensure_active_session(const std::string& session_id);
    // The same question without the throw, for callers whose honest answer to "not active"
    // is to do nothing rather than to fail.
    bool session_is_active(const std::string& session_id);
    void finalize_cache();

    sqlite3* db_ = nullptr;
    std::unordered_map<std::string, sqlite3_stmt*> stmt_cache_;
};

}  // namespace snapback
