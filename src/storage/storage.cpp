#include "storage/storage.hpp"

#include <sqlite3.h>

#include <array>
#include <chrono>
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

SessionRecord read_session(sqlite3_stmt* stmt) {
    SessionRecord r;
    r.session_id = column_text(stmt, 0);
    r.goal = column_text(stmt, 1);
    r.status = column_text(stmt, 2);
    r.focus_mode = column_text(stmt, 3);
    r.started_at = column_opt_text(stmt, 4);
    r.ended_at = column_opt_text(stmt, 5);
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
    r.created_at = column_text(stmt, 4);
    r.updated_at = column_text(stmt, 5);
    return r;
}

ContextSnapshotDto read_context_snapshot(sqlite3_stmt* stmt) {
    ContextSnapshotDto s;
    s.app_name = column_text(stmt, 0);
    s.window_title = column_text(stmt, 1);
    s.file_hint = column_text(stmt, 2);
    s.project_hint = column_text(stmt, 3);
    s.summary = column_text(stmt, 4);
    s.timestamp = column_text(stmt, 5);
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
    p.timestamp = column_text(stmt, 7);
    return p;
}

std::string utc_now_rfc3339() {
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

double unix_now_secs() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
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

// Same instant as retention_cutoff_rfc3339, expressed the way feature_snapshots stores it.
double retention_cutoff_unix_secs(int retention_days) {
    return unix_now_secs() - 60.0 * 60.0 * 24.0 * static_cast<double>(retention_days);
}

std::string retention_cutoff_rfc3339(int retention_days) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto cutoff = now - hours(24) * retention_days;
    const std::time_t tt = system_clock::to_time_t(cutoff);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
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
    try {
        std::filesystem::create_directories(app_data_dir);
        sqlite3* db = nullptr;
        const auto db_path = (app_data_dir / "focoflow.db").string();
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return std::nullopt;
        }
        Storage storage(db);
        Logger local_logger(std::cerr);
        Logger& log = logger ? *logger : local_logger;
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
        storage.migrate();
        try {
            const auto cutoff = retention_cutoff_rfc3339(kDefaultRetentionDays);
            const PruneSummary summary = storage.prune_runtime_data(
                cutoff, retention_cutoff_unix_secs(kDefaultRetentionDays));
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
    } catch (...) {
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
        storage.migrate();
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

void Storage::migrate() {
    exec(db_,
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
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_predictions_session_ts
                ON predictions(session_id, timestamp DESC);

            CREATE TABLE IF NOT EXISTS labels (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                label INTEGER NOT NULL,
                source TEXT NOT NULL DEFAULT 'manual',
                notes TEXT,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE TABLE IF NOT EXISTS snapback_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

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
         )sql");
}

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
    Stmt stmt(db_,
              "SELECT session_id, goal, status, focus_mode, started_at, ended_at "
              "FROM sessions WHERE session_id = ?1");
    stmt.bind(1, session_id);
    if (!stmt.step_row()) return std::nullopt;
    return read_session(stmt.get());
}

SessionRecord Storage::create_session(const std::string& goal, FocusMode mode) {
    const std::string ended_at = utc_now_rfc3339();
    {
        Stmt close_active(db_,
                          "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 "
                          "WHERE status = 'ACTIVE'");
        close_active.bind(1, ended_at);
        close_active.step_done();
    }

    SessionRecord r;
    r.session_id = make_uuid_v4();
    r.goal = goal;
    r.status = "ACTIVE";
    r.focus_mode = focus_mode_to_string(mode);
    r.started_at = utc_now_rfc3339();

    Stmt insert(db_,
                "INSERT INTO sessions (session_id, goal, status, focus_mode, started_at) "
                "VALUES (?1, ?2, 'ACTIVE', ?3, ?4)");
    insert.bind(1, r.session_id);
    insert.bind(2, r.goal);
    insert.bind(3, r.focus_mode);
    insert.bind(4, *r.started_at);
    insert.step_done();
    return r;
}

void Storage::end_session(const std::string& session_id) {
    const std::string ended_at = utc_now_rfc3339();
    Stmt update(db_,
                "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 "
                "WHERE session_id = ?2 AND status = 'ACTIVE'");
    update.bind(1, ended_at);
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
    // Idempotent: stopping an already-completed session just returns it (mirrors Rust,
    // so a double-click on "stop" in the UI doesn't error).
    if (auto existing = get_session(session_id); existing && existing->status == "COMPLETED") {
        return *existing;
    }

    const std::string ended_at = utc_now_rfc3339();
    Stmt update(db_,
                "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 "
                "WHERE session_id = ?2 AND status = 'ACTIVE'");
    update.bind(1, ended_at);
    update.bind(2, session_id);
    update.step_done();
    if (sqlite3_changes(db_) == 0) {
        throw std::runtime_error("session not found");
    }
    return *get_session(session_id);
}

std::optional<SessionRecord> Storage::active_session() {
    Stmt stmt(db_,
              "SELECT session_id, goal, status, focus_mode, started_at, ended_at "
              "FROM sessions WHERE status = 'ACTIVE' ORDER BY started_at DESC LIMIT 1");
    if (!stmt.step_row()) return std::nullopt;
    return read_session(stmt.get());
}

std::vector<SessionRecord> Storage::recent_sessions(std::size_t limit) {
    Stmt stmt(db_,
              "SELECT session_id, goal, status, focus_mode, started_at, ended_at "
              "FROM sessions ORDER BY started_at DESC LIMIT ?1");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    std::vector<SessionRecord> rows;
    while (stmt.step_row()) rows.push_back(read_session(stmt.get()));
    return rows;
}

SessionRecap Storage::recap(const std::string& session_id) {
    SessionRecord session;
    {
        Stmt stmt(db_,
                  "SELECT session_id, goal, status, focus_mode, started_at, ended_at "
                  "FROM sessions WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (!stmt.step_row()) throw std::runtime_error("session not found");
        session = read_session(stmt.get());
    }

    SessionRecap out;
    out.session_id = session_id;
    out.goal = session.goal;

    {
        Stmt stmt(db_,
                  "SELECT CAST(MAX(0, (julianday(COALESCE(ended_at, CURRENT_TIMESTAMP)) - "
                  "julianday(started_at)) * 86400) AS INTEGER) "
                  "FROM sessions WHERE session_id = ?1");
        stmt.bind(1, session_id);
        if (stmt.step_row()) out.duration_secs = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 0));
    }

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
        Stmt stmt(db_,
                  "SELECT COUNT(*) FROM predictions WHERE session_id = ?1 "
                  "AND distraction_risk >= 0.7 AND focus_state = 'DISTRACTED'");
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
                       "drift_score, goal_alignment, timestamp) "
                       "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)"));
    stmt.bind(1, p.session_id);
    stmt.bind(2, p.focus_score);
    stmt.bind(3, p.distraction_risk);
    stmt.bind(4, p.focus_state);
    stmt.bind(5, p.thrash_score);
    stmt.bind(6, p.drift_score);
    stmt.bind(7, p.goal_alignment);
    stmt.bind(8, p.timestamp);
    stmt.step_done();
}

std::optional<PredictionRecord> Storage::latest_prediction() {
    Stmt stmt(db_,
              "SELECT session_id, focus_score, distraction_risk, focus_state, thrash_score, "
              "drift_score, goal_alignment, timestamp "
              "FROM predictions ORDER BY timestamp DESC LIMIT 1");
    if (!stmt.step_row()) return std::nullopt;
    return read_prediction(stmt.get());
}

std::vector<PredictionRecord> Storage::recent_predictions(std::size_t limit) {
    Stmt stmt(db_,
              "SELECT session_id, focus_score, distraction_risk, focus_state, thrash_score, "
              "drift_score, goal_alignment, timestamp "
              "FROM predictions ORDER BY timestamp DESC LIMIT ?1");
    stmt.bind(1, static_cast<std::int64_t>(limit));
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
    stmt.bind(2, unix_now_secs());
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
    stmt.bind(5, utc_now_rfc3339());
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
    const std::string now = utc_now_rfc3339();
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
    stmt.bind(4, now);
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
    stmt.bind(7, snap.timestamp);
    stmt.step_done();
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
        "WHERE fs.session_id = ?1 ORDER BY fs.timestamp ASC";

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
        "ORDER BY fs.timestamp ASC";

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
    }

    ExportTrainingResult result;
    result.output_dir = out_dir.string();
    result.features_path = (out_dir / "features.csv").string();
    result.labels_path = (out_dir / "labels.csv").string();
    result.feature_count = feature_count;
    result.label_count = label_count;
    return result;
}

PruneSummary Storage::prune_runtime_data(const std::string& cutoff_rfc3339,
                                         double cutoff_unix_secs) {
    PruneSummary summary;
    {
        Stmt stmt(db_,
                  "DELETE FROM predictions WHERE datetime(timestamp) < datetime(?1)");
        stmt.bind(1, cutoff_rfc3339);
        stmt.step_done();
        summary.predictions_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    {
        Stmt stmt(db_,
                  "DELETE FROM context_snapshots WHERE datetime(timestamp) < datetime(?1)");
        stmt.bind(1, cutoff_rfc3339);
        stmt.step_done();
        summary.context_snapshots_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    {
        // feature_snapshots is the highest-volume table in the schema — one row per
        // prediction tick (~1/sec while active) with 31 REAL columns — and until now it
        // was never pruned at all, so it grew without bound while the other two stayed
        // flat. Numeric comparison, not datetime(): this column is REAL epoch seconds.
        Stmt stmt(db_, "DELETE FROM feature_snapshots WHERE timestamp < ?1");
        stmt.bind(1, cutoff_unix_secs);
        stmt.step_done();
        summary.feature_snapshots_deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    }
    return summary;
}

void Storage::vacuum() {
    exec(db_, "VACUUM");
}

}  // namespace snapback
