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
    throw SqliteError(db ? sqlite3_errcode(db) : SQLITE_ERROR, std::string(action) + ": " + msg);
}

// Counting busy handler. Installing it replaces PRAGMA busy_timeout, so it reproduces SQLite's
// own back-off schedule (1, 2, 5, 10, 15, 20, 25, 25, 25, 50, 50, then 100 ms) up to
// kSqliteBusyTimeoutMs. 1 means retry; 0 hands the caller SQLITE_BUSY.
int counting_busy_handler(void* raw_stats, int attempts) {
    static constexpr int kDelaysMs[] = {1, 2, 5, 10, 15, 20, 25, 25, 25, 50, 50, 100};
    static constexpr int kElapsedMs[] = {0, 1, 3, 8, 18, 33, 53, 78, 103, 128, 178, 228};
    static constexpr int kSteps = static_cast<int>(sizeof(kDelaysMs) / sizeof(kDelaysMs[0]));

    auto* stats = static_cast<SqliteBusyStats*>(raw_stats);
    int delay_ms = 0;
    int elapsed_ms = 0;
    if (attempts < kSteps) {
        delay_ms = kDelaysMs[attempts];
        elapsed_ms = kElapsedMs[attempts];
    } else {
        // Past the table, SQLite keeps retrying at the last interval.
        delay_ms = kDelaysMs[kSteps - 1];
        elapsed_ms = kElapsedMs[kSteps - 1] + delay_ms * (attempts - (kSteps - 1));
    }
    if (elapsed_ms + delay_ms > kSqliteBusyTimeoutMs) {
        delay_ms = kSqliteBusyTimeoutMs - elapsed_ms;
    }
    if (stats) {
        stats->waits.fetch_add(1, std::memory_order_relaxed);
        const auto waited = static_cast<std::uint64_t>(std::max(0, elapsed_ms + delay_ms));
        auto seen = stats->max_wait_ms.load(std::memory_order_relaxed);
        while (waited > seen &&
               !stats->max_wait_ms.compare_exchange_weak(seen, waited,
                                                         std::memory_order_relaxed)) {
        }
    }
    if (delay_ms <= 0) {
        if (stats) stats->exhausted.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    sqlite3_sleep(delay_ms);
    return 1;
}

void exec(sqlite3* db, const char* sql) {
    char* raw_error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &raw_error) != SQLITE_OK) {
        std::string error = raw_error ? raw_error : sqlite3_errmsg(db);
        sqlite3_free(raw_error);
        throw SqliteError(sqlite3_errcode(db), error);
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

// An optional instant (ADR-0007): NULL means there is no such moment (a running session, an
// episode with no recorded start). Columns and DTOs are both epoch ms, so no conversion.
std::optional<std::int64_t> column_opt_ms(sqlite3_stmt* stmt, int column) {
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int64(stmt, column);
}

// The column list every `sessions` read shares. A query that needs more appends after it and
// reads from kSessionColumnCount onward.
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


// The one wall-clock reading, in epoch milliseconds (ADR-0007).
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
        // WAL + NORMAL: fsync at checkpoints rather than per commit. Crash-safe (a power loss
        // can lose the last few commits, never corrupt the file).
        exec(storage.db_, "PRAGMA journal_mode = WAL;");
        exec(storage.db_, "PRAGMA synchronous = NORMAL;");
        // Wait briefly on SQLITE_BUSY instead of failing the first contended BEGIN, and
        // count how often that happens. See kSqliteBusyTimeoutMs and install_busy_handler.
        storage.install_busy_handler();
        // Bigger page cache, in-memory temp tables, and memory-mapped I/O speed up the
        // read/export/recap queries and reduce checkpoint stalls. cache_size is negative =
        // kibibytes (~8 MB); mmap_size is bytes (256 MB).
        exec(storage.db_, "PRAGMA cache_size = -8000;");
        exec(storage.db_, "PRAGMA temp_store = MEMORY;");
        exec(storage.db_, "PRAGMA mmap_size = 268435456;");
        storage.migrate(db_path, &log);
        // Ordinary retention and space reclamation must not delay the first view.
        // Opening still completes migrations and required recovery synchronously.
        return storage;
    } catch (const std::exception& err) {
        // Log the cause: a corrupt DB, a permissions error, a failed migration, and a full disk
        // would otherwise all look like "the app will not start".
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
        storage.install_busy_handler();
        exec(storage.db_, "PRAGMA cache_size = -8000;");
        exec(storage.db_, "PRAGMA temp_store = MEMORY;");
        // Empty path: an in-memory database has no file to back up, and nothing to lose.
        storage.migrate({}, nullptr);
        return storage;
    } catch (...) {
        return std::nullopt;
    }
}

Storage Storage::open_read_only(const std::filesystem::path& db_path) {
    sqlite3* db = nullptr;
    const auto path = db_path.string();
    const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = "storage: could not open read-only database " + path;
        if (db) {
            message += ": ";
            message += sqlite3_errmsg(db);
            sqlite3_close(db);
        }
        throw std::runtime_error(message);
    }

    try {
        Storage storage(db);
        db = nullptr;  // storage owns it from here
        exec(storage.db_, "PRAGMA foreign_keys = ON;");
        exec(storage.db_, "PRAGMA cache_size = -8000;");
        exec(storage.db_, "PRAGMA temp_store = MEMORY;");
        exec(storage.db_, "PRAGMA mmap_size = 268435456;");
        exec(storage.db_, "PRAGMA query_only = ON;");
        return storage;
    } catch (...) {
        if (db) sqlite3_close(db);
        throw;
    }
}

std::filesystem::path Storage::database_path() const {
    const char* path = sqlite3_db_filename(db_, "main");
    return path ? std::filesystem::path(path) : std::filesystem::path{};
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
    // Best-effort rollback; a destructor must not throw. ROLLBACK TO does not pop the savepoint
    // -- the RELEASE does, and omitting it would hold the enclosing transaction open.
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
// Ordered and append-only. `version` is the user_version *after* the step runs. Each must be
// idempotent; see kSchemaVersion in storage.hpp.

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

// A separate step because a pre-versioning install already has `predictions`, so the baseline's
// CREATE TABLE IF NOT EXISTS would skip adding the column.
void migrate_prediction_model_id(sqlite3* db) { ensure_prediction_model_id_column(db); }

// ADR-0004: which rule decided focus_state. Nullable, no default, no backfill: NULL means
// "written before verdicts carried provenance".
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

// ADR-0005: active time is durable spans rather than an in-memory counter a crash loses. Not
// backfilled: older sessions report no active duration and callers fall back to elapsed.
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

// Distraction episodes. The UNIQUE (session_id, started_at) index makes the write idempotent
// under INSERT OR IGNORE. Columns are ALTERed in because pre-versioning installs already have
// `snapback_events`.
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

    // Legacy rows have NULL started_at; SQLite treats NULLs as distinct, so they coexist.
    exec(db,
         "CREATE UNIQUE INDEX IF NOT EXISTS idx_snapback_events_episode "
         "ON snapback_events(session_id, started_at)");
}

// Session reflection: two nullable columns, no default, no backfill (NULL means unanswered,
// which is also what Skip leaves). Kept off `labels`: a reflection is a note, not training
// data. ALTER because pre-versioning installs already have `sessions`.
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


// ADR-0007: every point in time becomes INTEGER UTC milliseconds, in one migration.
//
// All columns move together: close_dangling_session_span takes MAX over several tables, and
// SQLite sorts every INTEGER below every TEXT, so a half-migrated schema would silently pick
// the wrong value. Tables are rebuilt because a TEXT-affinity column would convert integers
// back to text on write. Storage::migrate turns foreign keys off around the upgrade and runs
// foreign_key_check before committing.
//
// Unparseable values become 0 (the epoch), so the next prune collects them. Genuine NULLs stay
// NULL: ended_at NULL means "still open", started_at NULL means "never recorded".
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

    // feature_snapshots held REAL epoch seconds; converted arithmetically.
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

    // The rebuilt tables lost their indexes; recreate them with the same names and column order
    // as migrate_baseline_schema (index_names() is asserted in tests).
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

void migrate_retention_timestamp_indexes(sqlite3* db) {
    exec(db, R"sql(
        CREATE INDEX IF NOT EXISTS idx_context_snapshots_ts
            ON context_snapshots(timestamp);
        CREATE INDEX IF NOT EXISTS idx_feature_snapshots_ts
            ON feature_snapshots(timestamp);
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
    {8, "retention timestamp indexes", migrate_retention_timestamp_indexes},
};

static_assert(std::size(kMigrations) > 0, "migration list must not be empty");
static_assert(kMigrations[std::size(kMigrations) - 1].version == kSchemaVersion,
              "kSchemaVersion must match the last migration; bump one when you add the other");

}  // namespace

std::string pre_migration_backup_name(int from_version) {
    return "focoflow.db.pre-v" + std::to_string(from_version) + ".bak";
}

namespace {

// True if the database has any user objects. Distinguishes a brand-new file (nothing to back
// up) from a pre-versioning install, since both have user_version 0.
bool has_user_data(sqlite3* db) {
    Stmt stmt(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
                  "AND name NOT LIKE 'sqlite_%'");
    return stmt.step_row() && sqlite3_column_int64(stmt.get(), 0) > 0;
}

// VACUUM INTO gives a consistent single-file snapshot even in WAL mode, where a plain file copy
// can be torn. It cannot run inside a transaction, hence before migrate() opens one.
void back_up_before_migration(sqlite3* db, const std::filesystem::path& db_path, int from,
                              Logger* logger) {
    const auto backup = db_path.parent_path() / pre_migration_backup_name(from);

    std::error_code ec;
    // VACUUM INTO refuses to overwrite. Removing first also means a previous, older backup
    // does not masquerade as a fresh one if the new write fails.
    std::filesystem::remove(backup, ec);

    // Bound parameter: a data directory can contain a quote.
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

    // A database from a newer build: refuse and leave it untouched. Our INSERTs could omit its
    // NOT NULL columns and our reads would ignore its data.
    if (from > kSchemaVersion) {
        throw std::runtime_error("database schema version " + std::to_string(from) +
                                 " is newer than this build understands (" +
                                 std::to_string(kSchemaVersion) +
                                 "); it was written by a later version of Snapback");
    }

    if (from == kSchemaVersion) return;  // already current; skip the DDL entirely

    // Back up before touching anything: the transaction covers a migration that fails, not one
    // that succeeds and is wrong. Not fatal -- a failed backup must not stop the app from
    // opening.
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

    // Foreign keys off for the duration: SQLite's documented procedure for table rebuilds (DROP
    // TABLE on a parent counts one violation per child row, which recreation does not clear).
    // Must be set before BEGIN. foreign_key_check below replaces the enforcement.
    struct ForeignKeyGuard {
        sqlite3* db;
        ~ForeignKeyGuard() {
            // Also runs on the failure path; must not throw.
            sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
        }
    } foreign_key_guard{db_};
    exec(db_, "PRAGMA foreign_keys = OFF;");

    // One transaction for the whole upgrade; DDL is transactional in SQLite.
    Transaction tx(*this);
    for (const Migration& migration : kMigrations) {
        if (migration.version <= from) continue;
        migration.apply(db_);
    }
    write_user_version(db_, kSchemaVersion);

    // Inside the transaction, so a broken reference rolls the upgrade back. Names the table.
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
    : db_(other.db_),
      stmt_cache_(std::move(other.stmt_cache_)),
      busy_(std::move(other.busy_)) {
    other.db_ = nullptr;
    other.stmt_cache_.clear();
}

Storage& Storage::operator=(Storage&& other) noexcept {
    if (this != &other) {
        finalize_cache();
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        stmt_cache_ = std::move(other.stmt_cache_);
        busy_ = std::move(other.busy_);
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
    // Closing the previous session and inserting the new one are one atomic step, so a failed
    // insert leaves the old session running. Savepoint because callers may already hold a
    // Transaction.
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
    // Close-then-open atomically, so a missed pause cannot leave overlapping spans.
    Savepoint savepoint(*this, "begin_session_span");
    // Checked before the close: closing a dangling span here would credit all downtime.
    if (!session_is_active(session_id)) return false;

    close_session_span(session_id, started_at_ms);

    // The INSERT re-checks ACTIVE, so a racing stop cannot leave an open span on a completed
    // session (which would grow forever).
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

std::int64_t Storage::session_span_timestamp_now(std::int64_t millis_ago) const {
    return unix_now_ms() - std::max<std::int64_t>(0, millis_ago);
}

bool Storage::close_session_span_now(const std::string& session_id, std::int64_t secs_ago) {
    // Same clock the rest of this file stamps with.
    return close_session_span(session_id, session_span_timestamp_now(secs_ago * 1000));
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
    // Last evidence of presence: MAX over per-table maxima, each an index probe.
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

    const std::int64_t ended_at_ms =
        last_seen && *last_seen > started_at_ms ? *last_seen : started_at_ms;
    close_session_span(session_id, ended_at_ms);
    savepoint.release();
    return ended_at_ms;
}

std::optional<std::uint64_t> Storage::active_secs(const std::string& session_id,
                                                  std::int64_t now_ms) {
    // ROUND per span, not CAST over the sum: julianday returns a double, and truncation loses a
    // second per span.
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

// Attended seconds inside one local-time window, clipped per span. Local time appears only when
// computing the window's calendar boundaries (ADR-0007); comparisons are on instants.
// 'localtime' resolves per instant, so DST needs no special case. The sum is ms, rounded once.
constexpr const char* kAttendedInWindowSql =
    "SELECT CAST(ROUND(COALESCE(SUM(MAX(0,"
    "  MIN(COALESCE(ended_at, ?1), :window_end)"
    "  - MAX(started_at, :window_start)"
    ")), 0) / 1000.0) AS INTEGER) FROM session_spans";

// A local calendar boundary as epoch ms: unixepoch -> localtime -> caller modifiers -> utc.
// Without the final `utc` it would compare a local reading against UTC instants.
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
    // ISO weeks start Monday. Step back six days, then `weekday 1`, so Monday itself stays put.
    return attended_in_window(
        db_, now_ms,
        local_boundary_ms(", 'start of day', '-6 days', 'weekday 1'"),
        local_boundary_ms(", 'start of day', '-6 days', 'weekday 1', '+7 days'"));
}

std::uint64_t Storage::attended_secs_since(std::int64_t now_ms,
                                           const std::optional<std::int64_t>& since_ms) {
    // Lower bound is an instant, so no calendar boundary is needed. Open spans end at `now`.
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
    // One UPDATE for both columns, including NULL, so clearing an answer is expressible. No
    // status check: reflections are editable after the session ends.
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
    // Re-read rather than trusting sqlite3_changes(), which is 0 for a no-op update.
    return get_session(session_id);
}

std::vector<SessionSummary> Storage::recent_session_summaries(
    std::size_t limit, const std::optional<std::int64_t>& started_after_ms) {
    const std::int64_t now_ms = unix_now_ms();
    // All three queries select "the newest `limit` sessions" independently, so they need a
    // total order: session_id breaks started_at ties (for stability, not chronology).

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

    // Query 2: recap()'s prediction aggregates in one pass, including the absolute 0.7
    // distraction bar (see recap() and ADR-0004).
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
                  "SUM(CASE WHEN p.distraction_risk >= 0.7 THEN 1 ELSE 0 END), COUNT(*) "
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
            recap.sample_count = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 5));
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

    // Query 4: attended time, grouped rather than one active_secs() call per session. Sessions
    // with no spans are absent, so their active_secs stays empty.
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

// Two spellings per query (with/without cutoff) rather than `(?N IS NULL OR ...)`, which is not
// sargable; see storage.hpp. ANALYZE does not fix it: sqlite_stat1 has no histogram.
std::string prediction_stats_sql(bool with_cutoff) {
    return std::string(
               "SELECT COUNT(*), COALESCE(AVG(focus_score), 0), "
               "COALESCE(MAX(focus_score), 0), "
               "COALESCE(SUM(CASE WHEN focus_state = 'DISTRACTED' THEN 1 ELSE 0 END), 0) "
               "FROM predictions") +
           (with_cutoff ? " WHERE timestamp >= ?1" : "");
}

std::string longest_focus_stretch_sql(bool with_cutoff) {
    return std::string(
               "WITH ordered AS ("
               "  SELECT session_id, ROW_NUMBER() OVER w AS rn,"
               "         (focus_state = 'DISTRACTED') AS distracted,"
               "         LAG(focus_state = 'DISTRACTED') OVER w AS prev_distracted,"
               "         CAST(ROUND((timestamp -"
               "              LAG(timestamp) OVER w) / 1000.0) AS INTEGER) AS gap"
               "  FROM predictions") +
           (with_cutoff ? " WHERE timestamp >= ?2" : "") +
           "  WINDOW w AS (PARTITION BY session_id ORDER BY timestamp ASC, id ASC)"
           "), marked AS ("
           "  SELECT session_id, rn, distracted, gap,"
           "         CASE WHEN distracted = 1 OR prev_distracted IS NULL"
           "                   OR prev_distracted = 1"
           "                   OR gap IS NULL OR gap < 0 OR gap > ?1"
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
           "  FROM grouped WHERE distracted = 0 GROUP BY session_id, run_id)";
}

std::string hourly_focus_buckets_sql(bool with_cutoff) {
    return std::string(
               "SELECT CAST(strftime('%H', timestamp / 1000.0, 'unixepoch', 'localtime')"
               "            AS INTEGER) AS hour,"
               "       COUNT(*), AVG(focus_score),"
               "       CAST(SUM(CASE WHEN focus_state = 'DISTRACTED' THEN 1 ELSE 0 END) AS REAL)"
               "         / COUNT(*) "
               "FROM predictions WHERE ") +
           (with_cutoff ? "timestamp >= ?1 AND " : "") +
           "      strftime('%H', timestamp / 1000.0, 'unixepoch', 'localtime')"
           "      IS NOT NULL "
           "GROUP BY hour ORDER BY hour ASC";
}

Storage::PredictionStats Storage::prediction_stats(const std::optional<std::int64_t>& cutoff_ms) {
    PredictionStats out;
    {
        Stmt stmt(db_, prediction_stats_sql(cutoff_ms.has_value()).c_str());
        if (cutoff_ms) stmt.bind(1, *cutoff_ms);
        if (stmt.step_row()) {
            out.sample_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
            out.avg_focus_score = sqlite3_column_double(stmt.get(), 1);
            out.peak_focus_score = sqlite3_column_double(stmt.get(), 2);
            out.distracted_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 3));
        }
    }

    // Longest unbroken focused stretch, as a duration (gaps and islands). A row starts a new
    // run when it is distracted, first, or more than the gap bound after the previous row (so
    // idle and private time break runs). A run's duration is the sum of gaps inside it.
    //
    // `prev_distracted` keeps the interval *entering* focus out of the run, and the partition
    // by session keeps two sessions' rows from interleaving; the parity test pins both.
    {
        Stmt stmt(db_, longest_focus_stretch_sql(cutoff_ms.has_value()).c_str());
        stmt.bind(1, static_cast<std::int64_t>(kFocusRunGapSecs));
        if (cutoff_ms) stmt.bind(2, *cutoff_ms);
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
    // 'localtime' uses the same C library conversion as local_hour_from_rfc3339, so DST
    // matches. Unparseable timestamps yield NULL and are dropped.
    Stmt stmt(db_, hourly_focus_buckets_sql(cutoff_ms.has_value()).c_str());
    if (cutoff_ms) stmt.bind(1, *cutoff_ms);
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

std::vector<FocusCurvePoint> Storage::session_focus_curve(const std::string& session_id,
                                                          std::size_t buckets) {
    if (buckets == 0) return {};
    // Integer arithmetic throughout: `(timestamp - lo) * n / (span + 1)` lands the first
    // prediction in slice 0 and the last in slice n - 1, and the +1 keeps a one-prediction
    // session from dividing by zero.
    Stmt stmt(db_,
              "WITH bounds AS ("
              "  SELECT MIN(timestamp) AS lo, MAX(timestamp) AS hi"
              "  FROM predictions WHERE session_id = ?1"
              ") "
              "SELECT (p.timestamp - b.lo) * ?2 / (b.hi - b.lo + 1) AS slice,"
              "       MIN(p.timestamp), COUNT(*), AVG(p.focus_score) "
              "FROM predictions p, bounds b "
              "WHERE p.session_id = ?1 AND b.lo IS NOT NULL "
              "GROUP BY slice ORDER BY slice ASC");
    stmt.bind(1, session_id);
    stmt.bind(2, static_cast<std::int64_t>(buckets));
    std::vector<FocusCurvePoint> curve;
    while (stmt.step_row()) {
        FocusCurvePoint point;
        point.start_ms = sqlite3_column_int64(stmt.get(), 1);
        point.sample_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 2));
        point.avg_focus_score = sqlite3_column_double(stmt.get(), 3);
        curve.push_back(point);
    }
    return curve;
}

std::vector<DailySummaryDay> Storage::daily_summary(std::int64_t now_ms, std::int64_t since_ms) {
    // Keyed by "YYYY-MM-DD", ascending. The queries below merge into it.
    std::map<std::string, DailySummaryDay> by_day;
    const auto bucket_for = [&by_day](sqlite3_stmt* stmt, int column) -> DailySummaryDay& {
        std::string day = column_text(stmt, column);
        auto& bucket = by_day[day];
        bucket.day = day;
        return bucket;
    };

    // Snap the cutoff down to local midnight once, so the first bucket is a whole day and every
    // query filters on the same instant.
    std::int64_t start_ms = since_ms;
    {
        Stmt stmt(db_,
                  "SELECT (strftime('%s', ?1 / 1000.0, 'unixepoch', 'localtime',"
                  " 'start of day', 'utc') * 1000)");
        stmt.bind(1, since_ms);
        if (stmt.step_row()) start_ms = sqlite3_column_int64(stmt.get(), 0);
    }
    if (start_ms >= now_ms) return {};

    // Query 1: attended seconds per day via a recursive local-day axis joined to spans, so a
    // span crossing midnight splits exactly. The axis steps through 'localtime' (DST-safe).
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

    // Query 2: volume and average per local date. Filters on the raw integer; the upper bound
    // keeps future-dated rows from extending the series.
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

    // Query 3: focused/deep seconds per day. A gap counts when both endpoint rows qualify and
    // it is within kFocusRunGapSecs; each gap lands on its later row's date. focused >= deep.
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
    // Fragments (under 60s wall clock with no predictions) stay in Session Explorer but do not
    // inflate Review aggregates.
    Stmt stmt(db_,
              "WITH recent AS ("
              "  SELECT session_id, status, started_at, ended_at FROM sessions"
              "  ORDER BY started_at DESC, session_id DESC LIMIT ?1"
              ") "
              "SELECT COUNT(*),"
              "       COALESCE(SUM(CASE WHEN status = 'COMPLETED'"
              "         AND NOT ("
              "           CAST(ROUND(MAX(0, (COALESCE(ended_at, (strftime('%s','now') * 1000))"
              "                - started_at) / 1000.0)) AS INTEGER) < 60"
              "           AND NOT EXISTS (SELECT 1 FROM predictions p"
              "                           WHERE p.session_id = recent.session_id)"
              "         ) THEN 1 ELSE 0 END), 0),"
              "       COALESCE(SUM(CASE WHEN status = 'COMPLETED'"
              "         AND NOT ("
              "           CAST(ROUND(MAX(0, (COALESCE(ended_at, (strftime('%s','now') * 1000))"
              "                - started_at) / 1000.0)) AS INTEGER) < 60"
              "           AND NOT EXISTS (SELECT 1 FROM predictions p"
              "                           WHERE p.session_id = recent.session_id)"
              "         ) THEN"
              "         CAST(ROUND(MAX(0, (COALESCE(ended_at, (strftime('%s','now') * 1000))"
              "              - started_at) / 1000.0)) AS INTEGER)"
              "         ELSE 0 END), 0),"
              // Whether LIMIT cut sessions the window would include, counted against the full
              // table.
              "       (SELECT COUNT(*) FROM sessions"
              "         WHERE started_at IS NOT NULL AND started_at >= ?2) > ?1 "
              "FROM recent WHERE started_at IS NOT NULL AND started_at >= ?2");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    stmt.bind(2, started_after_ms);
    SessionWindowTotals out;
    if (stmt.step_row()) {
        out.session_count = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
        out.completed_session_count =
            static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 1));
        out.focus_seconds = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 2));
        out.limit_reached = sqlite3_column_int64(stmt.get(), 3) != 0;
    }
    return out;
}

std::unordered_map<std::string, std::size_t> Storage::context_app_counts(
    std::size_t session_limit, std::size_t per_session_limit,
    const std::optional<std::int64_t>& started_after_ms) {
    // ROW_NUMBER keeps list_context_snapshots' per-session cap in SQL.
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
    // Explicit order rather than relying on ON DELETE CASCADE, which older databases lack.
    // app_rules is user configuration and stays.
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

    // Child rows first, as in delete_all_activity_data.
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
                  "NULLIF(COUNT(*), 0), 0), COUNT(*) "
                  "FROM predictions WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (stmt.step_row()) {
            out.avg_focus_score = sqlite3_column_double(stmt.get(), 0);
            out.avg_distraction_risk = sqlite3_column_double(stmt.get(), 1);
            out.deep_focus_pct = sqlite3_column_double(stmt.get(), 2);
            out.sample_count = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 3));
        }
    }

    {
        Stmt stmt(db_, "SELECT COUNT(*) FROM snapback_events WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (stmt.step_row()) out.snapback_count = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 0));
    }

    {
        // An absolute 0.7 "strong distraction" bar, deliberately not the mode's alerting
        // threshold, so Deep mode's sensitivity does not skew quality metrics. Risk alone
        // (ADR-0004).
        Stmt stmt(db_,
                  "SELECT COUNT(*) FROM predictions WHERE session_id = ?1 "
                  "AND distraction_risk >= 0.7");
        stmt.bind(1, session_id);
        if (stmt.step_row()) out.thrash_spikes = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 0));
    }

    return out;
}

std::optional<FocusLabel> Storage::infer_session_label(const SessionRecap& recap) {
    if (recap.sample_count == 0) return std::nullopt;
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

std::optional<FocusLabel> Storage::save_auto_session_label(const std::string& session_id) {
    // One automatic label per session: the labels table is append-only, so a duplicate could
    // never be cleaned up. The existing label wins; it is the one the user saw.
    if (const auto existing = session_auto_label(session_id)) return *existing;

    const SessionRecap session_recap = recap(session_id);
    const auto label = infer_session_label(session_recap);
    if (!label) return std::nullopt;
    insert_label(session_id, *label, label_source_as_str(LabelSource::Auto),
                 std::string("inferred from session recap"));
    return label;
}

std::optional<FocusLabel> Storage::session_auto_label(const std::string& session_id) {
    Stmt stmt(db_,
              "SELECT label FROM labels WHERE session_id = ?1 AND source = 'auto' "
              "ORDER BY id ASC LIMIT 1");
    stmt.bind(1, session_id);
    if (!stmt.step_row()) return std::nullopt;
    return static_cast<FocusLabel>(sqlite3_column_int(stmt.get(), 0));
}

std::optional<FocusLabel> Storage::session_rating(const std::string& session_id) {
    Stmt survey(db_,
                "SELECT label FROM labels WHERE session_id = ?1 AND source = 'survey' "
                "ORDER BY id DESC LIMIT 1");
    survey.bind(1, session_id);
    if (survey.step_row()) {
        return static_cast<FocusLabel>(sqlite3_column_int(survey.get(), 0));
    }
    return session_auto_label(session_id);
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
    const std::string sql = cutoff_ms
                          ? "SELECT session_id, focus_score, distraction_risk, focus_state, "
                            "thrash_score, drift_score, goal_alignment, timestamp, model_id, "
                            "state_source "
                            "FROM predictions WHERE timestamp >= ?1 "
                            "ORDER BY timestamp DESC"
                          : "SELECT session_id, focus_score, distraction_risk, focus_state, "
                            "thrash_score, drift_score, goal_alignment, timestamp, model_id, "
                            "state_source "
                            "FROM predictions ORDER BY timestamp DESC";
    Stmt stmt(db_, sql.c_str());
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
    // OR IGNORE against idx_snapback_events_episode: one statement, no check-then-insert race.
    Stmt stmt(db_, cached_stmt(
                       "INSERT OR IGNORE INTO snapback_events "
                       "(session_id, summary, timestamp, started_at, duration_secs, app_name, "
                       "file_hint) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)"));
    stmt.bind(1, episode.session_id);
    stmt.bind(2, episode.summary);
    stmt.bind(3, episode.ended_at_ms);
    // No start is written as NULL; the UNIQUE index treats NULLs as distinct.
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
    // By start where known, else by return time, so legacy rows still read chronologically.
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

std::optional<SnapbackEpisode> Storage::longest_snapback_episode(const std::string& session_id) {
    Stmt stmt(db_,
              "SELECT session_id, summary, timestamp, started_at, duration_secs, app_name, "
              "file_hint FROM snapback_events WHERE session_id = ?1 "
              "ORDER BY duration_secs DESC, COALESCE(started_at, timestamp) ASC, id ASC LIMIT 1");
    stmt.bind(1, session_id);
    if (!stmt.step_row()) return std::nullopt;
    SnapbackEpisode episode;
    episode.session_id = column_text(stmt.get(), 0);
    episode.summary = column_text(stmt.get(), 1);
    episode.ended_at_ms = sqlite3_column_int64(stmt.get(), 2);
    episode.started_at_ms = column_opt_ms(stmt.get(), 3);
    episode.duration_secs = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 4));
    episode.app_name = column_text(stmt.get(), 5);
    episode.file_hint = column_text(stmt.get(), 6);
    return episode;
}

Storage::EpisodePage Storage::snapback_episodes_after(
    const std::string& session_id, const std::optional<EpisodeCursor>& after,
    std::size_t limit) {
    // The old export used LIMIT 10000 with no continuation, silently dropping later
    // interruptions. Keyset pagination keeps equal timestamps distinct with the row id.
    Stmt stmt(db_,
              "SELECT session_id, summary, timestamp, started_at, duration_secs, app_name, "
              "file_hint, id, COALESCE(started_at, timestamp) "
              "FROM snapback_events WHERE session_id = ?1 "
              "AND (?2 IS NULL OR COALESCE(started_at, timestamp) > ?2 "
              "OR (COALESCE(started_at, timestamp) = ?2 AND id > ?3)) "
              "ORDER BY COALESCE(started_at, timestamp) ASC, id ASC LIMIT ?4");
    stmt.bind(1, session_id);
    if (after) {
        stmt.bind(2, after->sort_timestamp_ms);
        stmt.bind(3, after->id);
    } else {
        stmt.bind_null(2);
        stmt.bind_null(3);
    }
    stmt.bind(4, static_cast<std::int64_t>(limit));

    EpisodePage page;
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
        page.next.sort_timestamp_ms = sqlite3_column_int64(stmt.get(), 8);
        page.next.id = sqlite3_column_int64(stmt.get(), 7);
        page.rows.push_back(std::move(episode));
    }
    return page;
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
    // Row-value comparison: the keyset predicate in a form the index can serve.
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
    // A deferred savepoint establishes the snapshot on the first SELECT and retains it until
    // both files are complete. On the dedicated read-only WAL connection this gives the pair
    // one point-in-time view without blocking the engine writer.
    Savepoint snapshot(*this, "training_export_snapshot");
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
        // `fs.id` breaks ties between rows in the same millisecond (insertion order).
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
        // Check after writing: a full disk only sets badbit, and a short file must not be
        // reported as a complete export.
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
    snapshot.release();
    return result;
}

PruneSummary Storage::prune_runtime_data(std::int64_t cutoff_unix_ms) {
    // INTEGER column vs integer cutoff: a sargable `<` that cannot return NULL (ADR-0007).
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
        // feature_snapshots is the highest-volume table (about one row per second while
        // active).
        Stmt stmt(db_, kPruneFeatureSnapshotsSql);
        stmt.bind(1, cutoff_unix_ms);
        stmt.step_done();
        summary.feature_snapshots_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    return summary;
}

PruneBatchSummary Storage::prune_runtime_data_batch(std::int64_t cutoff_unix_ms,
                                                    std::size_t max_rows) {
    PruneBatchSummary summary;
    if (max_rows == 0) return summary;

    Savepoint savepoint(*this, "prune_runtime_data_batch");
    auto delete_up_to = [&](const char* sql, std::size_t& deleted) {
        const std::size_t remaining = max_rows - summary.total();
        if (remaining == 0) return;
        Stmt stmt(db_, sql);
        stmt.bind(1, cutoff_unix_ms);
        stmt.bind(2, static_cast<std::int64_t>(remaining));
        stmt.step_done();
        deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    };

    delete_up_to(kPrunePredictionsBatchSql, summary.predictions_deleted);
    delete_up_to(kPruneContextSnapshotsBatchSql, summary.context_snapshots_deleted);
    delete_up_to(kPruneFeatureSnapshotsBatchSql, summary.feature_snapshots_deleted);
    summary.has_more = summary.total() == max_rows;
    savepoint.release();
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

void Storage::install_busy_handler() {
    if (!busy_) busy_ = std::make_unique<SqliteBusyStats>();
    sqlite3_busy_handler(db_, counting_busy_handler, busy_.get());
}

SqliteBusySnapshot Storage::busy_stats() const {
    SqliteBusySnapshot out;
    if (!busy_) return out;
    out.waits = busy_->waits.load(std::memory_order_relaxed);
    out.exhausted = busy_->exhausted.load(std::memory_order_relaxed);
    out.max_wait_ms = busy_->max_wait_ms.load(std::memory_order_relaxed);
    return out;
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
