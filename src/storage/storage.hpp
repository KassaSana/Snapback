// SQLite persistence.
//
// Another "easier in C++" case: SQLite is a C library, so you call sqlite3_* directly
// through the SQLite C API. The DB filename stays focoflow.db for install compatibility.
#pragma once

#include <filesystem>
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
inline constexpr int kSchemaVersion = 2;

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

    // Sessions
    std::optional<SessionRecord> get_session(const std::string& session_id);
    SessionRecord create_session(const std::string& goal, FocusMode mode);
    void end_session(const std::string& session_id);
    // Completes the session and returns the row. Idempotent if already COMPLETED.
    SessionRecord stop_session(const std::string& session_id);
    std::optional<SessionRecord> active_session();
    std::vector<SessionRecord> recent_sessions(std::size_t limit);
    SessionRecap recap(const std::string& session_id);

    // recent_sessions(limit) + recap() for each, in three queries instead of 1 + 5N.
    //
    // recap() issues five statements per session, so the loop it replaces cost 1 + 5N
    // round trips — all of them under AppState's storage mutex, which the engine tick also
    // takes to persist. Opening a history view could therefore stall capture writes, and a
    // bounded ring buffer turns a stall into dropped events. Results are identical to the
    // per-session path, which a test pins by comparing the two.
    std::vector<SessionSummary> recent_session_summaries(std::size_t limit);

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
        const std::optional<std::string>& started_after = std::nullopt);
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
        const std::optional<std::string>& cutoff = std::nullopt);
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

    // Context snapshots (the "where you left off" timeline).
    void save_context_snapshot(const std::string& session_id, const ContextSnapshotDto& snap);
    std::vector<ContextSnapshotDto> list_context_snapshots(const std::string& session_id,
                                                           std::size_t limit);

    // Export features.csv + labels.csv for the training pipeline.
    ExportTrainingResult export_training_csv(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);

    // Deletes old runtime rows on open.
    //
    // Takes the cutoff twice because the tables don't agree on a time format:
    // predictions/context_snapshots store RFC3339 TEXT, while feature_snapshots.timestamp
    // is REAL Unix epoch seconds (see insert_feature_snapshot). Passing one and deriving
    // the other would mean parsing RFC3339 by hand; the caller already has both.
    PruneSummary prune_runtime_data(const std::string& cutoff_rfc3339, double cutoff_unix_secs);
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
    void backdate_session_for_test(const std::string& session_id, const std::string& started_at);

private:
    explicit Storage(sqlite3* db) : db_(db) {}
    void migrate();
    // Prepare-once / reset-on-reuse cache for hot statements (per-tick inserts). Returns a
    // statement owned by stmt_cache_; wrap it in the borrowed Stmt ctor to bind + step.
    sqlite3_stmt* cached_stmt(const char* sql);
    void ensure_active_session(const std::string& session_id);
    void finalize_cache();

    sqlite3* db_ = nullptr;
    std::unordered_map<std::string, sqlite3_stmt*> stmt_cache_;
};

}  // namespace snapback
