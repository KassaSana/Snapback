// SQLite persistence. The DB filename stays focoflow.db for install compatibility.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "engine/features.hpp"
#include "types.hpp"
#include "util/logger.hpp"

struct sqlite3;       // forward decl; real sqlite3.h included in the .cpp
struct sqlite3_stmt;  // forward decl; cached prepared statements are held as pointers

namespace snapback {

class SqliteError : public std::runtime_error {
public:
    SqliteError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}
    int code() const noexcept { return code_; }
private:
    int code_;
};

inline constexpr int kDefaultRetentionDays = 90;
inline constexpr std::size_t kVacuumMinDeletedRows = 500;
// How long a writer waits on SQLITE_BUSY before failing, so brief contention (a backup tool
// opening the file) does not discard a drained persistence batch.
inline constexpr int kSqliteBusyTimeoutMs = 500;

// How often that timeout is hit. `waits` counts busy-handler invocations (pressure);
// `exhausted` counts the times the caller saw SQLITE_BUSY (failure).
struct SqliteBusySnapshot {
    std::uint64_t waits{};
    std::uint64_t exhausted{};
    // The longest a single statement spent inside the handler, in milliseconds. Bounded by
    // kSqliteBusyTimeoutMs by construction, so a value at the bound means "gave up".
    std::uint64_t max_wait_ms{};
};

// The pre-migration backup name, `focoflow.db.pre-v<N>.bak`, named for the version it was taken
// from.
std::string pre_migration_backup_name(int from_version);

// Schema version this build writes, stored in `PRAGMA user_version`. Bump it and append to the
// migration list in storage.cpp when the schema changes.
//
//   1. Every migration must be idempotent: user_version 0 means either a new file or a
//      pre-versioning install, so the runner replays from 0 on both.
//   2. Never edit a released migration; append a new one.
inline constexpr int kSchemaVersion = 8;

// The retention DELETEs, named so a test can query-plan exactly what production runs (the index
// use is only visible in a plan).
inline constexpr const char* kPrunePredictionsSql =
    "DELETE FROM predictions WHERE timestamp < ?1";
inline constexpr const char* kPruneContextSnapshotsSql =
    "DELETE FROM context_snapshots WHERE timestamp < ?1";
inline constexpr const char* kPruneFeatureSnapshotsSql =
    "DELETE FROM feature_snapshots WHERE timestamp < ?1";

// Bounded variants for periodic retention, so no maintenance transaction monopolizes the
// connection.
inline constexpr const char* kPrunePredictionsBatchSql =
    "DELETE FROM predictions WHERE id IN (SELECT id FROM predictions "
    "WHERE timestamp < ?1 ORDER BY timestamp, id LIMIT ?2)";
inline constexpr const char* kPruneContextSnapshotsBatchSql =
    "DELETE FROM context_snapshots WHERE id IN (SELECT id FROM context_snapshots "
    "WHERE timestamp < ?1 ORDER BY timestamp, id LIMIT ?2)";
inline constexpr const char* kPruneFeatureSnapshotsBatchSql =
    "DELETE FROM feature_snapshots WHERE id IN (SELECT id FROM feature_snapshots "
    "WHERE timestamp < ?1 ORDER BY timestamp, id LIMIT ?2)";

// Windowed reads over `predictions`, built here so a test can plan them. With a cutoff the
// predicate is a bare `timestamp >= ?N` (sargable on idx_predictions_ts); without one it is
// omitted. `(?N IS NULL OR ...)` looks tidier but is not sargable.
std::string prediction_stats_sql(bool with_cutoff);
// `?1` is the run-gap bound every caller binds and `?2` the optional cutoff, so the
// always-bound parameter keeps a fixed index and only the optional one moves.
std::string longest_focus_stretch_sql(bool with_cutoff);
std::string hourly_focus_buckets_sql(bool with_cutoff);

// The live counters behind SqliteBusySnapshot. Atomic because a read connection and the
// engine's writer can both be inside the handler at once.
struct SqliteBusyStats {
    std::atomic<std::uint64_t> waits{0};
    std::atomic<std::uint64_t> exhausted{0};
    std::atomic<std::uint64_t> max_wait_ms{0};
};

struct PruneSummary {
    std::size_t predictions_deleted = 0;
    std::size_t context_snapshots_deleted = 0;
    std::size_t feature_snapshots_deleted = 0;
    [[nodiscard]] std::size_t total() const {
        return predictions_deleted + context_snapshots_deleted + feature_snapshots_deleted;
    }
};

struct PruneBatchSummary : PruneSummary {
    bool has_more = false;
};

inline bool should_vacuum_after_prune(std::size_t rows_deleted) {
    return rows_deleted >= kVacuumMinDeletedRows;
}

class Storage {
public:
    // Opens focoflow.db and initializes the schema. `logger` defaults to stderr.
    static std::optional<Storage> open(const std::filesystem::path& app_data_dir,
                                       Logger* logger = nullptr);
    // Opens an existing database without migration, pruning, or write capability. Intended
    // for long-running exports so WAL readers never serialize the engine's writer connection.
    // Throws with SQLite's diagnostic when the connection cannot be opened.
    static Storage open_read_only(const std::filesystem::path& db_path);
    static std::optional<Storage> open_memory();
    // Empty for :memory:, otherwise the canonical main-database filename SQLite opened.
    std::filesystem::path database_path() const;
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

    // RAII savepoint: a nestable Transaction. Needed where a method must be atomic on its own
    // but may run inside a caller's Transaction (SQLite has no nested BEGIN). Releasing the
    // outermost savepoint commits.
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

    // --- Attended time (ADR-0005) ---------------------------------------------------------
    //
    // Active time is the sum of spans during which the user was present. Idle opens no span.

    // Opens a span at `started_at`, closing any open one first. Returns false (changing
    // nothing) unless the session is ACTIVE, since a stop can land between the decision and the
    // write.
    bool begin_session_span(const std::string& session_id, std::int64_t started_at_ms);

    // The same, stamped from Storage's own clock. Use these rather than passing "now": AppState
    // may use a different clock than the one that stamped the session. `secs_ago` back-dates a
    // pause on this clock.
    bool begin_session_span_now(const std::string& session_id);
    bool close_session_span_now(const std::string& session_id, std::int64_t secs_ago = 0);

    // Resolve an offset against Storage's wall clock once, so a caller that retries a failed
    // transaction can reuse the exact same boundary instead of moving it to the retry time.
    std::int64_t session_span_timestamp_now(std::int64_t millis_ago = 0) const;

    // Closes the session's open span at `ended_at`. Returns false when none was open, which
    // is an ordinary outcome (already paused, or a session that predates this table) rather
    // than an error. A span is never closed earlier than it started.
    bool close_session_span(const std::string& session_id, std::int64_t ended_at_ms);

    // Closes a span a crashed process left open, at the session's last recorded evidence
    // (newest prediction, context snapshot, or snapback event) -- never "now". No evidence
    // collapses it to zero length. feature_snapshots is not consulted: its timestamp is
    // monotonic. Returns the close time, or nullopt when none was open.
    std::optional<std::int64_t> close_dangling_session_span(const std::string& session_id);

    // Closed spans plus the open one measured to `now`. nullopt when the session has no spans
    // ("never measured", not zero).
    std::optional<std::uint64_t> active_secs(const std::string& session_id,
                                             std::int64_t now_ms);

    // True if the session has a span open, i.e. the user is currently attending it.
    bool has_open_span(const std::string& session_id);

    // Seconds from the session's `started_at` to now, or nullopt if no such session. Computed
    // in SQL against the clock that stamped it, never negative.
    std::optional<std::int64_t> session_elapsed_secs(const std::string& session_id);
    // Completes the session and returns the row. Idempotent if already COMPLETED.
    SessionRecord stop_session(const std::string& session_id);
    std::optional<SessionRecord> active_session();
    std::vector<SessionRecord> recent_sessions(std::size_t limit);
    SessionRecap recap(const std::string& session_id);

    // Attended seconds inside the local day / ISO week containing `now`, from session_spans
    // only. Spans are clipped to the window, so a stretch past midnight splits correctly.
    std::uint64_t attended_secs_in_local_day(std::int64_t now_ms);
    std::uint64_t attended_secs_in_local_week(std::int64_t now_ms);
    // Attended seconds in [since, now], clipped per span. `since` nullopt means all retained
    // spans.
    std::uint64_t attended_secs_since(std::int64_t now_ms,
                                      const std::optional<std::int64_t>& since_ms);

    // Writes (or clears) the session's reflection and returns the updated row; nullopt when no
    // such session. Either field may be nullopt.
    std::optional<SessionRecord> save_session_reflection(
        const std::string& session_id, const std::optional<std::string>& done,
        const std::optional<std::string>& next_step);

    // recent_sessions(limit) + recap() for each, in three queries instead of 1 + 5N (all under
    // the storage mutex the engine needs). A test pins it against the per-session path.
    std::vector<SessionSummary> recent_session_summaries(
        std::size_t limit, const std::optional<std::int64_t>& started_after_ms = std::nullopt);

    // --- Aggregates for the analytics and summary surfaces -------------------------------
    //
    // Each returns final numbers in one query rather than folding every row in C++ under the
    // storage mutex. A test pins them field by field against the 12,000-row fixture.

    struct PredictionStats {
        std::size_t sample_count{};
        double avg_focus_score{};
        double peak_focus_score{};
        std::size_t distracted_count{};
        // Duration of the longest unbroken focused stretch, in seconds. Breaks on a distracted
        // verdict or a gap longer than `kFocusRunGapSecs`.
        std::uint64_t longest_focus_secs{};
    };

    // Stats over every retained prediction, or only those at/after `cutoff`.
    PredictionStats prediction_stats(const std::optional<std::int64_t>& cutoff_ms = std::nullopt);

    // Per-local-hour focus buckets, ascending, omitting empty hours. Local via SQLite's
    // `localtime` (same C library conversion, so DST matches); unconvertible rows are skipped.
    std::vector<AnalyticsHour> hourly_focus_buckets(
        const std::optional<std::int64_t>& cutoff_ms = std::nullopt);

    // A session's predictions folded into at most `buckets` equal time slices, ascending,
    // omitting empty ones. Costs the session's rows only (idx_predictions_session_ts).
    std::vector<FocusCurvePoint> session_focus_curve(const std::string& session_id,
                                                     std::size_t buckets);

    // How many of the newest completed sessions, counting back, average at least
    // `min_avg_focus`; stops at the first that does not. Running sessions are skipped.
    std::size_t productive_session_streak(std::size_t limit, double min_avg_focus,
                                          const std::optional<std::int64_t>& started_after_ms =
                                              std::nullopt);

    // One row per local calendar day between `since_ms` and `now_ms`, omitting empty days. The
    // window snaps down to local midnight. Attended seconds split exactly at midnight;
    // focused/deep seconds attribute each gap to the later row's date, so a run crossing
    // midnight can misplace up to kFocusRunGapSecs.
    std::vector<DailySummaryDay> daily_summary(std::int64_t now_ms, std::int64_t since_ms);

    struct SessionWindowTotals {
        std::size_t session_count{};
        std::size_t completed_session_count{};
        std::uint64_t focus_seconds{};  // summed elapsed of the completed ones
        bool limit_reached{};  // more sessions matched the window than `limit` allowed in
    };

    // Totals for sessions started at/after `started_after`, within the newest `limit`. The cap
    // applies before the window filter.
    SessionWindowTotals session_window_totals(std::size_t limit,
                                              std::int64_t started_after_ms);

    // Context snapshots per app across the newest `session_limit` sessions, at most
    // `per_session_limit` (oldest) from each, so one long session cannot dominate.
    // `started_after` restricts to sessions started at or after it.
    std::unordered_map<std::string, std::size_t> context_app_counts(
        std::size_t session_limit, std::size_t per_session_limit,
        const std::optional<std::int64_t>& started_after_ms = std::nullopt);
    // Atomically removes every session and its collected activity while preserving
    // user configuration such as app rules.
    void delete_all_activity_data();

    // Atomically removes one session and everything recorded during it; false if none exists.
    // Not gated on the session being finished -- AppState::delete_session stops it first.
    bool delete_session(const std::string& session_id);

    // Infers and saves an automatic session label on stop.
    static FocusLabel infer_session_label(const SessionRecap& recap);
    FocusLabel save_auto_session_label(const std::string& session_id);
    std::optional<FocusLabel> session_auto_label(const std::string& session_id);

    // Predictions + feature snapshots (write path from the engine tick)
    void insert_prediction(const PredictionRecord& p);
    std::optional<PredictionRecord> latest_prediction();
    std::vector<PredictionRecord> recent_predictions(std::size_t limit);
    // Predictions at or after `cutoff` (all when absent), newest first. Used by the parity test
    // that pins prediction_stats against summarize_predictions.
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

    // --- Distraction episodes --------------------------------------------------------------

    // Records one episode. Returns false when (session, start) already exists -- the ordinary
    // outcome of a retry.
    bool insert_snapback_episode(const SnapbackEpisode& episode);

    // A session's episodes, oldest first. Legacy rows come back with empty start/duration.
    std::vector<SnapbackEpisode> list_snapback_episodes(const std::string& session_id,
                                                        std::size_t limit);
    // Longest recorded detour for a session; earliest start and row id break ties.
    std::optional<SnapbackEpisode> longest_snapback_episode(const std::string& session_id);

    struct EpisodeCursor {
        std::int64_t sort_timestamp_ms{};
        std::int64_t id{};
    };
    struct EpisodePage {
        std::vector<SnapbackEpisode> rows;
        EpisodeCursor next;  // the last row's stable sort key; meaningless when empty
    };
    EpisodePage snapback_episodes_after(
        const std::string& session_id,
        const std::optional<EpisodeCursor>& after,
        std::size_t limit);

    // Context snapshots (the "where you left off" timeline).
    void save_context_snapshot(const std::string& session_id, const ContextSnapshotDto& snap);
    std::vector<ContextSnapshotDto> list_context_snapshots(const std::string& session_id,
                                                           std::size_t limit);

    // --- Keyset paging for the personal export -----------------------------------------
    //
    // Paged so a full export never materializes the history under storage_mutex_. Keyset
    // cursors rather than OFFSET: a boundary is a value, so rows inserted during the export
    // cannot shift.

    // Sessions newest-first, strictly after `after` in `(started_at DESC, session_id DESC)`
    // order. Pass nullopt for the first page. The pair is the previous page's last row.
    struct SessionCursor {
        std::int64_t started_at_ms{};
        std::string session_id;
    };
    std::vector<SessionRecord> sessions_after(const std::optional<SessionCursor>& after,
                                              std::size_t limit);

    // Context snapshots for one session, oldest first, strictly after `after` in (timestamp,
    // id) order. `id` breaks ties within one millisecond.
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

    // Deletes runtime rows older than the cutoff (UTC epoch milliseconds).
    PruneSummary prune_runtime_data(std::int64_t cutoff_unix_ms);

    // Deletes at most `max_rows` expired rows in one transaction. A true `has_more` means
    // the batch filled its budget, so the caller should yield before requesting another.
    PruneBatchSummary prune_runtime_data_batch(std::int64_t cutoff_unix_ms,
                                               std::size_t max_rows);

    // The same prune against the retention window, in one transaction, with the cutoff
    // computed here.
    PruneSummary prune_to_retention(int retention_days = kDefaultRetentionDays);
    void vacuum();

    // Busy-wait counters for this connection, for `get_diagnostics` and the benchmarks.
    SqliteBusySnapshot busy_stats() const;

    // Test seam: index names in the current schema, sorted. A dropped index still returns
    // correct rows, so it needs an explicit assertion.
    std::vector<std::string> index_names();

    // Test seam: the SQLite query plan for `sql`, one line per step. Lets a test assert an
    // index is actually *used*, not merely present.
    std::vector<std::string> query_plan(const std::string& sql);

    // The `PRAGMA user_version` this database currently carries. Equals kSchemaVersion for
    // any database this build has opened successfully.
    int schema_version();

    // Test seam: force a session's started_at so ordering tests do not depend on ties.
    void backdate_session_for_test(const std::string& session_id,
                                   std::int64_t started_at_ms);

    // Test seam: run ANALYZE so plan assertions see real statistics (production never runs it).
    void analyze_for_test();

    // Test seam: how many statements SQLite starts while `body` runs, counted via
    // sqlite3_trace_v2. Lets a test pin "constant query count" without a timing bound.
    std::size_t count_statements_for_test(const std::function<void()>& body);

    // Test seam: run one statement, e.g. to seed spans at specific instants.
    void execute_for_test(const std::string& sql);

private:
    explicit Storage(sqlite3* db) : db_(db) {}

    // Applies pending migrations, backing the database up first. `db_path` is a parameter, not
    // a member, because Storage's moves carry only db_ and stmt_cache_. Empty means no file.
    void migrate(const std::filesystem::path& db_path, Logger* logger);
    // Prepare-once / reset-on-reuse cache for hot statements (per-tick inserts). Returns a
    // statement owned by stmt_cache_; wrap it in the borrowed Stmt ctor to bind + step.
    sqlite3_stmt* cached_stmt(const char* sql);
    // Installs the counting busy handler. Called on every writable connection, after the
    // PRAGMAs, because it replaces what `PRAGMA busy_timeout` would have set.
    void install_busy_handler();
    void ensure_active_session(const std::string& session_id);
    // The same check without the throw.
    bool session_is_active(const std::string& session_id);
    void finalize_cache();

    sqlite3* db_ = nullptr;
    std::unordered_map<std::string, sqlite3_stmt*> stmt_cache_;
    // Heap-allocated: SQLite holds a raw pointer to these counters and Storage is returned by
    // value, so the address must survive moves. Both move operations must carry it.
    std::unique_ptr<SqliteBusyStats> busy_;
};

}  // namespace snapback
