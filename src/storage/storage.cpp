#include "storage/storage.hpp"

#include "util/time.hpp"

#include <sqlite3.h>

#include <array>
#include <chrono>
#include <map>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace snapback {
namespace {

constexpr std::array<std::string_view, 31> kFeatureColumns = {
    "seconds_since_session_start",
    "hour_of_day",
    "day_of_week",
    "minutes_since_last_break",
    "keystroke_count",
    "keystroke_rate",
    "keystroke_interval_mean",
    "keystroke_interval_std",
    "keystroke_interval_trend",
    "mouse_move_count",
    "mouse_distance_pixels",
    "mouse_speed_mean",
    "mouse_speed_std",
    "mouse_acceleration_mean",
    "mouse_click_count",
    "context_switches_30s",
    "context_switches_5min",
    "time_in_current_app",
    "unique_apps_5min",
    "idle_time_30s",
    "idle_event_count_5min",
    "longest_active_stretch_5min",
    "window_title_length",
    "window_title_changed_30s",
    "is_browser",
    "is_ide",
    "is_communication",
    "is_entertainment",
    "is_productivity",
    "focus_momentum",
    "is_pseudo_productive",
};

[[noreturn]] void throw_sqlite(sqlite3* db, const char* action) {
    const char* msg = db ? sqlite3_errmsg(db) : "unknown sqlite error";
    throw std::runtime_error(std::string(action) + ": " + msg);
}

void exec(sqlite3* db, const char* sql) {
    char* raw_error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &raw_error) != SQLITE_OK) {
        std::string error = raw_error ? raw_error : sqlite3_errmsg(db);
        sqlite3_free(raw_error);
        throw std::runtime_error(error);
    }
}

class Stmt {
public:
    // Owning: prepares a one-off statement and finalizes it on destruction. Use for cold
    // paths (migrations, reads that don't repeat on the hot loop).
    Stmt(sqlite3* db, const char* sql) : db_(db), owns_(true) {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw_sqlite(db_, "prepare");
        }
    }

    // Borrowed: wraps a cached statement owned by Storage's stmt_cache_. Resets + clears
    // bindings on entry and does NOT finalize on destruction, so the parse/plan is paid
    // once and reused. Use for hot writes (per-tick inserts).
    Stmt(sqlite3* db, sqlite3_stmt* borrowed) : db_(db), stmt_(borrowed), owns_(false) {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    ~Stmt() {
        if (!stmt_) return;
        if (owns_) {
            sqlite3_finalize(stmt_);
        } else {
            // Borrowed from the cache: don't finalize (it's reused), but reset now so a
            // half-stepped SELECT doesn't keep holding a read lock until its next use.
            sqlite3_reset(stmt_);
        }
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    void bind(int index, const std::string& value) {
        if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
            throw_sqlite(db_, "bind text");
        }
    }

    void bind(int index, std::string_view value) {
        if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK) {
            throw_sqlite(db_, "bind text");
        }
    }

    void bind(int index, std::int64_t value) {
        if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
            throw_sqlite(db_, "bind int");
        }
    }

    void bind(int index, double value) {
        if (sqlite3_bind_double(stmt_, index, value) != SQLITE_OK) {
            throw_sqlite(db_, "bind double");
        }
    }

    void bind_null(int index) {
        if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
            throw_sqlite(db_, "bind null");
        }
    }

    bool step_row() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw_sqlite(db_, "step");
    }

    void step_done() {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_DONE) throw_sqlite(db_, "step");
    }

    sqlite3_stmt* get() { return stmt_; }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
    bool owns_ = true;
};

std::string column_text(sqlite3_stmt* stmt, int column) {
    const auto* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

std::optional<std::string> column_opt_text(sqlite3_stmt* stmt, int column) {
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return std::nullopt;
    return column_text(stmt, column);
}

// ADR-0007. An optional instant: NULL in the column means the fact that there is no such
// moment -- a session still running, an episode whose start nobody recorded -- and that has to
// survive the read rather than collapse into a stand-in value.
//
// There is no conversion left in this file. The columns are INTEGER epoch milliseconds and the
// DTOs carry the same, so a timestamp is read and written as itself; RFC3339 is produced at the
// edges that show one to a person. That is the decision's whole point, and it is why nothing
// here calls `rfc3339_from_unix_ms`.
std::optional<std::int64_t> column_opt_ms(sqlite3_stmt* stmt, int column) {
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int64(stmt, column);
}

// The column list every `sessions` read shares, and the count `read_session` consumes.
//
// Named rather than spelled out per query because it was spelled out six times, and 2.14 had
// to touch all six to add two columns. A query that needs more than these appends its own
// after the list and reads them from kSessionColumnCount onward, so adding a column here can
// never silently shift somebody else's index.
constexpr const char* kSessionColumns =
    "session_id, goal, status, focus_mode, started_at, ended_at, "
    "reflection_done, reflection_next_step";
constexpr int kSessionColumnCount = 8;

SessionRecord read_session(sqlite3_stmt* stmt) {
    SessionRecord r;
    r.session_id = column_text(stmt, 0);
    r.goal = column_text(stmt, 1);
    r.status = column_text(stmt, 2);
    r.focus_mode = column_text(stmt, 3);
    r.started_at_ms = column_opt_ms(stmt, 4);
    r.ended_at_ms = column_opt_ms(stmt, 5);
    r.reflection_done = column_opt_text(stmt, 6);
    r.reflection_next_step = column_opt_text(stmt, 7);
    return r;
}

AppRuleRecord read_app_rule(sqlite3_stmt* stmt) {
    AppRuleRecord r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.pattern = column_text(stmt, 1);
    // rule_type is constrained to 'allow'/'block' by the CHECK; fall back to Allow if
    // an unexpected value ever appears rather than throwing on read.
    r.rule_type = app_rule_kind_from_string(column_text(stmt, 2)).value_or(AppRuleKind::Allow);
    r.note = column_opt_text(stmt, 3);
    r.created_at_ms = sqlite3_column_int64(stmt, 4);
    r.updated_at_ms = sqlite3_column_int64(stmt, 5);
    return r;
}

ContextSnapshotDto read_context_snapshot(sqlite3_stmt* stmt) {
    ContextSnapshotDto s;
    s.app_name = column_text(stmt, 0);
    s.window_title = column_text(stmt, 1);
    s.file_hint = column_text(stmt, 2);
    s.project_hint = column_text(stmt, 3);
    s.summary = column_text(stmt, 4);
    s.timestamp_ms = sqlite3_column_int64(stmt, 5);
    return s;
}

PredictionRecord read_prediction(sqlite3_stmt* stmt) {
    PredictionRecord p;
    p.session_id = column_text(stmt, 0);
    p.focus_score = sqlite3_column_double(stmt, 1);
    p.distraction_risk = sqlite3_column_double(stmt, 2);
    p.focus_state = column_text(stmt, 3);
    p.thrash_score = sqlite3_column_double(stmt, 4);
    p.drift_score = sqlite3_column_double(stmt, 5);
    p.goal_alignment = sqlite3_column_double(stmt, 6);
    p.timestamp_ms = sqlite3_column_int64(stmt, 7);
    p.model_id = column_text(stmt, 8);
    p.state_source = column_opt_text(stmt, 9);
    return p;
}

void ensure_prediction_model_id_column(sqlite3* db) {
    Stmt columns(db, "PRAGMA table_info(predictions)");
    while (columns.step_row()) {
        if (column_text(columns.get(), 1) == "model_id") return;
    }
    exec(db, "ALTER TABLE predictions ADD COLUMN model_id TEXT NOT NULL "
             "DEFAULT 'heuristic:snapback-features-v1-31'");
}

int read_user_version(sqlite3* db) {
    Stmt version(db, "PRAGMA user_version");
    if (!version.step_row()) return 0;
    return sqlite3_column_int(version.get(), 0);
}

void write_user_version(sqlite3* db, int version) {
    // PRAGMA does not accept bound parameters, so this is built as text. `version` is an
    // int from kSchemaVersion, never user input, so there is nothing here to inject.
    exec(db, ("PRAGMA user_version = " + std::to_string(version)).c_str());
}


// ADR-0007. The one wall-clock reading, in the one representation. `unix_now_secs` returned a
// double of seconds and existed solely because `feature_snapshots.timestamp` was REAL; that
// column is INTEGER milliseconds as of schema v7, so the exception it served is gone.
std::int64_t unix_now_ms() {
    using clock = std::chrono::system_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
}

std::string make_uuid_v4() {
    std::array<unsigned char, 16> bytes{};
    std::random_device rd;
    for (auto& byte : bytes) byte = static_cast<unsigned char>(rd());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out << '-';
        out << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return out.str();
}

// ADR-0007 collapsed the matched pair this used to be. `retention_cutoff_rfc3339` and
// `retention_cutoff_unix_secs` existed only to express one instant in the two formats the
// tables disagreed on; the tables agree now, so one function answers for all three. Both are
// deleted rather than left for a caller who might reasonably assume they were still the way to
// ask -- an unused second representation is what this decision exists to prevent.
std::int64_t retention_cutoff_unix_ms(int retention_days) {
    return unix_now_ms() -
           static_cast<std::int64_t>(retention_days) * 24 * 60 * 60 * 1000;
}


std::string csv_escape(std::string_view cell) {
    const bool quote = cell.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!quote) return std::string(cell);

    std::string out;
    out.reserve(cell.size() + 2);
    out.push_back('"');
    for (char c : cell) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void write_csv_row(std::ofstream& out, const std::vector<std::string>& cells) {
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i != 0) out << ',';
        out << csv_escape(cells[i]);
    }
    out << '\n';
}

std::string double_cell(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

}  // namespace

std::optional<Storage> Storage::open(const std::filesystem::path& app_data_dir,
                                     Logger* logger) {
    // Declared outside the try so the catch blocks below can actually report the cause.
    Logger local_logger(std::cerr);
    Logger& log = logger ? *logger : local_logger;
    const auto db_path = (app_data_dir / "focoflow.db").string();
    try {
        std::filesystem::create_directories(app_data_dir);
        sqlite3* db = nullptr;
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            std::ostringstream msg;
            msg << "storage: could not open " << db_path;
            if (db) {
                msg << ": " << sqlite3_errmsg(db);
                sqlite3_close(db);
            }
            log.error(msg.str());
            return std::nullopt;
        }
        Storage storage(db);
        exec(storage.db_, "PRAGMA foreign_keys = ON;");
        // WAL + NORMAL: commits append to the write-ahead log and only fsync at
        // checkpoints, instead of an fsync per statement (synchronous=FULL default). This
        // is the single biggest win for the engine's per-tick write latency, and it's
        // still crash-safe (a power loss can lose the last few committed txns, never
        // corrupt the DB) — the right trade for local focus telemetry.
        exec(storage.db_, "PRAGMA journal_mode = WAL;");
        exec(storage.db_, "PRAGMA synchronous = NORMAL;");
        // Bigger page cache, in-memory temp tables, and memory-mapped I/O speed up the
        // read/export/recap queries and reduce checkpoint stalls. cache_size is negative =
        // kibibytes (~8 MB); mmap_size is bytes (256 MB).
        exec(storage.db_, "PRAGMA cache_size = -8000;");
        exec(storage.db_, "PRAGMA temp_store = MEMORY;");
        exec(storage.db_, "PRAGMA mmap_size = 268435456;");
        storage.migrate(db_path, &log);
        try {
            const PruneSummary summary = storage.prune_to_retention(kDefaultRetentionDays);
            if (summary.total() > 0) {
                std::ostringstream msg;
                msg << "storage: pruned " << summary.total() << " rows older than "
                    << kDefaultRetentionDays
                    << "d on open (predictions=" << summary.predictions_deleted
                    << ", context_snapshots=" << summary.context_snapshots_deleted
                    << ", feature_snapshots=" << summary.feature_snapshots_deleted << ")";
                log.info(msg.str());
                if (should_vacuum_after_prune(summary.total())) {
                    try {
                        storage.vacuum();
                    } catch (const std::exception& err) {
                        std::ostringstream vacuum_msg;
                        vacuum_msg << "storage: VACUUM after prune failed: " << err.what();
                        log.warn(vacuum_msg.str());
                    }
                }
            }
        } catch (const std::exception& err) {
            std::ostringstream msg;
            msg << "storage: startup retention prune failed: " << err.what();
            log.warn(msg.str());
        }
        return storage;
    } catch (const std::exception& err) {
        // Previously a bare `catch (...)` returning nullopt, which made a corrupt DB, a
        // permissions error, a failed migration, and a full disk indistinguishable to the
        // caller — and to the user, who just saw the app decline to start. The inner
        // prune/vacuum handlers already logged; only this outer one was blind.
        std::ostringstream msg;
        msg << "storage: failed to open " << db_path << ": " << err.what();
        log.error(msg.str());
        return std::nullopt;
    } catch (...) {
        std::ostringstream msg;
        msg << "storage: failed to open " << db_path << " (unknown error)";
        log.error(msg.str());
        return std::nullopt;
    }
}

std::optional<Storage> Storage::open_memory() {
    try {
        sqlite3* db = nullptr;
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return std::nullopt;
        }
        Storage storage(db);
        exec(storage.db_, "PRAGMA foreign_keys = ON;");
        // WAL is a no-op for :memory: (stays "memory" journal); NORMAL is harmless. Set
        // both so the in-memory and on-disk paths behave identically for tests.
        exec(storage.db_, "PRAGMA journal_mode = WAL;");
        exec(storage.db_, "PRAGMA synchronous = NORMAL;");
        exec(storage.db_, "PRAGMA cache_size = -8000;");
        exec(storage.db_, "PRAGMA temp_store = MEMORY;");
        // Empty path: an in-memory database has no file to back up, and nothing to lose.
        storage.migrate({}, nullptr);
        return storage;
    } catch (...) {
        return std::nullopt;
    }
}

Storage::Transaction::Transaction(Storage& storage) : db_(storage.db_) {
    exec(db_, "BEGIN IMMEDIATE;");
}

Storage::Transaction::~Transaction() {
    if (done_) return;
    // Best-effort rollback; a destructor must never throw, so use the raw API directly.
    char* error = nullptr;
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &error);
    if (error) sqlite3_free(error);
}

void Storage::Transaction::commit() {
    exec(db_, "COMMIT;");
    done_ = true;
}

Storage::Savepoint::Savepoint(Storage& storage, const char* name)
    : db_(storage.db_), name_(name) {
    exec(db_, ("SAVEPOINT " + std::string(name_) + ";").c_str());
}

Storage::Savepoint::~Savepoint() {
    if (done_) return;
    // Best-effort rollback; a destructor must never throw, so use the raw API directly.
    //
    // ROLLBACK TO undoes the work but leaves the savepoint on the stack — it is not a pop.
    // The RELEASE is what pops it, and omitting it would leave a savepoint holding the
    // enclosing transaction open for the rest of the process.
    char* error = nullptr;
    const std::string sql =
        "ROLLBACK TO " + std::string(name_) + "; RELEASE " + std::string(name_) + ";";
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (error) sqlite3_free(error);
}

void Storage::Savepoint::release() {
    exec(db_, ("RELEASE " + std::string(name_) + ";").c_str());
    done_ = true;
}

namespace {

// --- Schema migrations -------------------------------------------------------------------
//
// Ordered and append-only. `version` is the user_version the database carries *after* the
// step runs, so the runner applies every entry with version > the stored one. See
// kSchemaVersion in storage.hpp for the two rules these must obey — in particular that each
// one is idempotent, because user_version 0 means "fresh file" and "install from before
// versioning" indistinguishably, and both replay from 0.

void migrate_baseline_schema(sqlite3* db) {
    exec(db,
         R"sql(
            CREATE TABLE IF NOT EXISTS sessions (
                session_id TEXT PRIMARY KEY,
                goal TEXT NOT NULL,
                status TEXT NOT NULL,
                focus_mode TEXT NOT NULL DEFAULT 'normal',
                started_at TEXT NOT NULL,
                ended_at TEXT
            );

            CREATE TABLE IF NOT EXISTS predictions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                focus_score REAL NOT NULL,
                distraction_risk REAL NOT NULL,
                focus_state TEXT NOT NULL,
                thrash_score REAL NOT NULL DEFAULT 0.0,
                drift_score REAL NOT NULL DEFAULT 0.0,
                goal_alignment REAL NOT NULL DEFAULT 0.5,
                timestamp TEXT NOT NULL,
                model_id TEXT NOT NULL DEFAULT 'heuristic:snapback-features-v1-31',
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_predictions_session_ts
                ON predictions(session_id, timestamp DESC);

            -- latest_prediction()/recent_predictions() order by timestamp with no
            -- session filter. The composite index above can't serve them: SQLite can't
            -- skip a leading column, so both fell back to a full SCAN plus a temp B-tree
            -- sort over the largest table, on the UI's hot read path.
            CREATE INDEX IF NOT EXISTS idx_predictions_ts
                ON predictions(timestamp DESC);

            CREATE TABLE IF NOT EXISTS labels (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                label INTEGER NOT NULL,
                source TEXT NOT NULL DEFAULT 'manual',
                notes TEXT,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            -- Session recap/export queries filter both tables by session_id. Without these
            -- indexes, every completed session adds another full-table scan.
            CREATE INDEX IF NOT EXISTS idx_labels_session
                ON labels(session_id);

            CREATE TABLE IF NOT EXISTS snapback_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_snapback_events_session
                ON snapback_events(session_id);

            CREATE TABLE IF NOT EXISTS context_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                app_name TEXT NOT NULL,
                window_title TEXT NOT NULL,
                file_hint TEXT NOT NULL,
                project_hint TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE TABLE IF NOT EXISTS app_rules (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                pattern TEXT NOT NULL COLLATE NOCASE UNIQUE,
                rule_type TEXT NOT NULL CHECK (rule_type IN ('allow', 'block')),
                note TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS feature_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                timestamp REAL NOT NULL,
                seconds_since_session_start REAL NOT NULL,
                hour_of_day REAL NOT NULL,
                day_of_week REAL NOT NULL,
                minutes_since_last_break REAL NOT NULL,
                keystroke_count REAL NOT NULL,
                keystroke_rate REAL NOT NULL,
                keystroke_interval_mean REAL NOT NULL,
                keystroke_interval_std REAL NOT NULL,
                keystroke_interval_trend REAL NOT NULL,
                mouse_move_count REAL NOT NULL,
                mouse_distance_pixels REAL NOT NULL,
                mouse_speed_mean REAL NOT NULL,
                mouse_speed_std REAL NOT NULL,
                mouse_acceleration_mean REAL NOT NULL,
                mouse_click_count REAL NOT NULL,
                context_switches_30s REAL NOT NULL,
                context_switches_5min REAL NOT NULL,
                time_in_current_app REAL NOT NULL,
                unique_apps_5min REAL NOT NULL,
                idle_time_30s REAL NOT NULL,
                idle_event_count_5min REAL NOT NULL,
                longest_active_stretch_5min REAL NOT NULL,
                window_title_length REAL NOT NULL,
                window_title_changed_30s REAL NOT NULL,
                is_browser REAL NOT NULL,
                is_ide REAL NOT NULL,
                is_communication REAL NOT NULL,
                is_entertainment REAL NOT NULL,
                is_productivity REAL NOT NULL,
                focus_momentum REAL NOT NULL,
                is_pseudo_productive REAL NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_feature_snapshots_session_ts
                ON feature_snapshots(session_id, timestamp DESC);

            -- active_session() runs on every health check and stop_session; without this
            -- it scanned sessions and sorted. Ordered by started_at to match the query.
            CREATE INDEX IF NOT EXISTS idx_sessions_status_started
                ON sessions(status, started_at DESC);

            -- list_context_snapshots() filters by session and orders ascending; ASC here
            -- (not DESC) so the index supplies the ordering directly.
            CREATE INDEX IF NOT EXISTS idx_context_snapshots_session_ts
                ON context_snapshots(session_id, timestamp);
         )sql");
}

// Databases written before model provenance existed lack predictions.model_id. Kept as a
// separate step rather than folded into the baseline because a pre-versioning install
// already has the `predictions` table, so `CREATE TABLE IF NOT EXISTS` would skip it and
// the column would never appear.
void migrate_prediction_model_id(sqlite3* db) { ensure_prediction_model_id_column(db); }

// ADR-0004: records which guardrail decided focus_state ('risk'/'thrash'/'block'/'drift'),
// or 'model' when the classifier's own argmax stood. Nullable with no default, deliberately:
// pre-decision rows had their focus_state overwritten in place and the model's argmax was
// never stored, so nothing can backfill why a verdict disagrees with its scores. NULL means
// "written before verdicts carried provenance" and stays NULL forever.
void migrate_prediction_state_source(sqlite3* db) {
    Stmt columns(db, "PRAGMA table_info(predictions)");
    while (columns.step_row()) {
        if (column_text(columns.get(), 1) == "state_source") return;
    }
    exec(db, "ALTER TABLE predictions ADD COLUMN state_source TEXT");
}

struct Migration {
    int version;
    const char* name;
    void (*apply)(sqlite3* db);
};

// Roadmap 7.23 / ADR-0005. Active time is stored as spans rather than accumulated into a
// column on `sessions`: a running counter is mutable in-memory state that a crash loses,
// which is the same shape as the bug 7.20 fixed. Spans are durable, and they make active
// duration a query rather than something the app has to remember to keep correct.
//
// Deliberately **not backfilled**. A session that predates this table has no idle history,
// so inventing one full-width span would silently restate every historical duration as if it
// had been measured. Sessions with no spans report no active duration at all, and callers
// fall back to elapsed — an honest discontinuity instead of a fabricated number.
void migrate_session_spans(sqlite3* db) {
    exec(db,
         R"sql(
            CREATE TABLE IF NOT EXISTS session_spans (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                started_at TEXT NOT NULL,
                ended_at TEXT,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
            );
            CREATE INDEX IF NOT EXISTS idx_session_spans_session
                ON session_spans(session_id);
         )sql");
}

// Roadmap 2.15. `snapback_events` has existed since the baseline schema and `recap()` has
// always counted rows in it, but **nothing ever wrote one** — there was no production
// `INSERT INTO snapback_events` anywhere in the tree, so every user's Snapback count was zero
// and the only non-zero values ever seen came from hand-seeded test databases.
//
// Turning the write on needs more than the original two columns. A count of interruptions is
// not an answer to "what interrupted me"; the episode needs when it began, how long it lasted,
// and enough context to describe the way back — which the payload already carries and threw
// away.
//
// The UNIQUE index is the idempotence the item asks for. An episode is identified by the
// session it happened in and the moment it began, so a duplicate tick, a delivery retry, or a
// restart that re-drains the same payload lands on `INSERT OR IGNORE` and writes nothing.
//
// Columns are added rather than the table recreated: a pre-versioning install already has
// `snapback_events`, so `CREATE TABLE IF NOT EXISTS` would skip it and the columns would never
// appear — the same trap `migrate_prediction_model_id` documents.
void migrate_snapback_episodes(sqlite3* db) {
    std::vector<std::string> existing;
    {
        Stmt columns(db, "PRAGMA table_info(snapback_events)");
        while (columns.step_row()) existing.push_back(column_text(columns.get(), 1));
    }
    const auto has = [&](const char* name) {
        return std::find(existing.begin(), existing.end(), name) != existing.end();
    };
    if (!has("started_at")) exec(db, "ALTER TABLE snapback_events ADD COLUMN started_at TEXT");
    if (!has("duration_secs")) {
        exec(db, "ALTER TABLE snapback_events ADD COLUMN duration_secs INTEGER");
    }
    if (!has("app_name")) exec(db, "ALTER TABLE snapback_events ADD COLUMN app_name TEXT");
    if (!has("file_hint")) exec(db, "ALTER TABLE snapback_events ADD COLUMN file_hint TEXT");

    // Rows written before this migration have a NULL started_at. SQLite treats NULLs as
    // distinct in a UNIQUE index, so they neither collide with each other nor block the index
    // — which is the right outcome: nothing can reconstruct when those episodes began.
    exec(db,
         "CREATE UNIQUE INDEX IF NOT EXISTS idx_snapback_events_episode "
         "ON snapback_events(session_id, started_at)");
}

// Roadmap 2.14. Two nullable columns on `sessions`, deliberately not a table of their own: a
// reflection is exactly one optional pair per session, so a row per session with NULLs is the
// honest shape and a join buys nothing.
//
// Nullable with no default, and no backfill. NULL means "never answered", which is the same
// state Skip leaves behind — the product promise is that skipping costs one click and records
// nothing, so an empty string would be a different, wrongly-answered state. Sessions that
// predate this simply never had the chance to answer.
//
// These are kept off the `labels` table on purpose. A label is a training signal; this is the
// user writing to their future self, and 2.14 is explicit that it stays out of training data
// unless a later decision says otherwise. Storing them apart is what makes that enforceable
// rather than a convention the exporter has to remember.
//
// ALTER rather than CREATE: a pre-versioning install already has `sessions`, so a
// CREATE TABLE IF NOT EXISTS would skip it and the columns would never appear — the trap
// migrate_prediction_model_id documents.
void migrate_session_reflection(sqlite3* db) {
    std::vector<std::string> existing;
    {
        Stmt columns(db, "PRAGMA table_info(sessions)");
        while (columns.step_row()) existing.push_back(column_text(columns.get(), 1));
    }
    const auto has = [&](const char* name) {
        return std::find(existing.begin(), existing.end(), name) != existing.end();
    };
    if (!has("reflection_done")) {
        exec(db, "ALTER TABLE sessions ADD COLUMN reflection_done TEXT");
    }
    if (!has("reflection_next_step")) {
        exec(db, "ALTER TABLE sessions ADD COLUMN reflection_next_step TEXT");
    }
}


// ADR-0007 / Roadmap 7.16. Every point in time becomes UTC milliseconds since the epoch,
// stored as INTEGER, in one migration rather than one per table.
//
// **Why all twelve columns move together.** `close_dangling_session_span` takes MAX over a
// UNION ALL of `predictions`, `context_snapshots`, and `snapback_events`. SQLite orders
// storage classes before values -- every INTEGER sorts below every TEXT -- so a half-migrated
// schema would make that MAX silently return the unmigrated table's value every time,
// regardless of which is actually later. A migration split by table would therefore pass its
// own tests and corrupt attended time in exactly the quiet way this ADR exists to end.
//
// **Why the tables are rebuilt rather than patched.** SQLite has no ALTER COLUMN TYPE, and
// the obvious shortcut -- UPDATE the values in place and leave the column declared TEXT -- is
// worse than doing nothing: a TEXT-affinity column converts an integer back to text on the way
// in, so every subsequent write would silently re-introduce the representation this migration
// exists to remove. Affinity is the whole point of the change, so the declared type has to
// move. `Storage::migrate` turns foreign keys off around the whole upgrade and runs
// `foreign_key_check` before committing -- see the reasoning there, which this migration is
// what made necessary.
//
// **What happens to a value that does not parse.** `strftime` yields NULL for one, and the
// rule is uniform: a genuine NULL stays NULL, and an unparseable value becomes 0 -- the epoch.
// Neither dropping the row nor failing the migration is right. Dropping destroys user data
// during an upgrade, and failing turns a corrupt import into an app that will not open.
// Mapping to 0 keeps the row, and 0 is older than any retention window, so the next prune
// collects it under the ordinary policy. That is the honest end state for a row whose time is
// unknown, and it is a strict improvement: these are precisely the rows that used to outlive
// every retention pass forever, because `datetime()` returned NULL and `NULL < x` is NULL.
//
// A genuine NULL must be preserved rather than folded into 0, because NULL is load-bearing in
// three places: `sessions.ended_at` and `session_spans.ended_at` mean "still open", and
// `snapback_events.started_at` means "recorded before episodes carried a start". Mapping a bad
// value to NULL instead would resurrect a completed session as a running one.
void migrate_time_to_epoch_ms(sqlite3* db) {
    // TEXT -> epoch ms. NULL in, NULL out; unparseable in, 0 out.
    const auto ms = [](const std::string& col) {
        return "CASE WHEN " + col + " IS NULL THEN NULL ELSE COALESCE(CAST(strftime('%s', " +
               col + ") AS INTEGER) * 1000, 0) END";
    };
    // The same, for a column the schema declares NOT NULL: there is no NULL to preserve, so
    // the CASE would be dead weight.
    const auto ms_required = [](const std::string& col) {
        return "COALESCE(CAST(strftime('%s', " + col + ") AS INTEGER) * 1000, 0)";
    };

    exec(db, R"sql(
        CREATE TABLE sessions_v7 (
            session_id TEXT PRIMARY KEY,
            goal TEXT NOT NULL,
            status TEXT NOT NULL,
            focus_mode TEXT NOT NULL DEFAULT 'normal',
            started_at INTEGER NOT NULL,
            ended_at INTEGER,
            reflection_done TEXT,
            reflection_next_step TEXT
        );
    )sql");
    exec(db, ("INSERT INTO sessions_v7 (session_id, goal, status, focus_mode, started_at,"
              " ended_at, reflection_done, reflection_next_step) "
              "SELECT session_id, goal, status, focus_mode, " +
              ms_required("started_at") + ", " + ms("ended_at") +
              ", reflection_done, reflection_next_step FROM sessions")
                 .c_str());
    exec(db, "DROP TABLE sessions");
    exec(db, "ALTER TABLE sessions_v7 RENAME TO sessions");

    exec(db, R"sql(
        CREATE TABLE predictions_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            focus_score REAL NOT NULL,
            distraction_risk REAL NOT NULL,
            focus_state TEXT NOT NULL,
            thrash_score REAL NOT NULL DEFAULT 0.0,
            drift_score REAL NOT NULL DEFAULT 0.0,
            goal_alignment REAL NOT NULL DEFAULT 0.5,
            timestamp INTEGER NOT NULL,
            model_id TEXT NOT NULL DEFAULT 'heuristic:snapback-features-v1-31',
            state_source TEXT,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id)
        );
    )sql");
    exec(db, ("INSERT INTO predictions_v7 (id, session_id, focus_score, distraction_risk,"
              " focus_state, thrash_score, drift_score, goal_alignment, timestamp, model_id,"
              " state_source) "
              "SELECT id, session_id, focus_score, distraction_risk, focus_state,"
              " thrash_score, drift_score, goal_alignment, " +
              ms_required("timestamp") + ", model_id, state_source FROM predictions")
                 .c_str());
    exec(db, "DROP TABLE predictions");
    exec(db, "ALTER TABLE predictions_v7 RENAME TO predictions");

    exec(db, R"sql(
        CREATE TABLE labels_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            label INTEGER NOT NULL,
            source TEXT NOT NULL DEFAULT 'manual',
            notes TEXT,
            timestamp INTEGER NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id)
        );
    )sql");
    exec(db, ("INSERT INTO labels_v7 (id, session_id, label, source, notes, timestamp) "
              "SELECT id, session_id, label, source, notes, " +
              ms_required("timestamp") + " FROM labels")
                 .c_str());
    exec(db, "DROP TABLE labels");
    exec(db, "ALTER TABLE labels_v7 RENAME TO labels");

    exec(db, R"sql(
        CREATE TABLE snapback_events_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            summary TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            started_at INTEGER,
            duration_secs INTEGER,
            app_name TEXT,
            file_hint TEXT,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id)
        );
    )sql");
    exec(db, ("INSERT INTO snapback_events_v7 (id, session_id, summary, timestamp, started_at,"
              " duration_secs, app_name, file_hint) "
              "SELECT id, session_id, summary, " +
              ms_required("timestamp") + ", " + ms("started_at") +
              ", duration_secs, app_name, file_hint FROM snapback_events")
                 .c_str());
    exec(db, "DROP TABLE snapback_events");
    exec(db, "ALTER TABLE snapback_events_v7 RENAME TO snapback_events");

    exec(db, R"sql(
        CREATE TABLE context_snapshots_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            app_name TEXT NOT NULL,
            window_title TEXT NOT NULL,
            file_hint TEXT NOT NULL,
            project_hint TEXT NOT NULL,
            summary TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id)
        );
    )sql");
    exec(db, ("INSERT INTO context_snapshots_v7 (id, session_id, app_name, window_title,"
              " file_hint, project_hint, summary, timestamp) "
              "SELECT id, session_id, app_name, window_title, file_hint, project_hint,"
              " summary, " +
              ms_required("timestamp") + " FROM context_snapshots")
                 .c_str());
    exec(db, "DROP TABLE context_snapshots");
    exec(db, "ALTER TABLE context_snapshots_v7 RENAME TO context_snapshots");

    exec(db, R"sql(
        CREATE TABLE app_rules_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            pattern TEXT NOT NULL COLLATE NOCASE UNIQUE,
            rule_type TEXT NOT NULL CHECK (rule_type IN ('allow', 'block')),
            note TEXT,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
    )sql");
    exec(db, ("INSERT INTO app_rules_v7 (id, pattern, rule_type, note, created_at, updated_at) "
              "SELECT id, pattern, rule_type, note, " +
              ms_required("created_at") + ", " + ms_required("updated_at") + " FROM app_rules")
                 .c_str());
    exec(db, "DROP TABLE app_rules");
    exec(db, "ALTER TABLE app_rules_v7 RENAME TO app_rules");

    exec(db, R"sql(
        CREATE TABLE session_spans_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            started_at INTEGER NOT NULL,
            ended_at INTEGER,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );
    )sql");
    exec(db, ("INSERT INTO session_spans_v7 (id, session_id, started_at, ended_at) "
              "SELECT id, session_id, " +
              ms_required("started_at") + ", " + ms("ended_at") + " FROM session_spans")
                 .c_str());
    exec(db, "DROP TABLE session_spans");
    exec(db, "ALTER TABLE session_spans_v7 RENAME TO session_spans");

    // feature_snapshots is the one column that was never TEXT: REAL Unix *seconds*, which is
    // why `prune_runtime_data` had to take its cutoff twice. It converts arithmetically rather
    // than through strftime, and stops being the schema's documented exception.
    exec(db, R"sql(
        CREATE TABLE feature_snapshots_v7 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            seconds_since_session_start REAL NOT NULL,
            hour_of_day REAL NOT NULL,
            day_of_week REAL NOT NULL,
            minutes_since_last_break REAL NOT NULL,
            keystroke_count REAL NOT NULL,
            keystroke_rate REAL NOT NULL,
            keystroke_interval_mean REAL NOT NULL,
            keystroke_interval_std REAL NOT NULL,
            keystroke_interval_trend REAL NOT NULL,
            mouse_move_count REAL NOT NULL,
            mouse_distance_pixels REAL NOT NULL,
            mouse_speed_mean REAL NOT NULL,
            mouse_speed_std REAL NOT NULL,
            mouse_acceleration_mean REAL NOT NULL,
            mouse_click_count REAL NOT NULL,
            context_switches_30s REAL NOT NULL,
            context_switches_5min REAL NOT NULL,
            time_in_current_app REAL NOT NULL,
            unique_apps_5min REAL NOT NULL,
            idle_time_30s REAL NOT NULL,
            idle_event_count_5min REAL NOT NULL,
            longest_active_stretch_5min REAL NOT NULL,
            window_title_length REAL NOT NULL,
            window_title_changed_30s REAL NOT NULL,
            is_browser REAL NOT NULL,
            is_ide REAL NOT NULL,
            is_communication REAL NOT NULL,
            is_entertainment REAL NOT NULL,
            is_productivity REAL NOT NULL,
            focus_momentum REAL NOT NULL,
            is_pseudo_productive REAL NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id)
        );
    )sql");
    exec(db,
         "INSERT INTO feature_snapshots_v7 SELECT id, session_id,"
         " CAST(ROUND(timestamp * 1000) AS INTEGER),"
         " seconds_since_session_start, hour_of_day, day_of_week, minutes_since_last_break,"
         " keystroke_count, keystroke_rate, keystroke_interval_mean, keystroke_interval_std,"
         " keystroke_interval_trend, mouse_move_count, mouse_distance_pixels,"
         " mouse_speed_mean, mouse_speed_std, mouse_acceleration_mean, mouse_click_count,"
         " context_switches_30s, context_switches_5min, time_in_current_app,"
         " unique_apps_5min, idle_time_30s, idle_event_count_5min,"
         " longest_active_stretch_5min, window_title_length, window_title_changed_30s,"
         " is_browser, is_ide, is_communication, is_entertainment, is_productivity,"
         " focus_momentum, is_pseudo_productive FROM feature_snapshots");
    exec(db, "DROP TABLE feature_snapshots");
    exec(db, "ALTER TABLE feature_snapshots_v7 RENAME TO feature_snapshots");

    // Every index lived on a table that has just been dropped, so they are recreated here
    // rather than left to the baseline migration -- which does not run for an existing
    // database. Same names and same column order as migrate_baseline_schema, deliberately:
    // `index_names()` is asserted against in the suite precisely so that a dropped index
    // cannot become a silent full scan.
    exec(db, R"sql(
        CREATE INDEX IF NOT EXISTS idx_predictions_session_ts
            ON predictions(session_id, timestamp DESC);
        CREATE INDEX IF NOT EXISTS idx_predictions_ts
            ON predictions(timestamp DESC);
        CREATE INDEX IF NOT EXISTS idx_labels_session
            ON labels(session_id);
        CREATE INDEX IF NOT EXISTS idx_snapback_events_session
            ON snapback_events(session_id);
        CREATE UNIQUE INDEX IF NOT EXISTS idx_snapback_events_episode
            ON snapback_events(session_id, started_at);
        CREATE INDEX IF NOT EXISTS idx_feature_snapshots_session_ts
            ON feature_snapshots(session_id, timestamp DESC);
        CREATE INDEX IF NOT EXISTS idx_sessions_status_started
            ON sessions(status, started_at DESC);
        CREATE INDEX IF NOT EXISTS idx_context_snapshots_session_ts
            ON context_snapshots(session_id, timestamp);
        CREATE INDEX IF NOT EXISTS idx_session_spans_session
            ON session_spans(session_id);
    )sql");
}

constexpr Migration kMigrations[] = {
    {1, "baseline schema", migrate_baseline_schema},
    {2, "predictions.model_id", migrate_prediction_model_id},
    {3, "predictions.state_source", migrate_prediction_state_source},
    {4, "session_spans", migrate_session_spans},
    {5, "snapback_events episode detail", migrate_snapback_episodes},
    {6, "sessions reflection", migrate_session_reflection},
    {7, "time is epoch milliseconds", migrate_time_to_epoch_ms},
};

static_assert(std::size(kMigrations) > 0, "migration list must not be empty");
static_assert(kMigrations[std::size(kMigrations) - 1].version == kSchemaVersion,
              "kSchemaVersion must match the last migration; bump one when you add the other");

}  // namespace

std::string pre_migration_backup_name(int from_version) {
    return "focoflow.db.pre-v" + std::to_string(from_version) + ".bak";
}

namespace {

// True if this database has any user objects at all. Version 0 is ambiguous — it means both
// "brand-new file" and "install from before versioning existed" (see kSchemaVersion) — and
// backing up an empty file on every first run would be pure noise. The presence of a table
// is the one signal that distinguishes them after the fact.
bool has_user_data(sqlite3* db) {
    Stmt stmt(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
                  "AND name NOT LIKE 'sqlite_%'");
    return stmt.step_row() && sqlite3_column_int64(stmt.get(), 0) > 0;
}

// Roadmap 7.22. VACUUM INTO rather than copying the file: the database is open in WAL mode,
// so bytes on disk are not the whole story — recent commits may live in -wal and a plain copy
// of the .db alone can be a torn read. VACUUM INTO asks SQLite for a consistent single-file
// snapshot, which is exactly the guarantee a restore needs. It also cannot run inside a
// transaction, which is why this happens before migrate() opens one.
void back_up_before_migration(sqlite3* db, const std::filesystem::path& db_path, int from,
                              Logger* logger) {
    const auto backup = db_path.parent_path() / pre_migration_backup_name(from);

    std::error_code ec;
    // VACUUM INTO refuses to overwrite. Removing first also means a previous, older backup
    // does not masquerade as a fresh one if the new write fails.
    std::filesystem::remove(backup, ec);

    // Bound parameter, not string interpolation: a data directory can legitimately contain a
    // quote (a Windows user named O'Brien is enough), and hand-quoting a path into SQL is the
    // kind of thing that works until it silently does not.
    Stmt stmt(db, "VACUUM INTO ?1");
    stmt.bind(1, backup.string());
    stmt.step_done();

    if (logger) {
        logger->info("storage: backed up to " + backup.string() + " before migrating from v" +
                     std::to_string(from));
    }
}

}  // namespace

void Storage::migrate(const std::filesystem::path& db_path, Logger* logger) {
    const int from = read_user_version(db_);

    // A database stamped newer than this build understands was written by a later version
    // of Snapback. Opening it anyway is the dangerous option, not the friendly one: our
    // INSERTs omit columns that build may have added as NOT NULL, and our reads would
    // silently ignore data the user can still see in the newer build. Refuse loudly and
    // leave the file untouched — Storage::open turns this into a logged nullopt.
    if (from > kSchemaVersion) {
        throw std::runtime_error("database schema version " + std::to_string(from) +
                                 " is newer than this build understands (" +
                                 std::to_string(kSchemaVersion) +
                                 "); it was written by a later version of Snapback");
    }

    if (from == kSchemaVersion) return;  // already current; skip the DDL entirely

    // Roadmap 7.22. Back up before touching anything. The transaction below already handles a
    // migration that *fails* — it rolls back to `from`. What it cannot help with is a
    // migration that succeeds and is wrong: a bad UPDATE or a botched backfill commits, and
    // kSchemaVersion's own rule that a released migration is never edited makes that
    // permanent for everyone who upgrades. This copy is the only recovery path.
    //
    // Deliberately not fatal. Refusing to start because a backup failed would turn a
    // disk-space problem into "the app will not open", which is worse than the risk it
    // guards; the migration is still transactional either way. Log loudly and continue.
    if (!db_path.empty() && has_user_data(db_)) {
        try {
            back_up_before_migration(db_, db_path, from, logger);
        } catch (const std::exception& err) {
            if (logger) {
                logger->warn(std::string("storage: pre-migration backup failed, continuing: ") +
                             err.what());
            }
        }
    }

    // Foreign keys off for the duration, which is SQLite's documented procedure for any
    // migration that rebuilds a table (7.16 does; see `migrate_time_to_epoch_ms`).
    //
    // The reason is not squeamishness about constraints. `DROP TABLE` on a parent performs an
    // implicit `DELETE FROM` when foreign keys are on, so dropping `sessions` registers one
    // violation per child row -- and recreating the table under the same name does not clear
    // them, because the counter tracks row operations rather than the schema. `PRAGMA
    // defer_foreign_keys` only postpones that verdict to COMMIT, so it converts an immediate
    // failure into a failure at the end; it does not avoid it. This has to be set *before*
    // BEGIN, because `PRAGMA foreign_keys` is a silent no-op inside a transaction.
    //
    // What replaces the enforcement is `foreign_key_check` below, run while the transaction is
    // still open. That is strictly stronger here: it audits the whole database rather than only
    // the rows this migration happened to touch.
    struct ForeignKeyGuard {
        sqlite3* db;
        ~ForeignKeyGuard() {
            // A destructor must not throw, and this runs on the failure path too -- where the
            // transaction has already rolled back and leaving enforcement off would be the
            // worse outcome.
            sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
        }
    } foreign_key_guard{db_};
    exec(db_, "PRAGMA foreign_keys = OFF;");

    // One transaction for the whole upgrade. SQLite makes DDL transactional, so a failure
    // half-way through rolls back to the version we started at rather than leaving a
    // database that is neither the old shape nor the new one.
    Transaction tx(*this);
    for (const Migration& migration : kMigrations) {
        if (migration.version <= from) continue;
        migration.apply(db_);
    }
    write_user_version(db_, kSchemaVersion);

    // Inside the transaction on purpose: a rebuild that dropped a row's parent, or a copy that
    // lost a key, must roll the whole upgrade back rather than commit a database whose
    // references no longer resolve. Reported with the offending table named, because "foreign
    // key constraint failed" with no subject is the least actionable error SQLite produces.
    {
        Stmt check(db_, "PRAGMA foreign_key_check");
        if (check.step_row()) {
            throw std::runtime_error(
                "migration to schema v" + std::to_string(kSchemaVersion) +
                " left an unresolved foreign key in table '" + column_text(check.get(), 0) +
                "'; the database was left at v" + std::to_string(from));
        }
    }
    tx.commit();
}

int Storage::schema_version() { return read_user_version(db_); }

void Storage::finalize_cache() {
    // Cached statements must be finalized before sqlite3_close, or close returns BUSY.
    for (auto& [sql, stmt] : stmt_cache_) sqlite3_finalize(stmt);
    stmt_cache_.clear();
}

sqlite3_stmt* Storage::cached_stmt(const char* sql) {
    auto it = stmt_cache_.find(sql);
    if (it != stmt_cache_.end()) return it->second;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_sqlite(db_, "prepare");
    }
    stmt_cache_.emplace(sql, stmt);
    return stmt;
}

bool Storage::session_is_active(const std::string& session_id) {
    if (session_id.empty()) return false;
    Stmt stmt(db_, cached_stmt(
                       "SELECT COUNT(*) FROM sessions WHERE session_id = ?1 AND status = 'ACTIVE'"));
    stmt.bind(1, session_id);
    return stmt.step_row() && sqlite3_column_int64(stmt.get(), 0) > 0;
}

void Storage::ensure_active_session(const std::string& session_id) {
    if (session_id.empty()) {
        throw std::runtime_error("session_id is required");
    }
    Stmt stmt(db_, cached_stmt(
                       "SELECT COUNT(*) FROM sessions WHERE session_id = ?1 AND status = 'ACTIVE'"));
    stmt.bind(1, session_id);
    if (!stmt.step_row() || sqlite3_column_int64(stmt.get(), 0) == 0) {
        throw std::runtime_error("active session not found");
    }
}

Storage::~Storage() {
    finalize_cache();
    if (db_) sqlite3_close(db_);
}

Storage::Storage(Storage&& other) noexcept
    : db_(other.db_), stmt_cache_(std::move(other.stmt_cache_)) {
    other.db_ = nullptr;
    other.stmt_cache_.clear();
}

Storage& Storage::operator=(Storage&& other) noexcept {
    if (this != &other) {
        finalize_cache();
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        stmt_cache_ = std::move(other.stmt_cache_);
        other.db_ = nullptr;
        other.stmt_cache_.clear();
    }
    return *this;
}

std::optional<SessionRecord> Storage::get_session(const std::string& session_id) {
    const std::string sql =
        "SELECT " + std::string(kSessionColumns) + " FROM sessions WHERE session_id = ?1";
    Stmt stmt(db_, sql.c_str());
    stmt.bind(1, session_id);
    if (!stmt.step_row()) return std::nullopt;
    return read_session(stmt.get());
}

SessionRecord Storage::create_session(const std::string& goal, FocusMode mode) {
    // Roadmap 7.20. Closing the previous session and inserting the replacement are one
    // atomic step. Split across two statements, a failing insert left the user with *no*
    // active session: their old one already completed and the requested one absent. That is
    // strictly worse than either outcome the caller expects, and it is unrecoverable —
    // nothing afterwards knows the close was meant to be conditional on the insert.
    //
    // The rule this preserves is unchanged: starting a session still replaces an existing
    // one. Only the failure path differs, and only by leaving the old session running.
    //
    // Savepoint rather than Transaction because callers already wrap this: the large
    // storage fixture seeds sessions inside one outer transaction, and a nested BEGIN there
    // would throw. See Savepoint's comment in storage.hpp.
    Savepoint savepoint(*this, "create_session");

    const std::int64_t ended_at_ms = unix_now_ms();
    {
        Stmt close_active(db_,
                          "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 "
                          "WHERE status = 'ACTIVE'");
        close_active.bind(1, ended_at_ms);
        close_active.step_done();
    }

    SessionRecord r;
    r.session_id = make_uuid_v4();
    r.goal = goal;
    r.status = "ACTIVE";
    r.focus_mode = focus_mode_to_string(mode);
    r.started_at_ms = unix_now_ms();

    Stmt insert(db_,
                "INSERT INTO sessions (session_id, goal, status, focus_mode, started_at) "
                "VALUES (?1, ?2, 'ACTIVE', ?3, ?4)");
    insert.bind(1, r.session_id);
    insert.bind(2, r.goal);
    insert.bind(3, r.focus_mode);
    insert.bind(4, *r.started_at_ms);
    insert.step_done();
    savepoint.release();
    return r;
}

bool Storage::begin_session_span(const std::string& session_id, std::int64_t started_at_ms) {
    // Close-then-open in one transaction. If a pause was missed (a crash, a dropped idle
    // edge), reopening without closing would leave two overlapping spans and double-count
    // the overlap forever — and no later code could tell which of the two was wrong.
    Savepoint savepoint(*this, "begin_session_span");
    // Checked before the close, not only in the INSERT below, because the close is not
    // harmless on a session that is not ACTIVE: it would stamp a dangling span with *this*
    // call's timestamp, crediting every hour since the process died as attended. That is
    // the exact answer close_dangling_session_span() exists to avoid giving.
    if (!session_is_active(session_id)) return false;

    close_session_span(session_id, started_at_ms);

    // The INSERT re-states the guard as a SELECT so it also holds for a caller that read
    // the status a moment ago and raced a stop. An open span on a COMPLETED session is
    // unbounded damage rather than one wrong row — every attendance query measures an open
    // span as COALESCE(ended_at, now), so it grows forever, and nothing ever closes it:
    // shutdown and hydration only touch the *active* session.
    Stmt insert(db_,
                "INSERT INTO session_spans (session_id, started_at) "
                "SELECT ?1, ?2 FROM sessions WHERE session_id = ?1 AND status = 'ACTIVE'");
    insert.bind(1, session_id);
    insert.bind(2, started_at_ms);
    insert.step_done();
    const bool inserted = sqlite3_changes(db_) > 0;
    savepoint.release();
    return inserted;
}

bool Storage::begin_session_span_now(const std::string& session_id) {
    return begin_session_span(session_id, unix_now_ms());
}

bool Storage::close_session_span_now(const std::string& session_id, std::int64_t secs_ago) {
    // Off the same clock the rest of this file stamps with. This used to round-trip through
    // SQL and RFC3339 text purely to express "now minus N seconds" in the stored format;
    // milliseconds are arithmetic, so the detour is gone along with the whole-second rounding
    // it silently imposed on a value 7.23 cares about to the second.
    return close_session_span(
        session_id, unix_now_ms() - std::max<std::int64_t>(0, secs_ago) * 1000);
}

bool Storage::close_session_span(const std::string& session_id, std::int64_t ended_at_ms) {
    // MAX(started_at, ?1): a clock that jumped backwards (DST, NTP) must not produce a span
    // that ends before it began, because SUM over such a span would subtract time.
    Stmt update(db_,
                "UPDATE session_spans SET ended_at = MAX(started_at, ?1) "
                "WHERE session_id = ?2 AND ended_at IS NULL");
    update.bind(1, ended_at_ms);
    update.bind(2, session_id);
    update.step_done();
    return sqlite3_changes(db_) > 0;
}

std::optional<std::int64_t> Storage::close_dangling_session_span(const std::string& session_id) {
    // The last wall-clock evidence the user was present during this session. MAX over a
    // UNION ALL of per-table maxima rather than a UNION of rows: each subquery is answered by
    // that table's session index, so this stays three index probes no matter how many rows
    // the session recorded.
    Stmt evidence(db_,
                  "SELECT MAX(ts) FROM ("
                  "  SELECT MAX(timestamp) AS ts FROM predictions WHERE session_id = ?1"
                  "  UNION ALL SELECT MAX(timestamp) FROM context_snapshots WHERE session_id = ?1"
                  "  UNION ALL SELECT MAX(timestamp) FROM snapback_events WHERE session_id = ?1)");
    evidence.bind(1, session_id);
    const auto last_seen = evidence.step_row() ? column_opt_ms(evidence.get(), 0)
                                              : std::optional<std::int64_t>{};

    Savepoint savepoint(*this, "close_dangling_session_span");
    // Read the span's own start before closing, so the caller can be told what it settled on
    // even when there was no evidence at all and MAX() falls back to the start.
    Stmt open_span(db_,
                   "SELECT started_at FROM session_spans "
                   "WHERE session_id = ?1 AND ended_at IS NULL ORDER BY id DESC LIMIT 1");
    open_span.bind(1, session_id);
    if (!open_span.step_row()) return std::nullopt;
    const std::int64_t started_at_ms = sqlite3_column_int64(open_span.get(), 0);

    // Integers compare as instants. This used to lean on RFC3339 sorting lexicographically,
    // which is true only for a fixed-width UTC format and quietly false for anything else.
    const std::int64_t ended_at_ms =
        last_seen && *last_seen > started_at_ms ? *last_seen : started_at_ms;
    close_session_span(session_id, ended_at_ms);
    savepoint.release();
    return ended_at_ms;
}

std::optional<std::uint64_t> Storage::active_secs(const std::string& session_id,
                                                  std::int64_t now_ms) {
    // ROUND per span, not a truncating CAST over the sum. julianday returns a double, so a
    // 30-minute span computes as 1799.9999... and CAST truncates it to 1799; two of them lost
    // a second and turned an exact hour into 3599. Each span is a measurement in whole
    // seconds, so each is rounded before being summed.
    Stmt stmt(db_,
              "SELECT COUNT(*), CAST(COALESCE(SUM(ROUND(MAX(0, "
              "  (COALESCE(ended_at, MAX(started_at, ?2)) - started_at) / 1000.0"
              "  ))), 0) AS INTEGER) "
              "FROM session_spans WHERE session_id = ?1");
    stmt.bind(1, session_id);
    stmt.bind(2, now_ms);
    if (!stmt.step_row()) return std::nullopt;
    if (sqlite3_column_int64(stmt.get(), 0) == 0) return std::nullopt;  // never measured
    return static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 1));
}

std::optional<std::int64_t> Storage::session_elapsed_secs(const std::string& session_id) {
    Stmt stmt(db_,
              "SELECT CAST(ROUND(MAX(0, ((strftime('%s','now') * 1000) - started_at) / 1000.0)) "
              "AS INTEGER) FROM sessions WHERE session_id = ?1");
    stmt.bind(1, session_id);
    if (!stmt.step_row()) return std::nullopt;
    return sqlite3_column_int64(stmt.get(), 0);
}

bool Storage::has_open_span(const std::string& session_id) {
    Stmt stmt(db_,
              "SELECT 1 FROM session_spans WHERE session_id = ?1 AND ended_at IS NULL LIMIT 1");
    stmt.bind(1, session_id);
    return stmt.step_row();
}

void Storage::end_session(const std::string& session_id) {
    const std::int64_t ended_at_ms = unix_now_ms();
    Stmt update(db_,
                "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 "
                "WHERE session_id = ?2 AND status = 'ACTIVE'");
    update.bind(1, ended_at_ms);
    update.bind(2, session_id);
    update.step_done();
    if (sqlite3_changes(db_) == 0) {
        Stmt exists(db_, "SELECT COUNT(*) FROM sessions WHERE session_id = ?1");
        exists.bind(1, session_id);
        if (!exists.step_row() || sqlite3_column_int64(exists.get(), 0) == 0) {
            throw std::runtime_error("session not found");
        }
    }
}

SessionRecord Storage::stop_session(const std::string& session_id) {
    // Idempotent: stopping an already-completed session just returns it, so a double-click
    // on "stop" in the UI doesn't error.
    if (auto existing = get_session(session_id); existing && existing->status == "COMPLETED") {
        return *existing;
    }

    const std::int64_t ended_at_ms = unix_now_ms();
    Stmt update(db_,
                "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 "
                "WHERE session_id = ?2 AND status = 'ACTIVE'");
    update.bind(1, ended_at_ms);
    update.bind(2, session_id);
    update.step_done();
    if (sqlite3_changes(db_) == 0) {
        throw std::runtime_error("session not found");
    }
    return *get_session(session_id);
}

std::optional<SessionRecord> Storage::active_session() {
    const std::string sql = "SELECT " + std::string(kSessionColumns) +
                            " FROM sessions WHERE status = 'ACTIVE' "
                            "ORDER BY started_at DESC, session_id DESC LIMIT 1";
    Stmt stmt(db_, sql.c_str());
    if (!stmt.step_row()) return std::nullopt;
    return read_session(stmt.get());
}

std::vector<SessionRecord> Storage::recent_sessions(std::size_t limit) {
    const std::string sql = "SELECT " + std::string(kSessionColumns) +
                            " FROM sessions ORDER BY started_at DESC, session_id DESC LIMIT ?1";
    Stmt stmt(db_, sql.c_str());
    stmt.bind(1, static_cast<std::int64_t>(limit));
    std::vector<SessionRecord> rows;
    while (stmt.step_row()) rows.push_back(read_session(stmt.get()));
    return rows;
}

namespace {

// Roadmap 2.19. Attended seconds inside one local-time window, clipped per span.
//
// ADR-0007 simplified this rather than merely retyping it. Every value used to be converted to
// local time *before* being compared -- `julianday(MIN(datetime(COALESCE(ended_at, ?1),
// 'localtime'), :window_end))` -- because the columns were text and a text comparison is only
// meaningful between two strings on the same clock. Integers are absolute instants, so the
// comparison is now direct and local time survives in exactly one place: computing where the
// window's calendar boundaries fall. That is the ADR's rule stated as code -- local time is
// applied when a value is bucketed for a report, never when it is filtered or compared.
//
// DST still needs no special case, for the same reason as before: the boundary expressions
// resolve 'localtime' per instant, so an hour that repeats or never happens is handled by the
// conversion rather than by arithmetic on a fixed offset.
//
// ROUND before CAST for the reason active_secs documents: a truncating CAST loses a second per
// span. The sum is in milliseconds and is divided once at the end, so the rounding happens on
// the total rather than per span.
constexpr const char* kAttendedInWindowSql =
    "SELECT CAST(ROUND(COALESCE(SUM(MAX(0,"
    "  MIN(COALESCE(ended_at, ?1), :window_end)"
    "  - MAX(started_at, :window_start)"
    ")), 0) / 1000.0) AS INTEGER) FROM session_spans";

// A local calendar boundary, as epoch milliseconds. `unixepoch` reads the bound instant,
// `localtime` moves it onto the user's clock, the caller's modifiers land on the boundary, and
// `utc` converts the result back to an absolute instant -- without that last step the value
// would be a local wall-clock reading compared against UTC instants, which is the off-by-one-
// timezone bug this whole conversion exists to avoid.
std::string local_boundary_ms(const std::string& modifiers) {
    return "(strftime('%s', ?1 / 1000.0, 'unixepoch', 'localtime'" + modifiers +
           ", 'utc') * 1000)";
}

std::uint64_t attended_in_window(sqlite3* db, std::int64_t now_ms,
                                 const std::string& window_start_expr,
                                 const std::string& window_end_expr) {
    std::string sql = kAttendedInWindowSql;
    const auto replace_all = [&sql](const std::string& token, const std::string& value) {
        for (auto at = sql.find(token); at != std::string::npos; at = sql.find(token, at)) {
            sql.replace(at, token.size(), value);
            at += value.size();
        }
    };
    replace_all(":window_end", window_end_expr);
    replace_all(":window_start", window_start_expr);

    Stmt stmt(db, sql.c_str());
    stmt.bind(1, now_ms);
    if (!stmt.step_row()) return 0;
    const auto secs = sqlite3_column_int64(stmt.get(), 0);
    return secs > 0 ? static_cast<std::uint64_t>(secs) : 0;
}

}  // namespace

std::uint64_t Storage::attended_secs_in_local_day(std::int64_t now_ms) {
    return attended_in_window(db_, now_ms, local_boundary_ms(", 'start of day'"),
                              local_boundary_ms(", 'start of day', '+1 day'"));
}

std::uint64_t Storage::attended_secs_in_local_week(std::int64_t now_ms) {
    // ISO weeks: Monday to Sunday. `weekday 1` moves *forward* to the next Monday, so stepping
    // back six days first lands on the Monday on or before today -- including when today is
    // itself Monday, which is the case a naive `weekday 1` gets wrong by a whole week.
    return attended_in_window(
        db_, now_ms,
        local_boundary_ms(", 'start of day', '-6 days', 'weekday 1'"),
        local_boundary_ms(", 'start of day', '-6 days', 'weekday 1', '+7 days'"));
}

std::uint64_t Storage::attended_secs_since(std::int64_t now_ms,
                                           const std::optional<std::int64_t>& since_ms) {
    // Roadmap 2.19. Same clip arithmetic as the day/week helpers, but the lower bound is an
    // instant from the Review range rather than a calendar expression. Open spans use `now` as
    // their end, so a still-running session does not invent future attendance.
    //
    // Neither bound goes through `local_boundary_ms`: both are absolute instants already, and
    // this is the case that shows why the old code's blanket local conversion was the wrong
    // shape. "Since this moment" has no calendar boundary to find.
    if (!since_ms) {
        // 0 is the epoch, which is before any span this app could have recorded.
        return attended_in_window(db_, now_ms, "0", "?1");
    }
    // ?2 is the lower bound; the shared SQL template only knows ?1 (now) as a bind, so this
    // path substitutes the since-expression and binds the second argument itself.
    std::string sql = kAttendedInWindowSql;
    const auto replace_all = [&sql](const std::string& token, const std::string& value) {
        for (auto at = sql.find(token); at != std::string::npos; at = sql.find(token, at)) {
            sql.replace(at, token.size(), value);
            at += value.size();
        }
    };
    replace_all(":window_end", "?1");
    replace_all(":window_start", "?2");
    Stmt stmt(db_, sql.c_str());
    stmt.bind(1, now_ms);
    stmt.bind(2, *since_ms);
    if (!stmt.step_row()) return 0;
    const auto secs = sqlite3_column_int64(stmt.get(), 0);
    return secs > 0 ? static_cast<std::uint64_t>(secs) : 0;
}

std::optional<SessionRecord> Storage::save_session_reflection(
    const std::string& session_id, const std::optional<std::string>& done,
    const std::optional<std::string>& next_step) {
    // Roadmap 2.14. One UPDATE, both columns, every time — including the nullopt case, which
    // writes NULL. A "only update what was provided" variant would make clearing an answer
    // impossible to express, and the edit path in 2.9 needs exactly that.
    //
    // No status check. A reflection is offered at the end of a session but the item calls it
    // editable afterwards, so refusing to write one to a COMPLETED session would forbid the
    // ordinary case rather than a wrong one.
    {
        Stmt stmt(db_,
                  "UPDATE sessions SET reflection_done = ?1, reflection_next_step = ?2 "
                  "WHERE session_id = ?3");
        if (done) {
            stmt.bind(1, *done);
        } else {
            stmt.bind_null(1);
        }
        if (next_step) {
            stmt.bind(2, *next_step);
        } else {
            stmt.bind_null(2);
        }
        stmt.bind(3, session_id);
        stmt.step_done();
    }
    // Re-read rather than trusting sqlite3_changes(): an UPDATE that sets a column to the value
    // it already held reports zero rows changed, which would make a genuine no-op save
    // indistinguishable from a missing session.
    return get_session(session_id);
}

std::vector<SessionSummary> Storage::recent_session_summaries(
    std::size_t limit, const std::optional<std::int64_t>& started_after_ms) {
    const std::int64_t now_ms = unix_now_ms();
    // All three queries below re-derive "the most recent `limit` sessions" independently,
    // so they must agree on *which* sessions those are. `started_at` is milliseconds since
    // ADR-0007, which makes ties rarer than the whole seconds this comment used to describe
    // but not impossible, and SQLite does not promise that two differently-shaped queries
    // break a tie the same way. If they disagreed, a session present in the result would miss
    // its aggregates and silently
    // report zeros. `session_id` is the primary key, so adding it to the ORDER BY gives a
    // total order and removes the ambiguity.
    //
    // It is a determinism tiebreak, not a chronological one: session_id is a random UUIDv4,
    // so this makes the order *stable*, not *correct*. Recovering true sub-second order is
    // 7.16's job.

    // Query 1: the sessions themselves, plus the duration recap() computes separately.
    // Ordering and LIMIT match recent_sessions() exactly so the two stay interchangeable.
    std::vector<SessionSummary> out;
    std::unordered_map<std::string, std::size_t> index_of;
    {
        // The computed duration is appended *after* the shared column list and read at
        // kSessionColumnCount, so growing that list cannot silently move it.
        const std::string sql =
            "SELECT " + std::string(kSessionColumns) + ", " +
            "CAST(ROUND(MAX(0, (COALESCE(ended_at, (strftime('%s','now') * 1000)) - started_at)"
            " / 1000.0)) AS INTEGER) "
            "FROM sessions "
            "WHERE (?2 IS NULL OR (started_at IS NOT NULL AND started_at >= ?2)) "
            "ORDER BY started_at DESC, session_id DESC LIMIT ?1";
        Stmt stmt(db_, sql.c_str());
        stmt.bind(1, static_cast<std::int64_t>(limit));
        if (started_after_ms) {
            stmt.bind(2, *started_after_ms);
        } else {
            stmt.bind_null(2);
        }
        while (stmt.step_row()) {
            SessionSummary summary;
            summary.record = read_session(stmt.get());
            summary.recap.session_id = summary.record.session_id;
            summary.recap.goal = summary.record.goal;
            summary.recap.duration_secs =
                static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), kSessionColumnCount));
            index_of.emplace(summary.record.session_id, out.size());
            out.push_back(std::move(summary));
        }
    }
    if (out.empty()) return out;

    // Query 2: every prediction aggregate recap() computes, grouped in one pass. The
    // expressions are copied verbatim from recap() — including the deliberate absolute 0.7
    // distraction bar, which is a product decision documented there and in ADR-0004, not a
    // threshold to unify here.
    {
        Stmt stmt(db_,
                  "WITH recent AS (SELECT session_id FROM sessions "
                  "                WHERE (?2 IS NULL OR (started_at IS NOT NULL AND started_at >= ?2)) "
                  "                ORDER BY started_at DESC, session_id DESC LIMIT ?1) "
                  "SELECT p.session_id, "
                  "COALESCE(AVG(p.focus_score), 0), "
                  "COALESCE(AVG(p.distraction_risk), 0), "
                  "COALESCE(100.0 * SUM(CASE WHEN p.focus_state = 'DEEP_FOCUS' THEN 1 ELSE 0 END) / "
                  "NULLIF(COUNT(*), 0), 0), "
                  "SUM(CASE WHEN p.distraction_risk >= 0.7 THEN 1 ELSE 0 END) "
                  "FROM predictions p JOIN recent r ON p.session_id = r.session_id "
                  "GROUP BY p.session_id");
        stmt.bind(1, static_cast<std::int64_t>(limit));
        if (started_after_ms) {
            stmt.bind(2, *started_after_ms);
        } else {
            stmt.bind_null(2);
        }
        while (stmt.step_row()) {
            const auto it = index_of.find(column_text(stmt.get(), 0));
            if (it == index_of.end()) continue;
            SessionRecap& recap = out[it->second].recap;
            recap.avg_focus_score = sqlite3_column_double(stmt.get(), 1);
            recap.avg_distraction_risk = sqlite3_column_double(stmt.get(), 2);
            recap.deep_focus_pct = sqlite3_column_double(stmt.get(), 3);
            recap.thrash_spikes = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 4));
        }
    }

    // Query 3: snapback counts. Sessions with no events simply do not appear, which leaves
    // the zero-initialized default in place — the same answer COUNT(*) gives.
    {
        Stmt stmt(db_,
                  "WITH recent AS (SELECT session_id FROM sessions "
                  "                WHERE (?2 IS NULL OR (started_at IS NOT NULL AND started_at >= ?2)) "
                  "                ORDER BY started_at DESC, session_id DESC LIMIT ?1) "
                  "SELECT e.session_id, COUNT(*) "
                  "FROM snapback_events e JOIN recent r ON e.session_id = r.session_id "
                  "GROUP BY e.session_id");
        stmt.bind(1, static_cast<std::int64_t>(limit));
        if (started_after_ms) {
            stmt.bind(2, *started_after_ms);
        } else {
            stmt.bind_null(2);
        }
        while (stmt.step_row()) {
            const auto it = index_of.find(column_text(stmt.get(), 0));
            if (it == index_of.end()) continue;
            out[it->second].recap.snapback_count =
                static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 1));
        }
    }

    // Query 4: attended time (Roadmap 7.23). Grouped like the others rather than calling
    // active_secs() per session — this whole function exists because that shape cost 1 + 5N
    // round trips under the storage mutex, and adding an N back would undo 7.12.
    //
    // A session with no spans does not appear here, so its recap keeps `active_secs` empty:
    // "never measured", which is exactly right for sessions predating the table.
    {
        Stmt stmt(db_,
                  "WITH recent AS (SELECT session_id FROM sessions "
                  "                WHERE (?3 IS NULL OR (started_at IS NOT NULL AND started_at >= ?3)) "
                  "                ORDER BY started_at DESC, session_id DESC LIMIT ?1) "
                  "SELECT s.session_id, CAST(COALESCE(SUM(ROUND(MAX(0, "
                  "  (COALESCE(s.ended_at, MAX(s.started_at, ?2)) "
                  "   - s.started_at) / 1000.0))), 0) AS INTEGER) "
                  "FROM session_spans s JOIN recent r ON s.session_id = r.session_id "
                  "GROUP BY s.session_id");
        stmt.bind(1, static_cast<std::int64_t>(limit));
        stmt.bind(2, now_ms);
        if (started_after_ms) {
            stmt.bind(3, *started_after_ms);
        } else {
            stmt.bind_null(3);
        }
        while (stmt.step_row()) {
            const auto it = index_of.find(column_text(stmt.get(), 0));
            if (it == index_of.end()) continue;
            out[it->second].recap.active_secs =
                static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 1));
        }
    }

    return out;
}

void Storage::backdate_session_for_test(const std::string& session_id,
                                        std::int64_t started_at_ms) {
    Stmt stmt(db_, "UPDATE sessions SET started_at = ?2 WHERE session_id = ?1");
    stmt.bind(1, session_id);
    stmt.bind(2, started_at_ms);
    stmt.step_done();
}

void Storage::analyze_for_test() {
    Stmt stmt(db_, "ANALYZE");
    stmt.step_done();
}

void Storage::execute_for_test(const std::string& sql) { exec(db_, sql.c_str()); }

std::size_t Storage::count_statements_for_test(const std::function<void()>& body) {
    std::size_t count = 0;
    sqlite3_trace_v2(
        db_, SQLITE_TRACE_STMT,
        [](unsigned, void* context, void*, void*) -> int {
            ++*static_cast<std::size_t*>(context);
            return 0;
        },
        &count);
    // The trace has to come off even if `body` throws: it points at a counter on this frame,
    // and leaving it installed would leave SQLite writing through a dangling pointer.
    struct Untrace {
        sqlite3* db;
        ~Untrace() { sqlite3_trace_v2(db, 0, nullptr, nullptr); }
    } untrace{db_};
    body();
    return count;
}

Storage::PredictionStats Storage::prediction_stats(const std::optional<std::int64_t>& cutoff_ms) {
    PredictionStats out;
    {
        Stmt stmt(db_,
                  "SELECT COUNT(*), COALESCE(AVG(focus_score), 0), "
                  "COALESCE(SUM(CASE WHEN focus_state = 'DISTRACTED' THEN 1 ELSE 0 END), 0) "
                  "FROM predictions WHERE (?1 IS NULL OR timestamp >= ?1)");
        if (cutoff_ms) {
            stmt.bind(1, *cutoff_ms);
        } else {
            stmt.bind_null(1);
        }
        if (stmt.step_row()) {
            out.sample_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
            out.avg_focus_score = sqlite3_column_double(stmt.get(), 1);
            out.distracted_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 2));
        }
    }

    // Roadmap 10.13 + 7.12. The longest unbroken focused stretch, as a **duration**, computed
    // in SQL rather than by reading every row back into C++.
    //
    // Gaps-and-islands, with the island boundary carrying more than one condition. A row
    // starts a new run when it is distracted, when it is the first row, or when the gap to the
    // previous row exceeds the bound — that last one is what makes this a measure of the user
    // rather than of the data, because predictions stop entirely while they are idle or
    // private. A running sum over the break flags numbers the runs; the run's duration is the
    // sum of the gaps *inside* it, so the boundary row contributes nothing and a run of one
    // sample is correctly zero.
    //
    // `prev_distracted` is why the parity test earned its keep. Without it the first focused
    // row after a distracted one keeps its gap, and that interval — the time spent *being
    // distracted* — is credited to the focused run that follows. On the 12,000-row fixture
    // that reported every stretch as exactly twice its real length: a plausible number, wrong
    // by a factor of two, and invisible to any test that only checked it was non-zero.
    //
    // Ordered ascending here, unlike the counting query this replaced: intervals between
    // neighbours have a sign, so the direction is load-bearing rather than incidental. `id`
    // stays in the key because timestamps have whole-second resolution and are not unique.
    //
    // **Partitioned by session**, which the parity test against the C++ implementation is what
    // caught: without it, rows from two sessions recorded in the same second interleave, every
    // gap between them reads as zero, and the run walks the same stretch of time twice. Two
    // sessions seconds apart are still two pieces of work, and a run that spans them reports a
    // stretch of focus the user never had.
    {
        Stmt stmt(db_,
                  "WITH ordered AS ("
                  "  SELECT session_id, ROW_NUMBER() OVER w AS rn,"
                  "         (focus_state = 'DISTRACTED') AS distracted,"
                  "         LAG(focus_state = 'DISTRACTED') OVER w AS prev_distracted,"
                  "         CAST(ROUND((timestamp -"
                  "              LAG(timestamp) OVER w) / 1000.0) AS INTEGER) AS gap"
                  "  FROM predictions WHERE (?1 IS NULL OR timestamp >= ?1)"
                  "  WINDOW w AS (PARTITION BY session_id ORDER BY timestamp ASC, id ASC)"
                  "), marked AS ("
                  "  SELECT session_id, rn, distracted, gap,"
                  "         CASE WHEN distracted = 1 OR prev_distracted IS NULL"
                  "                   OR prev_distracted = 1"
                  "                   OR gap IS NULL OR gap < 0 OR gap > ?2"
                  "              THEN 1 ELSE 0 END AS is_break"
                  "  FROM ordered"
                  "), grouped AS ("
                  "  SELECT session_id, distracted, gap, is_break,"
                  "         SUM(is_break) OVER (PARTITION BY session_id ORDER BY rn"
                  "                             ROWS UNBOUNDED PRECEDING) AS run_id"
                  "  FROM marked"
                  ") "
                  "SELECT COALESCE(MAX(total), 0) FROM ("
                  "  SELECT SUM(CASE WHEN is_break = 1 THEN 0 ELSE gap END) AS total"
                  "  FROM grouped WHERE distracted = 0 GROUP BY session_id, run_id)");
        if (cutoff_ms) {
            stmt.bind(1, *cutoff_ms);
        } else {
            stmt.bind_null(1);
        }
        stmt.bind(2, static_cast<std::int64_t>(kFocusRunGapSecs));
        if (stmt.step_row()) {
            out.longest_focus_secs =
                static_cast<std::uint64_t>(std::max<std::int64_t>(
                    0, sqlite3_column_int64(stmt.get(), 0)));
        }
    }
    return out;
}

std::vector<AnalyticsHour> Storage::hourly_focus_buckets(
    const std::optional<std::int64_t>& cutoff_ms) {
    // `datetime(timestamp, 'localtime')` reads the stored UTC and converts to this machine's
    // local time through the same C library `localtime` that local_hour_from_rfc3339 calls, so
    // the two agree including across a DST boundary. A timestamp SQLite cannot parse yields
    // NULL and is dropped, which is what the C++ loop's `hour < 0 || hour >= 24` did.
    Stmt stmt(db_,
              "SELECT CAST(strftime('%H', timestamp / 1000.0, 'unixepoch', 'localtime')"
              "            AS INTEGER) AS hour,"
              "       COUNT(*), AVG(focus_score),"
              "       CAST(SUM(CASE WHEN focus_state = 'DISTRACTED' THEN 1 ELSE 0 END) AS REAL)"
              "         / COUNT(*) "
              "FROM predictions "
              "WHERE (?1 IS NULL OR timestamp >= ?1) "
              "  AND strftime('%H', timestamp / 1000.0, 'unixepoch', 'localtime')"
              "      IS NOT NULL "
              "GROUP BY hour ORDER BY hour ASC");
    if (cutoff_ms) {
        stmt.bind(1, *cutoff_ms);
    } else {
        stmt.bind_null(1);
    }
    std::vector<AnalyticsHour> hourly;
    while (stmt.step_row()) {
        AnalyticsHour bucket;
        bucket.hour = sqlite3_column_int(stmt.get(), 0);
        bucket.sample_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1));
        bucket.avg_focus_score = sqlite3_column_double(stmt.get(), 2);
        bucket.distracted_fraction = sqlite3_column_double(stmt.get(), 3);
        hourly.push_back(std::move(bucket));
    }
    return hourly;
}

std::vector<DailySummaryDay> Storage::daily_summary(std::int64_t now_ms, std::int64_t since_ms) {
    // Keyed by "YYYY-MM-DD"; std::map keeps the days ascending, and the four queries below
    // merge into it so the statement count stays constant regardless of range (7.12).
    std::map<std::string, DailySummaryDay> by_day;
    const auto bucket_for = [&by_day](sqlite3_stmt* stmt, int column) -> DailySummaryDay& {
        std::string day = column_text(stmt, column);
        auto& bucket = by_day[day];
        bucket.day = day;
        return bucket;
    };

    // Snap the cutoff down to the local midnight of its own day, once, so every query filters
    // on the same absolute instant (a raw-integer compare, which is what lets
    // idx_predictions_ts serve it) and the first bucket is a whole calendar day. This is the
    // one calendar-snapped window in this family of queries; the divergence from the rolling
    // cutoffs is deliberate — a trend chart of partial first days would misread as a bad day.
    std::int64_t start_ms = since_ms;
    {
        Stmt stmt(db_,
                  "SELECT (strftime('%s', ?1 / 1000.0, 'unixepoch', 'localtime',"
                  " 'start of day', 'utc') * 1000)");
        stmt.bind(1, since_ms);
        if (stmt.step_row()) start_ms = sqlite3_column_int64(stmt.get(), 0);
    }
    if (start_ms >= now_ms) return {};

    // Query 1: attended seconds per day. A recursive local-day axis joined to session_spans,
    // so a span crossing midnight splits exactly at the boundary; the clip arithmetic is
    // kAttendedInWindowSql's, applied per day instead of once. Stepping the axis through
    // 'localtime'/'start of day' (never fixed +24h arithmetic) keeps DST correct per instant,
    // the same reasoning as attended_secs_in_local_week.
    {
        Stmt stmt(db_,
                  "WITH RECURSIVE days(day_start, day_end) AS ("
                  "  SELECT ?2, (strftime('%s', ?2 / 1000.0, 'unixepoch', 'localtime',"
                  "              'start of day', '+1 day', 'utc') * 1000)"
                  "  UNION ALL"
                  "  SELECT day_end, (strftime('%s', day_end / 1000.0, 'unixepoch', 'localtime',"
                  "                   'start of day', '+1 day', 'utc') * 1000)"
                  "  FROM days WHERE day_end <= ?1"
                  ") "
                  "SELECT strftime('%Y-%m-%d', day_start / 1000.0, 'unixepoch', 'localtime'),"
                  "       CAST(ROUND(COALESCE(SUM(MAX(0,"
                  "         MIN(COALESCE(s.ended_at, ?1), day_end)"
                  "         - MAX(s.started_at, day_start)"
                  "       )), 0) / 1000.0) AS INTEGER) "
                  "FROM days LEFT JOIN session_spans s"
                  "  ON s.started_at < day_end AND COALESCE(s.ended_at, ?1) > day_start "
                  "GROUP BY day_start ORDER BY day_start ASC");
        stmt.bind(1, now_ms);
        stmt.bind(2, start_ms);
        while (stmt.step_row()) {
            const auto secs = sqlite3_column_int64(stmt.get(), 1);
            if (secs <= 0) continue;  // days with no data are omitted, as hourly buckets do
            bucket_for(stmt.get(), 0).attended_secs = static_cast<std::uint64_t>(secs);
        }
    }

    // Query 2: prediction volume and average per local date. The filters stay on the raw
    // integer; 'localtime' appears only in the bucketing expression (ADR-0007's rule). The
    // upper bound matters here where the scalar queries skip it: the day axis ends at `now`,
    // so a future-dated row (clock skew, a fixture) must not grow the series past it.
    {
        Stmt stmt(db_,
                  "SELECT strftime('%Y-%m-%d', timestamp / 1000.0, 'unixepoch', 'localtime')"
                  "         AS day,"
                  "       COUNT(*), AVG(focus_score) "
                  "FROM predictions "
                  "WHERE timestamp >= ?1 AND timestamp <= ?2 "
                  "  AND strftime('%Y-%m-%d', timestamp / 1000.0, 'unixepoch', 'localtime')"
                  "      IS NOT NULL "
                  "GROUP BY day ORDER BY day ASC");
        stmt.bind(1, start_ms);
        stmt.bind(2, now_ms);
        while (stmt.step_row()) {
            auto& bucket = bucket_for(stmt.get(), 0);
            bucket.sample_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1));
            bucket.avg_focus_score = sqlite3_column_double(stmt.get(), 2);
        }
    }

    // Query 3: focused and deep seconds per day — prediction_stats' run arithmetic, summed
    // per day instead of MAX'd per run. A gap counts toward a state's total when *both* of
    // its endpoint rows qualify (non-DISTRACTED for focused, DEEP_FOCUS for deep) and the gap
    // is within kFocusRunGapSecs; that is exactly "inside a run" without needing the run
    // numbering, because a per-day SUM has no per-run grouping to preserve. The both-ends
    // rule is prediction_stats' prev_distracted lesson: the interval *entering* a state is
    // not time spent in it. `prev_* = 0` is false for the NULL of a partition's first row,
    // which is what excludes it. Each gap lands on the local date of its later row, so a run
    // crossing midnight misplaces at most kFocusRunGapSecs per midnight — accepted; spans
    // (query 1) split exactly. DEEP_FOCUS implies non-DISTRACTED, so focused >= deep per day.
    {
        Stmt stmt(db_,
                  "WITH ordered AS ("
                  "  SELECT strftime('%Y-%m-%d', timestamp / 1000.0, 'unixepoch', 'localtime')"
                  "           AS day,"
                  "         (focus_state = 'DISTRACTED') AS distracted,"
                  "         LAG(focus_state = 'DISTRACTED') OVER w AS prev_distracted,"
                  "         (focus_state = 'DEEP_FOCUS') AS deep,"
                  "         LAG(focus_state = 'DEEP_FOCUS') OVER w AS prev_deep,"
                  "         CAST(ROUND((timestamp -"
                  "              LAG(timestamp) OVER w) / 1000.0) AS INTEGER) AS gap"
                  "  FROM predictions WHERE timestamp >= ?1 AND timestamp <= ?2"
                  "  WINDOW w AS (PARTITION BY session_id ORDER BY timestamp ASC, id ASC)"
                  ") "
                  "SELECT day,"
                  "  COALESCE(SUM(CASE WHEN distracted = 0 AND prev_distracted = 0"
                  "                    AND gap IS NOT NULL AND gap >= 0 AND gap <= ?3"
                  "               THEN gap ELSE 0 END), 0),"
                  "  COALESCE(SUM(CASE WHEN deep = 1 AND prev_deep = 1"
                  "                    AND gap IS NOT NULL AND gap >= 0 AND gap <= ?3"
                  "               THEN gap ELSE 0 END), 0) "
                  "FROM ordered WHERE day IS NOT NULL GROUP BY day ORDER BY day ASC");
        stmt.bind(1, start_ms);
        stmt.bind(2, now_ms);
        stmt.bind(3, static_cast<std::int64_t>(kFocusRunGapSecs));
        while (stmt.step_row()) {
            const auto focused = sqlite3_column_int64(stmt.get(), 1);
            const auto deep = sqlite3_column_int64(stmt.get(), 2);
            if (focused <= 0 && deep <= 0) continue;
            auto& bucket = bucket_for(stmt.get(), 0);
            bucket.focused_secs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, focused));
            bucket.deep_focus_secs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, deep));
        }
    }

    // Query 4: sessions started and snapback episodes per local date.
    {
        Stmt stmt(db_,
                  "SELECT strftime('%Y-%m-%d', started_at / 1000.0, 'unixepoch', 'localtime')"
                  "         AS day, COUNT(*) "
                  "FROM sessions "
                  "WHERE started_at IS NOT NULL AND started_at >= ?1 AND started_at <= ?2 "
                  "  AND strftime('%Y-%m-%d', started_at / 1000.0, 'unixepoch', 'localtime')"
                  "      IS NOT NULL "
                  "GROUP BY day ORDER BY day ASC");
        stmt.bind(1, start_ms);
        stmt.bind(2, now_ms);
        while (stmt.step_row()) {
            bucket_for(stmt.get(), 0).session_count =
                static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1));
        }
    }
    {
        Stmt stmt(db_,
                  "SELECT strftime('%Y-%m-%d', timestamp / 1000.0, 'unixepoch', 'localtime')"
                  "         AS day, COUNT(*) "
                  "FROM snapback_events "
                  "WHERE timestamp >= ?1 AND timestamp <= ?2 "
                  "  AND strftime('%Y-%m-%d', timestamp / 1000.0, 'unixepoch', 'localtime')"
                  "      IS NOT NULL "
                  "GROUP BY day ORDER BY day ASC");
        stmt.bind(1, start_ms);
        stmt.bind(2, now_ms);
        while (stmt.step_row()) {
            bucket_for(stmt.get(), 0).snapback_count =
                static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1));
        }
    }

    std::vector<DailySummaryDay> out;
    out.reserve(by_day.size());
    for (auto& [day, bucket] : by_day) out.push_back(std::move(bucket));
    return out;
}

std::size_t Storage::productive_session_streak(std::size_t limit, double min_avg_focus,
                                               const std::optional<std::int64_t>& started_after_ms) {
    Stmt stmt(db_,
              "WITH recent AS ("
              "  SELECT session_id, status, started_at FROM sessions"
              "  WHERE (?3 IS NULL OR (started_at IS NOT NULL AND started_at >= ?3))"
              "  ORDER BY started_at DESC, session_id DESC LIMIT ?1"
              "), completed AS ("
              "  SELECT session_id,"
              "         ROW_NUMBER() OVER (ORDER BY started_at DESC, session_id DESC) AS rank"
              "  FROM recent WHERE status = 'COMPLETED'"
              "), scored AS ("
              "  SELECT c.rank, COALESCE(AVG(p.focus_score), 0) AS avg_focus"
              "  FROM completed c LEFT JOIN predictions p ON p.session_id = c.session_id"
              "  GROUP BY c.rank"
              ") "
              "SELECT COALESCE((SELECT MIN(rank) FROM scored WHERE avg_focus < ?2) - 1,"
              "                (SELECT COUNT(*) FROM scored))");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    stmt.bind(2, min_avg_focus);
    if (started_after_ms) {
        stmt.bind(3, *started_after_ms);
    } else {
        stmt.bind_null(3);
    }
    if (!stmt.step_row()) return 0;
    return static_cast<std::size_t>(std::max<std::int64_t>(0, sqlite3_column_int64(stmt.get(), 0)));
}

Storage::SessionWindowTotals Storage::session_window_totals(std::size_t limit,
                                                            std::int64_t started_after_ms) {
    // ROUND before CAST, the same way recap() computes duration_secs — a truncating CAST on
    // julianday's double turns an exact hour into 3599 seconds.
    Stmt stmt(db_,
              "WITH recent AS ("
              "  SELECT status, started_at, ended_at FROM sessions"
              "  ORDER BY started_at DESC, session_id DESC LIMIT ?1"
              ") "
              "SELECT COUNT(*),"
              "       COALESCE(SUM(status = 'COMPLETED'), 0),"
              "       COALESCE(SUM(CASE WHEN status = 'COMPLETED' THEN"
              "         CAST(ROUND(MAX(0, (COALESCE(ended_at, (strftime('%s','now') * 1000))"
              "              - started_at) / 1000.0)) AS INTEGER)"
              "         ELSE 0 END), 0) "
              "FROM recent WHERE started_at IS NOT NULL AND started_at >= ?2");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    stmt.bind(2, started_after_ms);
    SessionWindowTotals out;
    if (stmt.step_row()) {
        out.session_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
        out.completed_session_count =
            static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1));
        out.focus_seconds = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 2));
    }
    return out;
}

std::unordered_map<std::string, std::size_t> Storage::context_app_counts(
    std::size_t session_limit, std::size_t per_session_limit,
    const std::optional<std::int64_t>& started_after_ms) {
    // ROW_NUMBER reproduces list_context_snapshots' "ORDER BY timestamp ASC LIMIT n" per
    // session, so the per-session cap survives the move into SQL. Without it this would
    // count every snapshot and quietly change which app ranks first.
    const char* sql =
        "WITH recent AS ("
        "  SELECT session_id FROM sessions"
        "  WHERE (?3 IS NULL OR (started_at IS NOT NULL AND started_at >= ?3))"
        "  ORDER BY started_at DESC, session_id DESC LIMIT ?1"
        "), capped AS ("
        "  SELECT c.app_name,"
        "         ROW_NUMBER() OVER (PARTITION BY c.session_id ORDER BY c.timestamp ASC) AS rn"
        "  FROM context_snapshots c JOIN recent r ON c.session_id = r.session_id"
        ") "
        "SELECT app_name, COUNT(*) FROM capped "
        "WHERE rn <= ?2 AND app_name <> '' GROUP BY app_name";
    Stmt stmt(db_, sql);
    stmt.bind(1, static_cast<std::int64_t>(session_limit));
    stmt.bind(2, static_cast<std::int64_t>(per_session_limit));
    if (started_after_ms) {
        stmt.bind(3, *started_after_ms);
    } else {
        stmt.bind_null(3);  // the wrapper checks the return code; the raw call does not
    }

    std::unordered_map<std::string, std::size_t> counts;
    while (stmt.step_row()) {
        counts.emplace(column_text(stmt.get(), 0),
                       static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1)));
    }
    return counts;
}

void Storage::delete_all_activity_data() {
    Transaction transaction(*this);
    // Keep the order explicit instead of depending on every historical database having
    // ON DELETE CASCADE. app_rules is deliberately absent: it is user configuration, not
    // captured activity.
    exec(db_,
         R"sql(
            DELETE FROM session_spans;
            DELETE FROM feature_snapshots;
            DELETE FROM context_snapshots;
            DELETE FROM snapback_events;
            DELETE FROM labels;
            DELETE FROM predictions;
            DELETE FROM sessions;
         )sql");
    transaction.commit();
}

bool Storage::delete_session(const std::string& session_id) {
    Transaction transaction(*this);
    {
        // Check inside the transaction, not before it: BEGIN IMMEDIATE already holds the
        // write lock here, so the answer cannot change between the check and the deletes.
        Stmt exists(db_, "SELECT 1 FROM sessions WHERE session_id = ?1");
        exists.bind(1, session_id);
        if (!exists.step_row()) return false;  // ~Transaction rolls back the empty txn
    }

    // Child rows first, in the same order and for the same reason as
    // delete_all_activity_data: historical databases were not all created with
    // ON DELETE CASCADE, so relying on it would leave orphans on exactly the old files
    // Roadmap 7.11's fixtures exist to represent.
    for (const char* sql : {"DELETE FROM session_spans WHERE session_id = ?1",
                            "DELETE FROM feature_snapshots WHERE session_id = ?1",
                            "DELETE FROM context_snapshots WHERE session_id = ?1",
                            "DELETE FROM snapback_events WHERE session_id = ?1",
                            "DELETE FROM labels WHERE session_id = ?1",
                            "DELETE FROM predictions WHERE session_id = ?1",
                            "DELETE FROM sessions WHERE session_id = ?1"}) {
        Stmt stmt(db_, sql);
        stmt.bind(1, session_id);
        // step_done(), not step_row(): a DELETE returns no rows, and step_done throws if one
        // somehow appears instead of silently reporting "no row" the way step_row would.
        stmt.step_done();
    }
    transaction.commit();
    return true;
}

SessionRecap Storage::recap(const std::string& session_id) {
    SessionRecord session;
    {
        const std::string sql =
            "SELECT " + std::string(kSessionColumns) + " FROM sessions WHERE session_id = ?1";
        Stmt stmt(db_, sql.c_str());
        stmt.bind(1, session_id);
        if (!stmt.step_row()) throw std::runtime_error("session not found");
        session = read_session(stmt.get());
    }

    SessionRecap out;
    out.session_id = session_id;
    out.goal = session.goal;

    {
        Stmt stmt(db_,
                  // ROUND before CAST — same reason as the batched query above.
                  "SELECT CAST(ROUND(MAX(0, (COALESCE(ended_at, (strftime('%s','now') * 1000)) - "
                  "started_at) / 1000.0)) AS INTEGER) "
                  "FROM sessions WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (stmt.step_row()) out.duration_secs = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 0));
    }

    out.active_secs = active_secs(session_id, unix_now_ms());

    {
        Stmt stmt(db_,
                  "SELECT COALESCE(AVG(focus_score), 0), "
                  "COALESCE(AVG(distraction_risk), 0), "
                  "COALESCE(100.0 * SUM(CASE WHEN focus_state = 'DEEP_FOCUS' THEN 1 ELSE 0 END) / "
                  "NULLIF(COUNT(*), 0), 0) "
                  "FROM predictions WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (stmt.step_row()) {
            out.avg_focus_score = sqlite3_column_double(stmt.get(), 0);
            out.avg_distraction_risk = sqlite3_column_double(stmt.get(), 1);
            out.deep_focus_pct = sqlite3_column_double(stmt.get(), 2);
        }
    }

    {
        Stmt stmt(db_, "SELECT COUNT(*) FROM snapback_events WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (stmt.step_row()) out.snapback_count = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 0));
    }

    {
        // The 0.7 is deliberate: it's an *absolute* "this was a strong distraction" bar,
        // not the mode's alerting threshold
        // (risk_threshold() = 0.55/0.70/0.85). Keeping it absolute stops Deep mode's higher
        // sensitivity from inflating session-quality metrics that feed auto-labels.
        //
        // ADR-0004 made it a pure opinion-channel count: distraction_risk alone, no
        // focus_state conjunct. The state is the policy verdict and carries the mode with
        // it — the old `AND focus_state = 'DISTRACTED'` meant Recovery rows between 0.7
        // and 0.85 were never counted, which defeated the point of an absolute bar.
        Stmt stmt(db_,
                  "SELECT COUNT(*) FROM predictions WHERE session_id = ?1 "
                  "AND distraction_risk >= 0.7");
        stmt.bind(1, session_id);
        if (stmt.step_row()) out.thrash_spikes = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 0));
    }

    return out;
}

FocusLabel Storage::infer_session_label(const SessionRecap& recap) {
    if (recap.deep_focus_pct >= 50.0 && recap.avg_distraction_risk < 0.35) {
        return FocusLabel::DeepFocus;
    }
    if (recap.avg_distraction_risk >= 0.6 || recap.thrash_spikes >= 3) {
        return FocusLabel::Distracted;
    }
    if (recap.deep_focus_pct < 25.0 && recap.thrash_spikes >= 1) {
        return FocusLabel::PseudoProductive;
    }
    return FocusLabel::Productive;
}

FocusLabel Storage::save_auto_session_label(const std::string& session_id) {
    // Roadmap 7.25. One automatic label per session, enforced here rather than by every
    // caller remembering. `Storage::stop_session` is idempotent, so a double-click on Stop
    // reached this twice and appended a second inferred verdict — two `auto` rows for one
    // session, differing whenever a prediction landed between them, with nothing to say which
    // was meant. The labels table is append-only by design (a correction is a new row, not an
    // edit), which is exactly why a duplicate cannot be cleaned up afterwards.
    //
    // The existing label wins rather than the newer inference: it is the one the user was
    // shown, and re-deriving a verdict they have already seen is a silent revision.
    {
        Stmt existing(db_,
                      "SELECT label FROM labels WHERE session_id = ?1 AND source = ?2 "
                      "ORDER BY id ASC LIMIT 1");
        existing.bind(1, session_id);
        existing.bind(2, std::string(label_source_as_str(LabelSource::Auto)));
        if (existing.step_row()) {
            return static_cast<FocusLabel>(sqlite3_column_int(existing.get(), 0));
        }
    }

    const SessionRecap session_recap = recap(session_id);
    const FocusLabel label = infer_session_label(session_recap);
    insert_label(session_id, label, label_source_as_str(LabelSource::Auto),
                 std::string("inferred from session recap"));
    return label;
}

void Storage::insert_prediction(const PredictionRecord& p) {
    ensure_active_session(p.session_id);
    Stmt stmt(db_, cached_stmt(
                       "INSERT INTO predictions "
                       "(session_id, focus_score, distraction_risk, focus_state, thrash_score, "
                       "drift_score, goal_alignment, timestamp, model_id, state_source) "
                       "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"));
    stmt.bind(1, p.session_id);
    stmt.bind(2, p.focus_score);
    stmt.bind(3, p.distraction_risk);
    stmt.bind(4, p.focus_state);
    stmt.bind(5, p.thrash_score);
    stmt.bind(6, p.drift_score);
    stmt.bind(7, p.goal_alignment);
    stmt.bind(8, p.timestamp_ms);
    stmt.bind(9, p.model_id);
    if (p.state_source) {
        stmt.bind(10, *p.state_source);
    } else {
        stmt.bind_null(10);
    }
    stmt.step_done();
}

std::optional<PredictionRecord> Storage::latest_prediction() {
    Stmt stmt(db_,
              "SELECT session_id, focus_score, distraction_risk, focus_state, thrash_score, "
              "drift_score, goal_alignment, timestamp, model_id, state_source "
              "FROM predictions ORDER BY timestamp DESC LIMIT 1");
    if (!stmt.step_row()) return std::nullopt;
    return read_prediction(stmt.get());
}

std::vector<PredictionRecord> Storage::recent_predictions(std::size_t limit) {
    Stmt stmt(db_,
              "SELECT session_id, focus_score, distraction_risk, focus_state, thrash_score, "
              "drift_score, goal_alignment, timestamp, model_id, state_source "
              "FROM predictions ORDER BY timestamp DESC LIMIT ?1");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    std::vector<PredictionRecord> rows;
    while (stmt.step_row()) rows.push_back(read_prediction(stmt.get()));
    return rows;
}

std::vector<PredictionRecord> Storage::predictions_since(
    const std::optional<std::int64_t>& cutoff_ms) {
    const char* sql = cutoff_ms
                          ? "SELECT session_id, focus_score, distraction_risk, focus_state, "
                            "thrash_score, drift_score, goal_alignment, timestamp, model_id, "
                            "state_source "
                            "FROM predictions WHERE timestamp >= ?1 "
                            "ORDER BY timestamp DESC"
                          : "SELECT session_id, focus_score, distraction_risk, focus_state, "
                            "thrash_score, drift_score, goal_alignment, timestamp, model_id, "
                            "state_source "
                            "FROM predictions ORDER BY timestamp DESC";
    Stmt stmt(db_, sql);
    if (cutoff_ms) stmt.bind(1, *cutoff_ms);
    std::vector<PredictionRecord> rows;
    while (stmt.step_row()) rows.push_back(read_prediction(stmt.get()));
    return rows;
}

void Storage::insert_feature_snapshot(const std::string& session_id, const FeatureVector& f) {
    ensure_active_session(session_id);
    Stmt stmt(db_, cached_stmt(
              "INSERT INTO feature_snapshots ("
              "session_id, timestamp, seconds_since_session_start, hour_of_day, day_of_week, "
              "minutes_since_last_break, keystroke_count, keystroke_rate, "
              "keystroke_interval_mean, keystroke_interval_std, keystroke_interval_trend, "
              "mouse_move_count, mouse_distance_pixels, mouse_speed_mean, mouse_speed_std, "
              "mouse_acceleration_mean, mouse_click_count, context_switches_30s, "
              "context_switches_5min, time_in_current_app, unique_apps_5min, idle_time_30s, "
              "idle_event_count_5min, longest_active_stretch_5min, window_title_length, "
              "window_title_changed_30s, is_browser, is_ide, is_communication, "
              "is_entertainment, is_productivity, focus_momentum, is_pseudo_productive) "
              "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
              "?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, "
              "?29, ?30, ?31, ?32, ?33)"));
    stmt.bind(1, session_id);
    stmt.bind(2, unix_now_ms());
    for (std::size_t i = 0; i < f.values.size(); ++i) {
        stmt.bind(static_cast<int>(i) + 3, f.values[i]);
    }
    stmt.step_done();
}

void Storage::insert_label(const std::string& session_id, FocusLabel label,
                           const std::string& source,
                           std::optional<std::string> notes) {
    Stmt stmt(db_, cached_stmt(
                       "INSERT INTO labels (session_id, label, source, notes, timestamp) "
                       "VALUES (?1, ?2, ?3, ?4, ?5)"));
    stmt.bind(1, session_id);
    stmt.bind(2, static_cast<std::int64_t>(static_cast<int>(label)));
    stmt.bind(3, source);
    if (notes) stmt.bind(4, *notes);
    else stmt.bind_null(4);
    stmt.bind(5, unix_now_ms());
    stmt.step_done();
}

std::vector<AppRuleRecord> Storage::list_app_rules() {
    Stmt stmt(db_,
              "SELECT id, pattern, rule_type, note, created_at, updated_at "
              "FROM app_rules ORDER BY pattern ASC");
    std::vector<AppRuleRecord> rows;
    while (stmt.step_row()) rows.push_back(read_app_rule(stmt.get()));
    return rows;
}

AppRuleRecord Storage::upsert_app_rule(const std::string& pattern, AppRuleKind rule_type,
                                       std::optional<std::string> note) {
    const std::int64_t now_ms = unix_now_ms();
    // UPSERT on the UNIQUE pattern: insert, or update rule_type/note/updated_at if the
    // pattern already exists. ?4 is bound once but used for both created_at and updated_at.
    Stmt stmt(db_,
              "INSERT INTO app_rules (pattern, rule_type, note, created_at, updated_at) "
              "VALUES (?1, ?2, ?3, ?4, ?4) "
              "ON CONFLICT(pattern) DO UPDATE SET "
              "rule_type = excluded.rule_type, "
              "note = excluded.note, "
              "updated_at = excluded.updated_at");
    stmt.bind(1, pattern);
    stmt.bind(2, std::string(app_rule_kind_to_string(rule_type)));
    if (note) stmt.bind(3, *note);
    else stmt.bind_null(3);
    stmt.bind(4, now_ms);
    stmt.step_done();

    Stmt fetch(db_,
               "SELECT id, pattern, rule_type, note, created_at, updated_at "
               "FROM app_rules WHERE pattern = ?1 COLLATE NOCASE");
    fetch.bind(1, pattern);
    if (!fetch.step_row()) throw std::runtime_error("app rule not found after upsert");
    return read_app_rule(fetch.get());
}

void Storage::delete_app_rule(std::int64_t id) {
    Stmt stmt(db_, "DELETE FROM app_rules WHERE id = ?1");
    stmt.bind(1, id);
    stmt.step_done();
    if (sqlite3_changes(db_) == 0) {
        throw std::runtime_error("app rule not found");
    }
}

bool Storage::insert_snapback_episode(const SnapbackEpisode& episode) {
    // OR IGNORE against idx_snapback_events_episode. The alternative — checking first, then
    // inserting — is two statements with a race between them, and this write happens inside
    // the engine tick's transaction where a second round trip is not free.
    Stmt stmt(db_, cached_stmt(
                       "INSERT OR IGNORE INTO snapback_events "
                       "(session_id, summary, timestamp, started_at, duration_secs, app_name, "
                       "file_hint) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)"));
    stmt.bind(1, episode.session_id);
    stmt.bind(2, episode.summary);
    stmt.bind(3, episode.ended_at_ms);
    // An empty start is the pre-2.15 row's "nobody recorded when this began", and it has to
    // reach the column as NULL rather than as some stand-in instant. The UNIQUE index over
    // (session_id, started_at) treats NULLs as distinct, which is what lets those rows coexist
    // without colliding; a shared sentinel would make the second one silently vanish into
    // INSERT OR IGNORE.
    if (episode.started_at_ms) {
        stmt.bind(4, *episode.started_at_ms);
    } else {
        stmt.bind_null(4);
    }
    stmt.bind(5, static_cast<std::int64_t>(episode.duration_secs));
    stmt.bind(6, episode.app_name);
    stmt.bind(7, episode.file_hint);
    stmt.step_done();
    return sqlite3_changes(db_) > 0;
}

std::vector<SnapbackEpisode> Storage::list_snapback_episodes(const std::string& session_id,
                                                             std::size_t limit) {
    // Ordered by the episode's own start where there is one, falling back to the return time
    // for pre-2.15 rows, so a mixed table still reads chronologically.
    Stmt stmt(db_,
              "SELECT session_id, summary, timestamp, started_at, duration_secs, app_name, "
              "file_hint FROM snapback_events WHERE session_id = ?1 "
              "ORDER BY COALESCE(started_at, timestamp) ASC, id ASC LIMIT ?2");
    stmt.bind(1, session_id);
    stmt.bind(2, static_cast<std::int64_t>(limit));
    std::vector<SnapbackEpisode> rows;
    while (stmt.step_row()) {
        SnapbackEpisode episode;
        episode.session_id = column_text(stmt.get(), 0);
        episode.summary = column_text(stmt.get(), 1);
        episode.ended_at_ms = sqlite3_column_int64(stmt.get(), 2);
        episode.started_at_ms = column_opt_ms(stmt.get(), 3);
        episode.duration_secs =
            static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 4));
        episode.app_name = column_text(stmt.get(), 5);
        episode.file_hint = column_text(stmt.get(), 6);
        rows.push_back(std::move(episode));
    }
    return rows;
}

void Storage::save_context_snapshot(const std::string& session_id,
                                    const ContextSnapshotDto& snap) {
    Stmt stmt(db_, cached_stmt(
                       "INSERT INTO context_snapshots "
                       "(session_id, app_name, window_title, file_hint, project_hint, summary, "
                       "timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)"));
    stmt.bind(1, session_id);
    stmt.bind(2, snap.app_name);
    stmt.bind(3, snap.window_title);
    stmt.bind(4, snap.file_hint);
    stmt.bind(5, snap.project_hint);
    stmt.bind(6, snap.summary);
    stmt.bind(7, snap.timestamp_ms);
    stmt.step_done();
}

std::vector<SessionRecord> Storage::sessions_after(const std::optional<SessionCursor>& after,
                                                   std::size_t limit) {
    // The row-value comparison `(a, b) < (?, ?)` is the keyset predicate written the way
    // SQLite can serve from idx_sessions_status_started / the primary key, and it expresses
    // "strictly after in this ordering" without the three-way OR expansion by hand.
    const std::string sql =
        "SELECT " + std::string(kSessionColumns) + " FROM sessions " +
        (after ? "WHERE (started_at, session_id) < (?2, ?3) " : "") +
        "ORDER BY started_at DESC, session_id DESC LIMIT ?1";
    Stmt stmt(db_, sql.c_str());
    stmt.bind(1, static_cast<std::int64_t>(limit));
    if (after) {
        stmt.bind(2, after->started_at_ms);
        stmt.bind(3, after->session_id);
    }
    std::vector<SessionRecord> rows;
    while (stmt.step_row()) rows.push_back(read_session(stmt.get()));
    return rows;
}

Storage::ContextPage Storage::context_snapshots_after(const std::string& session_id,
                                                      const std::optional<ContextCursor>& after,
                                                      std::size_t limit) {
    const char* sql =
        after ? "SELECT app_name, window_title, file_hint, project_hint, summary, timestamp, id "
                "FROM context_snapshots WHERE session_id = ?1 AND (timestamp, id) > (?3, ?4) "
                "ORDER BY timestamp ASC, id ASC LIMIT ?2"
              : "SELECT app_name, window_title, file_hint, project_hint, summary, timestamp, id "
                "FROM context_snapshots WHERE session_id = ?1 "
                "ORDER BY timestamp ASC, id ASC LIMIT ?2";
    Stmt stmt(db_, sql);
    stmt.bind(1, session_id);
    stmt.bind(2, static_cast<std::int64_t>(limit));
    if (after) {
        stmt.bind(3, after->timestamp_ms);
        stmt.bind(4, after->id);
    }
    ContextPage page;
    while (stmt.step_row()) {
        page.rows.push_back(read_context_snapshot(stmt.get()));
        page.next.timestamp_ms = sqlite3_column_int64(stmt.get(), 5);
        page.next.id = sqlite3_column_int64(stmt.get(), 6);
    }
    return page;
}

std::size_t Storage::count_context_snapshots(const std::string& session_id) {
    Stmt stmt(db_, "SELECT COUNT(*) FROM context_snapshots WHERE session_id = ?1");
    stmt.bind(1, session_id);
    if (!stmt.step_row()) return 0;
    return static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
}

std::vector<ContextSnapshotDto> Storage::list_context_snapshots(const std::string& session_id,
                                                                std::size_t limit) {
    Stmt stmt(db_,
              "SELECT app_name, window_title, file_hint, project_hint, summary, timestamp "
              "FROM context_snapshots WHERE session_id = ?1 "
              "ORDER BY timestamp ASC LIMIT ?2");
    stmt.bind(1, session_id);
    stmt.bind(2, static_cast<std::int64_t>(limit));
    std::vector<ContextSnapshotDto> rows;
    while (stmt.step_row()) rows.push_back(read_context_snapshot(stmt.get()));
    return rows;
}

ExportTrainingResult Storage::export_training_csv(
    const std::filesystem::path& out_dir, const std::optional<std::string>& session_id) {
    std::filesystem::create_directories(out_dir);
    std::uint64_t feature_count = 0;
    std::uint64_t label_count = 0;

    const char* feature_select_filtered =
        "SELECT fs.timestamp, "
        "fs.seconds_since_session_start, fs.hour_of_day, fs.day_of_week, "
        "fs.minutes_since_last_break, fs.keystroke_count, fs.keystroke_rate, "
        "fs.keystroke_interval_mean, fs.keystroke_interval_std, "
        "fs.keystroke_interval_trend, fs.mouse_move_count, fs.mouse_distance_pixels, "
        "fs.mouse_speed_mean, fs.mouse_speed_std, fs.mouse_acceleration_mean, "
        "fs.mouse_click_count, fs.context_switches_30s, fs.context_switches_5min, "
        "fs.time_in_current_app, fs.unique_apps_5min, fs.idle_time_30s, "
        "fs.idle_event_count_5min, fs.longest_active_stretch_5min, "
        "fs.window_title_length, fs.window_title_changed_30s, fs.is_browser, "
        "fs.is_ide, fs.is_communication, fs.is_entertainment, fs.is_productivity, "
        "fs.focus_momentum, fs.is_pseudo_productive, fs.session_id, s.goal, s.focus_mode "
        "FROM feature_snapshots fs "
        "LEFT JOIN sessions s ON s.session_id = fs.session_id "
        // `fs.id` breaks the tie, and it has to: the timestamp is whole milliseconds, and
        // several feature rows can land in one. Under the old REAL seconds column that was
        // rare enough to go unnoticed -- which is exactly why it needs stating rather than
        // rediscovering. `id` is the AUTOINCREMENT insertion order, so equal timestamps come
        // back in the order they were written.
        "WHERE fs.session_id = ?1 ORDER BY fs.timestamp ASC, fs.id ASC";

    const char* feature_select_all =
        "SELECT fs.timestamp, "
        "fs.seconds_since_session_start, fs.hour_of_day, fs.day_of_week, "
        "fs.minutes_since_last_break, fs.keystroke_count, fs.keystroke_rate, "
        "fs.keystroke_interval_mean, fs.keystroke_interval_std, "
        "fs.keystroke_interval_trend, fs.mouse_move_count, fs.mouse_distance_pixels, "
        "fs.mouse_speed_mean, fs.mouse_speed_std, fs.mouse_acceleration_mean, "
        "fs.mouse_click_count, fs.context_switches_30s, fs.context_switches_5min, "
        "fs.time_in_current_app, fs.unique_apps_5min, fs.idle_time_30s, "
        "fs.idle_event_count_5min, fs.longest_active_stretch_5min, "
        "fs.window_title_length, fs.window_title_changed_30s, fs.is_browser, "
        "fs.is_ide, fs.is_communication, fs.is_entertainment, fs.is_productivity, "
        "fs.focus_momentum, fs.is_pseudo_productive, fs.session_id, s.goal, s.focus_mode "
        "FROM feature_snapshots fs "
        "LEFT JOIN sessions s ON s.session_id = fs.session_id "
        "WHERE fs.session_id IS NOT NULL AND fs.session_id != '' "
        "AND fs.session_id != 'idle' "
        "ORDER BY fs.timestamp ASC, fs.id ASC";

    {
        const auto features_path = out_dir / "features.csv";
        std::ofstream out(features_path, std::ios::binary);
        if (!out) throw std::runtime_error("failed to open features.csv");

        std::vector<std::string> header = {"timestamp"};
        for (const auto column : kFeatureColumns) header.emplace_back(column);
        header.emplace_back("session_id");
        header.emplace_back("session_goal");
        header.emplace_back("focus_mode");
        write_csv_row(out, header);

        if (session_id) {
            Stmt stmt(db_, feature_select_filtered);
            stmt.bind(1, *session_id);
            while (stmt.step_row()) {
                ++feature_count;
                std::vector<std::string> row;
                for (int i = 0; i < 32; ++i) {
                    row.push_back(double_cell(sqlite3_column_double(stmt.get(), i)));
                }
                row.push_back(column_text(stmt.get(), 32));
                row.push_back(column_text(stmt.get(), 33));
                row.push_back(column_text(stmt.get(), 34));
                write_csv_row(out, row);
            }
        } else {
            Stmt stmt(db_, feature_select_all);
            while (stmt.step_row()) {
                ++feature_count;
                std::vector<std::string> row;
                for (int i = 0; i < 32; ++i) {
                    row.push_back(double_cell(sqlite3_column_double(stmt.get(), i)));
                }
                row.push_back(column_text(stmt.get(), 32));
                row.push_back(column_text(stmt.get(), 33));
                row.push_back(column_text(stmt.get(), 34));
                write_csv_row(out, row);
            }
        }
        // Checking only at open() catches a bad path but not a full disk: the writes above
        // fail silently and the stream just sets badbit. Without this, export returns a
        // success result whose feature_count doesn't match the truncated file on disk, and
        // the training pipeline then trains on whatever survived.
        out.flush();
        if (!out) throw std::runtime_error("failed to write features.csv (disk full?)");
    }

    {
        const auto labels_path = out_dir / "labels.csv";
        std::ofstream out(labels_path, std::ios::binary);
        if (!out) throw std::runtime_error("failed to open labels.csv");
        write_csv_row(out, {"timestamp", "label", "source", "session_id", "notes"});

        if (session_id) {
            Stmt stmt(db_,
                      "SELECT timestamp, label, source, session_id, COALESCE(notes, '') "
                      "FROM labels WHERE session_id = ?1 ORDER BY timestamp ASC");
            stmt.bind(1, *session_id);
            while (stmt.step_row()) {
                ++label_count;
                write_csv_row(out,
                              {column_text(stmt.get(), 0),
                               std::to_string(sqlite3_column_int(stmt.get(), 1)),
                               column_text(stmt.get(), 2),
                               column_text(stmt.get(), 3),
                               column_text(stmt.get(), 4)});
            }
        } else {
            Stmt stmt(db_,
                      "SELECT timestamp, label, source, session_id, COALESCE(notes, '') "
                      "FROM labels ORDER BY timestamp ASC");
            while (stmt.step_row()) {
                ++label_count;
                write_csv_row(out,
                              {column_text(stmt.get(), 0),
                               std::to_string(sqlite3_column_int(stmt.get(), 1)),
                               column_text(stmt.get(), 2),
                               column_text(stmt.get(), 3),
                               column_text(stmt.get(), 4)});
            }
        }
        out.flush();
        if (!out) throw std::runtime_error("failed to write labels.csv (disk full?)");
    }

    ExportTrainingResult result;
    result.output_dir = out_dir.string();
    result.features_path = (out_dir / "features.csv").string();
    result.labels_path = (out_dir / "labels.csv").string();
    result.feature_count = feature_count;
    result.label_count = label_count;
    return result;
}

PruneSummary Storage::prune_runtime_data(std::int64_t cutoff_unix_ms) {
    // Roadmap 5.5, closed by ADR-0007 rather than patched.
    //
    // These two DELETEs used to read `WHERE datetime(timestamp) < datetime(?1)`, which was
    // wrong twice over. `datetime()` returns NULL for a value it cannot parse and `NULL < x`
    // is NULL, so any row with a malformed timestamp survived every retention pass forever,
    // silently and with nothing surfaced -- and wrapping the column defeated
    // `idx_predictions_ts`, so the prune full-scanned the largest tables in the database on
    // every startup.
    //
    // Both are gone, and neither is *fixed* so much as made unexpressible: the column is
    // INTEGER, the cutoff is an integer, and a plain `<` between them is a sargable predicate
    // that cannot return NULL. That is the whole argument for ADR-0007's Option A over the
    // one-sitting patch -- the class of defect leaves with the representation.
    PruneSummary summary;
    {
        Stmt stmt(db_, kPrunePredictionsSql);
        stmt.bind(1, cutoff_unix_ms);
        stmt.step_done();
        summary.predictions_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    {
        Stmt stmt(db_, kPruneContextSnapshotsSql);
        stmt.bind(1, cutoff_unix_ms);
        stmt.step_done();
        summary.context_snapshots_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    {
        // feature_snapshots is the highest-volume table in the schema -- one row per
        // prediction tick (~1/sec while active) with 31 REAL columns -- and for most of the
        // project's life it was never pruned at all, so it grew without bound while the other
        // two stayed flat. It used to need its own REAL seconds cutoff; as of schema v7 it
        // takes the same integer as the other two.
        Stmt stmt(db_, "DELETE FROM feature_snapshots WHERE timestamp < ?1");
        stmt.bind(1, cutoff_unix_ms);
        stmt.step_done();
        summary.feature_snapshots_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    return summary;
}

PruneSummary Storage::prune_to_retention(int retention_days) {
    Savepoint savepoint(*this, "prune_to_retention");
    const PruneSummary summary = prune_runtime_data(retention_cutoff_unix_ms(retention_days));
    savepoint.release();
    return summary;
}

void Storage::vacuum() {
    exec(db_, "VACUUM");
}

std::vector<std::string> Storage::index_names() {
    std::vector<std::string> names;
    Stmt stmt(db_,
              "SELECT name FROM sqlite_master WHERE type = 'index' AND name NOT LIKE "
              "'sqlite_autoindex%' ORDER BY name");
    while (stmt.step_row()) names.push_back(column_text(stmt.get(), 0));
    return names;
}

std::vector<std::string> Storage::query_plan(const std::string& sql) {
    std::vector<std::string> steps;
    Stmt stmt(db_, ("EXPLAIN QUERY PLAN " + sql).c_str());
    // Column 3 is the human-readable "detail" text ("SEARCH x USING INDEX y", "SCAN x").
    while (stmt.step_row()) steps.push_back(column_text(stmt.get(), 3));
    return steps;
}

}  // namespace snapback
