// SQLite persistence. Rust: storage/mod.rs (rusqlite).
//
// Another "easier in C++" case: SQLite is a C library, so you call sqlite3_* directly
// instead of going through rusqlite. The DB filename stays focoflow.db for install
// compatibility, exactly as the Rust README calls out.
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
    // Rust: Storage::open(app_data_dir). Opens focoflow.db and runs migrations.
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
    // Rust: Storage::stop_session — completes the session and returns the row (the
    // frontend maps the returned record). Idempotent if already COMPLETED.
    SessionRecord stop_session(const std::string& session_id);
    std::optional<SessionRecord> active_session();
    std::vector<SessionRecord> recent_sessions(std::size_t limit);
    SessionRecap recap(const std::string& session_id);

    // Rust: Storage::infer_session_label + save_auto_session_label — written on stop.
    static FocusLabel infer_session_label(const SessionRecap& recap);
    FocusLabel save_auto_session_label(const std::string& session_id);

    // Predictions + feature snapshots (write path from the engine tick)
    void insert_prediction(const PredictionRecord& p);
    std::optional<PredictionRecord> latest_prediction();
    std::vector<PredictionRecord> recent_predictions(std::size_t limit);
    void insert_feature_snapshot(const std::string& session_id, const FeatureVector& f);

    // Labels (one-tap feedback)
    void insert_label(const std::string& session_id, FocusLabel label,
                      const std::string& source,
                      std::optional<std::string> notes = std::nullopt);

    // App rules (allow/block overrides). Rust: storage/mod.rs app-rule CRUD.
    std::vector<AppRuleRecord> list_app_rules();
    AppRuleRecord upsert_app_rule(const std::string& pattern, AppRuleKind rule_type,
                                  std::optional<std::string> note);
    void delete_app_rule(std::int64_t id);

    // Context snapshots (the "where you left off" timeline).
    void save_context_snapshot(const std::string& session_id, const ContextSnapshotDto& snap);
    std::vector<ContextSnapshotDto> list_context_snapshots(const std::string& session_id,
                                                           std::size_t limit);

    // Rust: export_training_data -> features.csv + labels.csv for the ml/ trainer.
    ExportTrainingResult export_training_csv(
        const std::filesystem::path& out_dir,
        const std::optional<std::string>& session_id = std::nullopt);

    // Rust: prune_runtime_data — deletes old runtime rows on open.
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
