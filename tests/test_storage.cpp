#include "doctest_wrapper.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <sqlite3.h>

#include "storage/storage.hpp"
#include "util/logger.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_storage_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

PredictionRecord prediction(const std::string& session_id, double focus, double risk,
                            const std::string& state) {
    PredictionRecord p;
    p.session_id = session_id;
    p.focus_score = focus;
    p.distraction_risk = risk;
    p.focus_state = state;
    p.thrash_score = risk >= 0.7 ? 1.0 : 0.0;
    p.drift_score = 0.1;
    p.goal_alignment = 0.6;
    p.timestamp = "2026-07-11T19:00:00Z";
    p.model_id = "test:model-v1";
    return p;
}

}  // namespace

TEST_CASE("Storage::open creates focoflow.db and migrates the schema") {
    TempDir temp;

    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());

    CHECK(std::filesystem::exists(temp.path / "focoflow.db"));
    CHECK(storage->active_session() == std::nullopt);
}

TEST_CASE("Storage::open runs the DB in WAL mode") {
    TempDir temp;
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());

    // A write in WAL mode creates the "-wal" sidecar next to the DB; its presence is a
    // dependency-free proof that journal_mode=WAL took effect (vs the default rollback
    // journal). WAL + synchronous=NORMAL is what drops per-tick write latency ~50x.
    storage->create_session("wal check", FocusMode::Normal);
    CHECK(std::filesystem::exists(temp.path / "focoflow.db-wal"));
}

TEST_CASE("storage session lifecycle keeps only one active session") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto first = storage->create_session("First goal", FocusMode::Normal);
    CHECK(first.status == "ACTIVE");
    CHECK(first.focus_mode == "normal");
    CHECK(first.started_at.has_value());
    CHECK(first.ended_at == std::nullopt);

    auto second = storage->create_session("Second goal", FocusMode::Deep);
    auto active = storage->active_session();
    REQUIRE(active.has_value());
    CHECK(active->session_id == second.session_id);
    CHECK(active->goal == "Second goal");
    CHECK(active->focus_mode == "deep");

    storage->end_session(second.session_id);
    CHECK(storage->active_session() == std::nullopt);
}

TEST_CASE("storage keeps the previous session active when the replacement insert fails") {
    // Roadmap 7.20. create_session closes the running session and inserts its replacement.
    // Before those two statements shared a transaction, a failing insert left the user with
    // no active session at all -- the outcome neither the caller nor the user ever asks for.
    TempDir temp;
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());

    const auto first = storage->create_session("First goal", FocusMode::Normal);

    // Deterministic failure seam. session_id is a random UUID, so no constraint fixture can
    // predict a collision; a BEFORE INSERT trigger is the one thing that fails the second
    // statement on demand. Installed from a separate connection so the code under test stays
    // exactly what ships -- no production test hook exists for this, and none should.
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db,
                             "CREATE TRIGGER block_session_insert BEFORE INSERT ON sessions "
                             "BEGIN SELECT RAISE(ABORT, 'insert blocked'); END;",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_close(db) == SQLITE_OK);
    }

    CHECK_THROWS_AS(storage->create_session("Second goal", FocusMode::Deep),
                    std::runtime_error);

    // The rollback must restore the original session, not merely avoid inserting.
    const auto active = storage->active_session();
    REQUIRE(active.has_value());
    CHECK(active->session_id == first.session_id);
    CHECK(active->goal == "First goal");
    CHECK(active->status == "ACTIVE");
    CHECK(active->ended_at == std::nullopt);

    // And nothing half-written survived: no COMPLETED first session, no orphan replacement.
    CHECK(storage->recent_sessions(10).size() == 1);
}

TEST_CASE("a failed session replacement rolls back itself, not the caller's transaction") {
    // create_session uses a SAVEPOINT rather than a Transaction because callers already
    // wrap it -- LargeFixture::seed creates sixty sessions inside one outer transaction, and
    // a nested BEGIN there throws "cannot start a transaction within a transaction". This
    // pins both halves of that: the inner failure undoes only its own work, and the caller's
    // transaction survives it intact.
    TempDir temp;
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());

    const auto first = storage->create_session("First goal", FocusMode::Normal);

    // Install the trigger before opening the outer transaction: BEGIN IMMEDIATE takes the
    // write lock, and a second connection cannot alter the schema while it is held.
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db,
                             "CREATE TRIGGER block_session_insert BEFORE INSERT ON sessions "
                             "BEGIN SELECT RAISE(ABORT, 'insert blocked'); END;",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_close(db) == SQLITE_OK);
    }

    {
        Storage::Transaction outer(*storage);
        CHECK_THROWS_AS(storage->create_session("Second goal", FocusMode::Deep),
                        std::runtime_error);
        // The outer transaction is still open and committable. If create_session had used a
        // Transaction, this line would never be reached -- the constructor would have thrown.
        CHECK_NOTHROW(outer.commit());
    }

    const auto active = storage->active_session();
    REQUIRE(active.has_value());
    CHECK(active->session_id == first.session_id);
    CHECK(active->status == "ACTIVE");
}

TEST_CASE("storage gates prediction and feature writes to active sessions") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    CHECK_THROWS_AS(storage->insert_prediction(prediction("missing", 50.0, 0.2, "PRODUCTIVE")),
                    std::runtime_error);

    auto session = storage->create_session("Ship storage", FocusMode::Normal);
    CHECK_NOTHROW(storage->insert_prediction(
        prediction(session.session_id, 75.0, 0.2, "PRODUCTIVE")));

    FeatureVector f;
    f.seconds_since_session_start() = 120.0;
    f.keystroke_rate() = 2.5;
    f.is_ide() = 1.0;
    f.focus_momentum() = 0.75;
    CHECK_NOTHROW(storage->insert_feature_snapshot(session.session_id, f));

    storage->end_session(session.session_id);
    CHECK_THROWS_AS(storage->insert_prediction(
                        prediction(session.session_id, 60.0, 0.3, "PRODUCTIVE")),
                    std::runtime_error);
    CHECK_THROWS_AS(storage->insert_feature_snapshot(session.session_id, f),
                    std::runtime_error);
}

TEST_CASE("delete all activity data preserves user configuration") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto session = storage->create_session("Private work", FocusMode::Normal);
    storage->insert_prediction(prediction(session.session_id, 75.0, 0.2, "PRODUCTIVE"));
    FeatureVector features;
    storage->insert_feature_snapshot(session.session_id, features);
    storage->insert_label(session.session_id, FocusLabel::Productive, "manual");
    ContextSnapshotDto context;
    context.app_name = "Editor";
    context.window_title = "private.txt";
    context.timestamp = "2026-07-11T19:00:00Z";
    storage->save_context_snapshot(session.session_id, context);
    storage->upsert_app_rule("Editor", AppRuleKind::Allow, std::nullopt);

    storage->delete_all_activity_data();

    CHECK(storage->active_session() == std::nullopt);
    CHECK(storage->recent_sessions(10).empty());
    CHECK(storage->recent_predictions(10).empty());
    CHECK(storage->list_context_snapshots(session.session_id, 10).empty());
    REQUIRE(storage->list_app_rules().size() == 1);

    TempDir temp;
    const auto exported = storage->export_training_csv(temp.path);
    CHECK(exported.feature_count == 0);
    CHECK(exported.label_count == 0);
}

TEST_CASE("storage persists prediction model identity and verdict provenance") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("Model identity", FocusMode::Normal);

    auto original = prediction(session.session_id, 75.0, 0.2, "PRODUCTIVE");
    original.model_id = "onnx:snapback-features-v1-31:abc123";
    original.state_source = "block";
    storage->insert_prediction(original);

    const auto latest = storage->latest_prediction();
    REQUIRE(latest.has_value());
    CHECK(latest->model_id == original.model_id);
    CHECK(latest->state_source == original.state_source);
}

TEST_CASE("session spans accumulate only attended time") {
    // Roadmap 7.23 / ADR-0005. Two 10-minute spans an hour apart: elapsed is ~2h, attended
    // is 20 minutes. The gap is never counted rather than counted and later subtracted.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("attended", FocusMode::Normal);

    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");
    REQUIRE(storage->close_session_span(session.session_id, "2026-08-05T09:10:00Z"));
    storage->begin_session_span(session.session_id, "2026-08-05T10:00:00Z");
    REQUIRE(storage->close_session_span(session.session_id, "2026-08-05T10:10:00Z"));

    const auto active = storage->active_secs(session.session_id, "2026-08-05T11:00:00Z");
    REQUIRE(active.has_value());
    CHECK(*active == 20 * 60);
    CHECK_FALSE(storage->has_open_span(session.session_id));
}

TEST_CASE("an open span counts up to now") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("in progress", FocusMode::Normal);

    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");
    CHECK(storage->has_open_span(session.session_id));

    const auto active = storage->active_secs(session.session_id, "2026-08-05T09:30:00Z");
    REQUIRE(active.has_value());
    CHECK(*active == 30 * 60);
}

TEST_CASE("a session with no spans reports no active time, not zero") {
    // The distinction matters: nullopt means "never measured" so a caller falls back to
    // elapsed. Returning 0 would tell every session that predates this table that the user
    // was present for none of it -- a fabricated number dressed as a measurement.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("unmeasured", FocusMode::Normal);

    CHECK_FALSE(storage->active_secs(session.session_id, "2026-08-05T09:00:00Z").has_value());
    CHECK_FALSE(storage->has_open_span(session.session_id));
}

TEST_CASE("reopening a span closes the previous one instead of overlapping it") {
    // A missed pause -- a crash, a dropped idle edge -- must not leave two open spans
    // double-counting the same minutes, because nothing later could tell which was wrong.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("overlap", FocusMode::Normal);

    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");
    storage->begin_session_span(session.session_id, "2026-08-05T09:30:00Z");  // no close between

    const auto active = storage->active_secs(session.session_id, "2026-08-05T10:00:00Z");
    REQUIRE(active.has_value());
    // 09:00-09:30 closed by the reopen, plus 09:30-10:00 still open. Sixty minutes, not
    // ninety -- which is what overlapping spans would have produced.
    CHECK(*active == 60 * 60);
}

TEST_CASE("closing a span twice is not an error and does not extend it") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("idempotent", FocusMode::Normal);

    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");
    CHECK(storage->close_session_span(session.session_id, "2026-08-05T09:10:00Z"));
    // Already paused: nothing open, so nothing to close. An ordinary outcome.
    CHECK_FALSE(storage->close_session_span(session.session_id, "2026-08-05T09:20:00Z"));

    const auto active = storage->active_secs(session.session_id, "2026-08-05T10:00:00Z");
    REQUIRE(active.has_value());
    CHECK(*active == 10 * 60);
}

TEST_CASE("a backwards clock cannot make a span subtract time") {
    // DST and NTP corrections move wall clock backwards. A span ending before it began would
    // contribute a negative to the SUM and silently reduce the total.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("time travel", FocusMode::Normal);

    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");
    REQUIRE(storage->close_session_span(session.session_id, "2026-08-05T08:00:00Z"));

    const auto active = storage->active_secs(session.session_id, "2026-08-05T10:00:00Z");
    REQUIRE(active.has_value());
    CHECK(*active == 0);
}

TEST_CASE("deleting a session deletes its spans") {
    // delete_session enumerates child tables explicitly rather than trusting ON DELETE
    // CASCADE, because historical databases were not all created with it. A new child table
    // that is not added to that list leaves orphans.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("doomed", FocusMode::Normal);
    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");
    storage->close_session_span(session.session_id, "2026-08-05T09:10:00Z");

    REQUIRE(storage->delete_session(session.session_id));
    CHECK_FALSE(storage->active_secs(session.session_id, "2026-08-05T10:00:00Z").has_value());
}

TEST_CASE("deleting all activity deletes spans too") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("wiped", FocusMode::Normal);
    storage->begin_session_span(session.session_id, "2026-08-05T09:00:00Z");

    storage->delete_all_activity_data();
    CHECK_FALSE(storage->active_secs(session.session_id, "2026-08-05T10:00:00Z").has_value());
}

TEST_CASE("migrating an existing database backs it up first") {
    // Roadmap 7.22. The transaction in migrate() already covers a migration that *fails*.
    // This covers the case it cannot: one that succeeds and is wrong. The backup is the only
    // recovery path, so it must exist, carry the pre-migration data, and be openable.
    TempDir temp;
    const auto db_file = temp.path / "focoflow.db";
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(db_file.string().c_str(), &db) == SQLITE_OK);
        const char* legacy =
            "CREATE TABLE sessions (session_id TEXT PRIMARY KEY, goal TEXT NOT NULL, "
            "status TEXT NOT NULL, focus_mode TEXT NOT NULL, started_at TEXT NOT NULL, "
            "ended_at TEXT);"
            "INSERT INTO sessions VALUES ('keepme', 'irreplaceable work', 'COMPLETED', "
            "'normal', '2026-07-11T19:00:00Z', '2026-07-11T20:00:00Z');"
            "PRAGMA user_version = 0;";
        REQUIRE(sqlite3_exec(db, legacy, nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_close(db) == SQLITE_OK);
    }

    { auto storage = Storage::open(temp.path); REQUIRE(storage.has_value()); }

    // Named for the version it came *from*, so two upgrades leave distinguishable files.
    const auto backup = temp.path / pre_migration_backup_name(0);
    REQUIRE(std::filesystem::exists(backup));

    // It must be a real database holding the pre-migration row, not an empty or torn file.
    sqlite3* restored = nullptr;
    REQUIRE(sqlite3_open(backup.string().c_str(), &restored) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(restored, "SELECT goal FROM sessions WHERE session_id='keepme'",
                               -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) ==
          "irreplaceable work");
    sqlite3_finalize(stmt);
    sqlite3_close(restored);
}

TEST_CASE("a brand-new database is not backed up") {
    // Version 0 means both "new file" and "pre-versioning install" and cannot be told apart
    // after the fact, so the presence of a user table is what distinguishes them. Backing up
    // an empty file on every first run would be noise, and noise is what makes a real backup
    // message easy to miss.
    TempDir temp;
    { auto storage = Storage::open(temp.path); REQUIRE(storage.has_value()); }

    CHECK_FALSE(std::filesystem::exists(temp.path / pre_migration_backup_name(0)));
    CHECK_FALSE(std::filesystem::exists(temp.path / pre_migration_backup_name(kSchemaVersion)));
}

TEST_CASE("reopening an up-to-date database writes no new backup") {
    // migrate() early-returns when the version already matches, so the backup must not be
    // paid on every launch -- only when a migration is actually about to run.
    TempDir temp;
    { auto storage = Storage::open(temp.path); REQUIRE(storage.has_value()); }
    { auto storage = Storage::open(temp.path); REQUIRE(storage.has_value()); }

    for (const auto& entry : std::filesystem::directory_iterator(temp.path)) {
        CHECK(entry.path().filename().string().find(".bak") == std::string::npos);
    }
}

TEST_CASE("a failed backup does not stop the database from opening") {
    // Deliberate: refusing to start because a backup failed turns a disk-space problem into
    // "the app will not open", which is worse than the risk it guards against -- the
    // migration is still transactional either way.
    //
    // The failure seam is a **non-empty** directory where the backup file belongs. An empty
    // one is not enough: back_up_before_migration calls std::filesystem::remove first (so a
    // stale backup cannot masquerade as a fresh one), and remove() deletes empty directories
    // -- which silently cleared the seam and let the backup succeed, making the first version
    // of this test assert nothing.
    TempDir temp;
    const auto db_file = temp.path / "focoflow.db";
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(db_file.string().c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db,
                             "CREATE TABLE sessions (session_id TEXT PRIMARY KEY, "
                             "goal TEXT NOT NULL, status TEXT NOT NULL, focus_mode TEXT NOT NULL, "
                             "started_at TEXT NOT NULL, ended_at TEXT);"
                             "PRAGMA user_version = 0;",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_close(db) == SQLITE_OK);
    }
    const auto blocked = temp.path / pre_migration_backup_name(0);
    std::filesystem::create_directories(blocked);
    { std::ofstream occupied(blocked / "occupied.txt"); occupied << "blocks remove()"; }

    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());
    // The backup really did fail: the path is still the directory, not a database file.
    REQUIRE(std::filesystem::is_directory(blocked));
    // And the migration still happened despite it.
    CHECK_NOTHROW(storage->create_session("after a failed backup", FocusMode::Normal));
}

TEST_CASE("storage upgrades legacy predictions with heuristic model identity") {
    TempDir temp;
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const char* legacy_schema =
        "CREATE TABLE sessions (session_id TEXT PRIMARY KEY, goal TEXT NOT NULL, "
        "status TEXT NOT NULL, focus_mode TEXT NOT NULL, started_at TEXT NOT NULL, ended_at TEXT);"
        "CREATE TABLE predictions (id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT NOT NULL, "
        "focus_score REAL NOT NULL, distraction_risk REAL NOT NULL, focus_state TEXT NOT NULL, "
        "thrash_score REAL NOT NULL DEFAULT 0.0, drift_score REAL NOT NULL DEFAULT 0.0, "
        "goal_alignment REAL NOT NULL DEFAULT 0.5, timestamp TEXT NOT NULL);"
        "INSERT INTO sessions VALUES ('legacy', 'legacy', 'ACTIVE', 'normal', "
        "'2026-07-11T19:00:00Z', NULL);"
        "INSERT INTO predictions (session_id, focus_score, distraction_risk, focus_state, "
        "timestamp) VALUES ('legacy', 50.0, 0.5, 'PRODUCTIVE', '2026-07-11T19:00:00Z');";
    char* error = nullptr;
    REQUIRE(sqlite3_exec(db, legacy_schema, nullptr, nullptr, &error) == SQLITE_OK);
    if (error) sqlite3_free(error);
    sqlite3_close(db);

    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());
    const auto latest = storage->latest_prediction();
    REQUIRE(latest.has_value());
    CHECK(latest->model_id == "heuristic:snapback-features-v1-31");
    // Verdict provenance did not exist when this row was written and cannot be invented
    // after the fact (ADR-0004): the legacy row reads back NULL, not "model".
    CHECK_FALSE(latest->state_source.has_value());

    // A row written by this build carries its provenance through the migrated file.
    auto stamped = prediction("legacy", 60.0, 0.3, "DISTRACTED");
    stamped.timestamp = "2026-07-11T20:00:00Z";
    stamped.state_source = "risk";
    storage->insert_prediction(stamped);
    const auto newest = storage->latest_prediction();
    REQUIRE(newest.has_value());
    CHECK(newest->state_source == std::optional<std::string>("risk"));
}

// --- Schema versioning (Roadmap 7.3) -----------------------------------------------------

TEST_CASE("a fresh database is stamped with the current schema version") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    CHECK(storage->schema_version() == kSchemaVersion);
}

TEST_CASE("Storage::open stamps a schema version on disk and keeps it across reopens") {
    TempDir temp;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        CHECK(storage->schema_version() == kSchemaVersion);
    }
    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);
}

TEST_CASE("an unversioned database with a full schema is adopted, not rebuilt") {
    // The case every existing install is in: `user_version` is 0 because versioning did not
    // exist when the file was written, but the schema is already complete. The runner cannot
    // distinguish this from a brand-new file, so it replays every migration — which is only
    // safe because each one is idempotent. What must survive is the data.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("pre-versioning", FocusMode::Normal).session_id;
    }

    // Rewind the stamp to simulate a database written before this commit existed.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "PRAGMA user_version = 0;", nullptr, nullptr, nullptr) ==
            SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);
    // The session is still there — replaying the baseline did not drop or recreate a table.
    CHECK(reopened->get_session(session_id).has_value());
}

TEST_CASE("a legacy database is migrated and then stamped current") {
    // The 7.3 gap in one test: an on-disk database from an older schema (no model_id, no
    // user_version) must come out of open() both upgraded *and* versioned, so the next
    // launch takes the fast path instead of replaying DDL forever.
    TempDir temp;
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const char* legacy_schema =
        "CREATE TABLE sessions (session_id TEXT PRIMARY KEY, goal TEXT NOT NULL, "
        "status TEXT NOT NULL, focus_mode TEXT NOT NULL, started_at TEXT NOT NULL, ended_at TEXT);"
        "CREATE TABLE predictions (id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT NOT NULL, "
        "focus_score REAL NOT NULL, distraction_risk REAL NOT NULL, focus_state TEXT NOT NULL, "
        "thrash_score REAL NOT NULL DEFAULT 0.0, drift_score REAL NOT NULL DEFAULT 0.0, "
        "goal_alignment REAL NOT NULL DEFAULT 0.5, timestamp TEXT NOT NULL);"
        "INSERT INTO sessions VALUES ('legacy', 'legacy', 'ACTIVE', 'normal', "
        "'2026-07-11T19:00:00Z', NULL);";
    REQUIRE(sqlite3_exec(db, legacy_schema, nullptr, nullptr, nullptr) == SQLITE_OK);
    // A legacy file carries no stamp at all — user_version stays at SQLite's default 0.
    sqlite3_close(db);

    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());
    CHECK(storage->schema_version() == kSchemaVersion);
    // Tables the legacy schema never had must exist now.
    CHECK(storage->list_app_rules().empty());
    CHECK(storage->get_session("legacy").has_value());
}

TEST_CASE("a database from a newer build is refused instead of opened") {
    // The failure this prevents is silent and one-directional: a later Snapback could add a
    // NOT NULL column, and this build's INSERTs — which know nothing about it — would either
    // fail at runtime or write rows the newer build considers malformed. Downgrading is rare;
    // corrupting the user's history because of it is unacceptable, so open() fails closed.
    TempDir temp;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
    }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const auto bump = "PRAGMA user_version = " + std::to_string(kSchemaVersion + 1) + ";";
    REQUIRE(sqlite3_exec(db, bump.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info);
    auto storage = Storage::open(temp.path, &logger);

    CHECK_FALSE(storage.has_value());
    // Refusing is only useful if the user can tell *why*; a bare nullopt reads as "the app
    // is broken" rather than "you downgraded".
    const auto logged = log_out.str();
    CHECK(logged.find("newer than this build") != std::string::npos);
}

TEST_CASE("refusing a newer database leaves it untouched") {
    // The whole point of failing closed is that re-upgrading recovers the user's data. If
    // the refused open had rewritten the stamp or dropped a table, that would be false.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("from the future", FocusMode::Normal).session_id;
    }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const auto bump = "PRAGMA user_version = " + std::to_string(kSchemaVersion + 5) + ";";
    REQUIRE(sqlite3_exec(db, bump.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    REQUIRE_FALSE(Storage::open(temp.path).has_value());

    // Put the stamp back the way a newer build would have left it after a downgrade+upgrade
    // cycle, and the data must still be there.
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const auto restore = "PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";";
    REQUIRE(sqlite3_exec(db, restore.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto recovered = Storage::open(temp.path);
    REQUIRE(recovered.has_value());
    CHECK(recovered->get_session(session_id).has_value());
}

// --- Pre-existing database fixtures (Roadmap 7.11) ---------------------------------------
//
// Every other test in this file starts from a database this build just created, which means
// they all agree with themselves by construction. These start from a file some *other*
// process left behind — the only shape that matters in the field, because the stable
// focoflow.db filename means earlier installs' data is picked up.
//
// The fixtures are built in-process rather than committed as binary .db files on purpose: a
// checked-in database cannot be code-reviewed, and it silently stops representing "what an
// old build wrote" the moment someone regenerates it from a current build.

namespace {

// Copies a live database *and its WAL sidecars* to a new directory. Copying while the
// original connection is still open is what makes the copy dirty: the committed rows are in
// the -wal file and have not been checkpointed back into the main database yet, which is
// exactly the on-disk state a killed process leaves behind.
void copy_db_with_wal(const std::filesystem::path& from_dir,
                      const std::filesystem::path& to_dir) {
    std::filesystem::create_directories(to_dir);
    for (const char* suffix : {"", "-wal", "-shm"}) {
        const auto src = from_dir / ("focoflow.db" + std::string(suffix));
        if (!std::filesystem::exists(src)) continue;
        std::filesystem::copy_file(src, to_dir / src.filename(),
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

}  // namespace

TEST_CASE("committed rows survive an unclean shutdown via WAL recovery") {
    // For an always-on tray app that users quit with Force Quit or Task Manager, this *is*
    // the normal shutdown path. Nothing tested it, so a change to journal_mode or a
    // checkpoint-on-close could have started discarding the last writes of every session
    // without a single test noticing.
    TempDir live;
    TempDir crashed;
    std::string session_id;
    {
        auto storage = Storage::open(live.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("unclean shutdown", FocusMode::Normal).session_id;
        storage->insert_prediction(prediction(session_id, 71.0, 0.2, "PRODUCTIVE"));

        // Snapshot mid-life: the connection is still open, so the WAL is still dirty.
        REQUIRE(std::filesystem::exists(live.path / "focoflow.db-wal"));
        copy_db_with_wal(live.path, crashed.path);
    }

    auto recovered = Storage::open(crashed.path);
    REQUIRE(recovered.has_value());
    CHECK(recovered->get_session(session_id).has_value());
    const auto predictions = recovered->recent_predictions(10);
    REQUIRE(predictions.size() == 1);
    CHECK(predictions[0].focus_score == doctest::Approx(71.0));
}

TEST_CASE("a corrupt database is refused with a logged reason, not a crash") {
    // A truncated or overwritten file is what a full disk or a bad sync client leaves. The
    // requirement is not that we repair it — it is that the app says so instead of dying or
    // silently starting with an empty history.
    TempDir temp;
    {
        std::ofstream junk(temp.path / "focoflow.db", std::ios::binary);
        junk << "this is definitely not a SQLite database, not even a little bit";
    }

    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info);
    auto storage = Storage::open(temp.path, &logger);

    CHECK_FALSE(storage.has_value());
    const auto logged = log_out.str();
    CHECK(logged.find("ERROR") != std::string::npos);
    CHECK(logged.find("focoflow.db") != std::string::npos);
}

TEST_CASE("an aged database is pruned on open") {
    // Retention has only ever been exercised against rows inserted moments earlier in the
    // same process. This is the real shape: a file that sat on disk long enough for its
    // contents to age past the window, opened fresh.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("aged", FocusMode::Normal).session_id;
    }

    // Backdate a prediction well past kDefaultRetentionDays by writing it directly, the way
    // an install from months ago would have left it.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const auto insert =
        "INSERT INTO predictions (session_id, focus_score, distraction_risk, focus_state, "
        "timestamp) VALUES ('" + session_id + "', 40.0, 0.4, 'PRODUCTIVE', '2020-01-01T00:00:00Z');";
    REQUIRE(sqlite3_exec(db, insert.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->recent_predictions(10).empty());
    // Pruning runtime rows must not take the session with it — sessions are the user's
    // history, predictions are regenerable telemetry.
    CHECK(reopened->get_session(session_id).has_value());
}

TEST_CASE("a database carrying unknown tables and columns still opens") {
    // "Foreign-authored" in practice means a file a newer build, a migration we later revert,
    // or a user's own sqlite3 session has added things to. A superset must be tolerated:
    // every write in this codebase names its columns, so extra ones are simply not our
    // business.
    TempDir temp;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
    }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const char* foreign_objects =
        "CREATE TABLE someone_elses_table (id INTEGER PRIMARY KEY, note TEXT);"
        "ALTER TABLE sessions ADD COLUMN experimental_tag TEXT;";
    REQUIRE(sqlite3_exec(db, foreign_objects, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    const auto session = reopened->create_session("after foreign columns", FocusMode::Normal);
    CHECK(reopened->get_session(session.session_id).has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);
}

TEST_CASE("reopening a populated database preserves every table's contents") {
    // The blunt end-to-end version: write one of everything, close, reopen, and read it all
    // back. Catches a migration that drops and recreates a table — which the IF NOT EXISTS
    // baseline cannot do today, but a future ALTER-heavy migration easily could.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("round trip", FocusMode::Deep).session_id;
        storage->insert_prediction(prediction(session_id, 80.0, 0.1, "DEEP_FOCUS"));
        storage->insert_label(session_id, FocusLabel::Productive, "manual");
        storage->upsert_app_rule("figma.com", AppRuleKind::Allow, "design work");
        FeatureVector f;
        f.seconds_since_session_start() = 42.0;
        storage->insert_feature_snapshot(session_id, f);
    }

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->get_session(session_id).has_value());
    CHECK(reopened->recent_predictions(10).size() == 1);
    REQUIRE(reopened->list_app_rules().size() == 1);
    CHECK(reopened->list_app_rules()[0].pattern == "figma.com");
    // recap() reads across predictions and snapback_events, so a non-zero average is proof
    // the reopened handle can still join the tables, not just SELECT from one.
    const auto recap = reopened->recap(session_id);
    CHECK(recap.avg_focus_score == doctest::Approx(80.0));
}

// --- Batched history/analytics queries (Roadmap 7.12) ------------------------------------

namespace {

ContextSnapshotDto snapshot(const std::string& app, const std::string& timestamp) {
    ContextSnapshotDto snap;
    snap.app_name = app;
    snap.window_title = app + " window";
    snap.summary = "in " + app;
    snap.timestamp = timestamp;
    return snap;
}

// Two-digit second suffix, so snapshots sort in insertion order under ORDER BY timestamp.
std::string ts(int second) {
    std::ostringstream out;
    out << "2026-07-11T19:" << std::setfill('0') << std::setw(2) << (second / 60) << ":"
        << std::setw(2) << (second % 60) << "Z";
    return out.str();
}

}  // namespace

TEST_CASE("recent_session_summaries matches the per-session recap it replaces") {
    // The whole point of the batched query is that it is a pure performance change. Anything
    // it computes differently from recap() is a silent behavior change in the history view,
    // so compare the two directly rather than asserting hand-written expected numbers.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    // Deliberately uneven: a session with predictions of both kinds, one with none at all,
    // and one still running. Sessions with no predictions are the case where a JOIN-based
    // rewrite most easily drops a row entirely.
    const auto busy = storage->create_session("busy", FocusMode::Deep);
    storage->insert_prediction(prediction(busy.session_id, 90.0, 0.1, "DEEP_FOCUS"));
    storage->insert_prediction(prediction(busy.session_id, 30.0, 0.9, "DISTRACTED"));
    storage->insert_prediction(prediction(busy.session_id, 55.0, 0.4, "PRODUCTIVE"));
    storage->stop_session(busy.session_id);

    const auto quiet = storage->create_session("quiet", FocusMode::Normal);
    storage->stop_session(quiet.session_id);

    const auto running = storage->create_session("running", FocusMode::Normal);

    const auto summaries = storage->recent_session_summaries(10);
    REQUIRE(summaries.size() == 3);

    // Order must match recent_sessions() exactly, since callers render it directly.
    const auto records = storage->recent_sessions(10);
    REQUIRE(records.size() == summaries.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CHECK(summaries[i].record.session_id == records[i].session_id);

        const auto expected = storage->recap(records[i].session_id);
        const auto& actual = summaries[i].recap;
        CHECK(actual.session_id == expected.session_id);
        CHECK(actual.goal == expected.goal);
        CHECK(actual.avg_focus_score == doctest::Approx(expected.avg_focus_score));
        CHECK(actual.avg_distraction_risk == doctest::Approx(expected.avg_distraction_risk));
        CHECK(actual.deep_focus_pct == doctest::Approx(expected.deep_focus_pct));
        CHECK(actual.thrash_spikes == expected.thrash_spikes);
        CHECK(actual.snapback_count == expected.snapback_count);
        CHECK(actual.duration_secs == expected.duration_secs);
    }
    // Deliberately no assertion that `running` sorts first. started_at comes from
    // now_rfc3339(), which has whole-second resolution, so three sessions created in the
    // same second tie under ORDER BY started_at DESC and their relative order is undefined.
    // That is ROADMAP 7.16's "ordering within a second is undefined" showing up in practice
    // — matching recent_sessions() above is the invariant that actually holds.
    CHECK(running.session_id != busy.session_id);
}

TEST_CASE("recent_session_summaries keeps aggregates attached under same-second ties") {
    // The defect this guards: recent_session_summaries runs three queries that each
    // re-derive "the most recent N sessions". started_at has only second resolution
    // (ROADMAP 7.16), so sessions created in one test body — or by a user starting and
    // stopping quickly — all tie. If two of those queries broke the tie differently, a
    // session would appear in the result with its aggregates silently zeroed.
    //
    // Every session here shares one started_at, so the ordering is decided entirely by the
    // tiebreak. Each carries a distinct focus score, which is what makes a mismatch visible:
    // without a total order the aggregate rows land on the wrong sessions or nowhere.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    std::unordered_map<std::string, double> expected_focus;
    for (int i = 0; i < 8; ++i) {
        const auto session = storage->create_session("tie" + std::to_string(i), FocusMode::Normal);
        const double focus = 10.0 * (i + 1);
        storage->insert_prediction(prediction(session.session_id, focus, 0.2, "PRODUCTIVE"));
        storage->backdate_session_for_test(session.session_id, "2026-07-11T19:00:00Z");
        expected_focus[session.session_id] = focus;
    }

    const auto summaries = storage->recent_session_summaries(5);
    REQUIRE(summaries.size() == 5);
    for (const auto& summary : summaries) {
        CAPTURE(summary.record.session_id);
        // The aggregate must belong to *this* session, not to whichever one a second query
        // happened to pick for the same slot.
        CHECK(summary.recap.avg_focus_score ==
              doctest::Approx(expected_focus.at(summary.record.session_id)));
    }

    // And the selection must be repeatable: same input, same five sessions, same order.
    const auto again = storage->recent_session_summaries(5);
    REQUIRE(again.size() == summaries.size());
    for (std::size_t i = 0; i < again.size(); ++i) {
        CHECK(again[i].record.session_id == summaries[i].record.session_id);
    }
    // recent_sessions() must agree too — session_history renders whichever one it is given.
    const auto records = storage->recent_sessions(5);
    REQUIRE(records.size() == summaries.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CHECK(records[i].session_id == summaries[i].record.session_id);
    }
}

TEST_CASE("recent_session_summaries honours its limit and handles an empty database") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    CHECK(storage->recent_session_summaries(10).empty());

    for (int i = 0; i < 5; ++i) {
        const auto session = storage->create_session("s" + std::to_string(i), FocusMode::Normal);
        storage->stop_session(session.session_id);
    }
    CHECK(storage->recent_session_summaries(3).size() == 3);
    CHECK(storage->recent_session_summaries(50).size() == 5);
}

TEST_CASE("context_app_counts caps snapshots per session") {
    // The subtle half of the rewrite. The old loop asked for at most N snapshots per session
    // via list_context_snapshots' LIMIT; counting every row instead would let one very long
    // session dominate the app ranking, which is a different answer, not a faster one.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto marathon = storage->create_session("marathon", FocusMode::Normal);
    for (int i = 0; i < 10; ++i) {
        storage->save_context_snapshot(marathon.session_id, snapshot("Cursor", ts(i)));
    }
    const auto brief = storage->create_session("brief", FocusMode::Normal);
    for (int i = 0; i < 2; ++i) {
        storage->save_context_snapshot(brief.session_id, snapshot("Safari", ts(i)));
    }

    const auto capped = storage->context_app_counts(10, 3);
    CHECK(capped.at("Cursor") == 3);  // capped, not 10
    CHECK(capped.at("Safari") == 2);  // under the cap, so untouched

    const auto uncapped = storage->context_app_counts(10, 100);
    CHECK(uncapped.at("Cursor") == 10);
}

TEST_CASE("context_app_counts takes the oldest snapshots within the cap") {
    // list_context_snapshots orders ASC, so its LIMIT keeps the *earliest* rows. A rewrite
    // that ordered DESC would still respect the cap while counting different apps.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("ordered", FocusMode::Normal);
    storage->save_context_snapshot(session.session_id, snapshot("First", ts(1)));
    storage->save_context_snapshot(session.session_id, snapshot("Second", ts(2)));
    storage->save_context_snapshot(session.session_id, snapshot("Third", ts(3)));

    const auto counts = storage->context_app_counts(10, 1);
    CHECK(counts.size() == 1);
    CHECK(counts.count("First") == 1);
}

TEST_CASE("context_app_counts honours the session limit and skips blank app names") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto older = storage->create_session("older", FocusMode::Normal);
    storage->save_context_snapshot(older.session_id, snapshot("Older", ts(1)));
    const auto newer = storage->create_session("newer", FocusMode::Normal);
    storage->save_context_snapshot(newer.session_id, snapshot("Newer", ts(1)));
    // A snapshot with no app name must not become an empty-string entry in the ranking.
    storage->save_context_snapshot(newer.session_id, snapshot("", ts(2)));

    // Backdate the older session explicitly. Both were created in the same wall-clock
    // second, and started_at has only second resolution (ROADMAP 7.16), so without this the
    // LIMIT 1 below would pick between two tied rows arbitrarily and the test would flake.
    storage->backdate_session_for_test(older.session_id, "2020-01-01T00:00:00Z");

    const auto one_session = storage->context_app_counts(1, 100);
    CHECK(one_session.count("Newer") == 1);
    CHECK(one_session.count("Older") == 0);
    CHECK(one_session.count("") == 0);
}

TEST_CASE("context_app_counts filters by session start when given a cutoff") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("recent", FocusMode::Normal);
    storage->save_context_snapshot(session.session_id, snapshot("Cursor", ts(1)));

    // The session was created moments ago, so a far-future cutoff excludes it and a past
    // one keeps it.
    CHECK(storage->context_app_counts(10, 100, std::string("2099-01-01T00:00:00Z")).empty());
    const auto included =
        storage->context_app_counts(10, 100, std::string("2000-01-01T00:00:00Z"));
    CHECK(included.at("Cursor") == 1);
}

// --- Single-session deletion (Roadmap 7.6) -----------------------------------------------

TEST_CASE("delete_session removes the session and every row collected during it") {
    // Until this existed the only eraser was delete_all_activity_data, so removing one bad
    // session cost the user their entire history.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto doomed = storage->create_session("doomed", FocusMode::Normal);
    storage->insert_prediction(prediction(doomed.session_id, 60.0, 0.3, "PRODUCTIVE"));
    storage->insert_label(doomed.session_id, FocusLabel::Productive, "manual");
    storage->save_context_snapshot(doomed.session_id, snapshot("Cursor", ts(1)));
    FeatureVector f;
    f.seconds_since_session_start() = 12.0;
    storage->insert_feature_snapshot(doomed.session_id, f);
    storage->stop_session(doomed.session_id);

    // A second session must be entirely unaffected — the failure this guards against is a
    // DELETE that forgot its WHERE clause.
    const auto keeper = storage->create_session("keeper", FocusMode::Normal);
    storage->insert_prediction(prediction(keeper.session_id, 70.0, 0.2, "PRODUCTIVE"));
    storage->save_context_snapshot(keeper.session_id, snapshot("Safari", ts(1)));

    CHECK(storage->delete_session(doomed.session_id));

    CHECK_FALSE(storage->get_session(doomed.session_id).has_value());
    CHECK(storage->list_context_snapshots(doomed.session_id, 10).empty());
    CHECK(storage->get_session(keeper.session_id).has_value());
    const auto remaining = storage->recent_predictions(10);
    REQUIRE(remaining.size() == 1);
    CHECK(remaining[0].session_id == keeper.session_id);
    CHECK(storage->list_context_snapshots(keeper.session_id, 10).size() == 1);
}

TEST_CASE("delete_session reports whether anything was deleted") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("once", FocusMode::Normal);

    CHECK(storage->delete_session(session.session_id));
    // Returning false rather than throwing keeps a double-click on Delete harmless, while
    // still letting the caller tell "already gone" from "deleted".
    CHECK_FALSE(storage->delete_session(session.session_id));
    CHECK_FALSE(storage->delete_session("no-such-session"));
}

TEST_CASE("delete_session preserves app rules") {
    // App rules are user configuration, not captured activity — the same boundary
    // delete_all_activity_data draws.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    storage->upsert_app_rule("figma.com", AppRuleKind::Allow, std::nullopt);
    const auto session = storage->create_session("with rules", FocusMode::Normal);

    REQUIRE(storage->delete_session(session.session_id));
    CHECK(storage->list_app_rules().size() == 1);
}

TEST_CASE("storage recap computes averages, deep-focus percentage, and distraction spikes") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session = storage->create_session("Measure focus", FocusMode::Recovery);
    storage->insert_prediction(prediction(session.session_id, 90.0, 0.10, "DEEP_FOCUS"));
    storage->insert_prediction(prediction(session.session_id, 70.0, 0.20, "PRODUCTIVE"));
    storage->insert_prediction(prediction(session.session_id, 30.0, 0.80, "DISTRACTED"));
    // The row ADR-0004's predicate exists for: a Recovery-mode session, risk in the 0.7-0.85
    // band, so the verdict is not DISTRACTED — under the old `AND focus_state` conjunct this
    // strong distraction was invisible to the count.
    storage->insert_prediction(prediction(session.session_id, 55.0, 0.75, "PRODUCTIVE"));

    const auto recap = storage->recap(session.session_id);
    CHECK(recap.session_id == session.session_id);
    CHECK(recap.goal == "Measure focus");
    CHECK(recap.avg_focus_score == doctest::Approx((90.0 + 70.0 + 30.0 + 55.0) / 4.0));
    CHECK(recap.avg_distraction_risk == doctest::Approx((0.10 + 0.20 + 0.80 + 0.75) / 4.0));
    CHECK(recap.deep_focus_pct == doctest::Approx(100.0 / 4.0));
    CHECK(recap.thrash_spikes == 2);
}

TEST_CASE("Storage::infer_session_label maps recap thresholds") {
    SessionRecap deep;
    deep.session_id = "s";
    deep.goal = "focus";
    deep.duration_secs = 3600;
    deep.avg_focus_score = 80.0;
    deep.avg_distraction_risk = 0.2;
    deep.snapback_count = 0;
    deep.thrash_spikes = 0;
    deep.deep_focus_pct = 60.0;
    CHECK(Storage::infer_session_label(deep) == FocusLabel::DeepFocus);

    SessionRecap distracted = deep;
    distracted.avg_distraction_risk = 0.75;
    distracted.thrash_spikes = 4;
    distracted.deep_focus_pct = 10.0;
    CHECK(Storage::infer_session_label(distracted) == FocusLabel::Distracted);

    SessionRecap pseudo = deep;
    pseudo.avg_distraction_risk = 0.4;
    pseudo.thrash_spikes = 1;
    pseudo.deep_focus_pct = 10.0;
    CHECK(Storage::infer_session_label(pseudo) == FocusLabel::PseudoProductive);

    SessionRecap productive = deep;
    productive.avg_distraction_risk = 0.4;
    productive.thrash_spikes = 0;
    productive.deep_focus_pct = 30.0;
    CHECK(Storage::infer_session_label(productive) == FocusLabel::Productive);
}

TEST_CASE("Storage::save_auto_session_label writes an AUTO label from recap") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session = storage->create_session("Auto label", FocusMode::Normal);
    storage->insert_prediction(prediction(session.session_id, 90.0, 0.10, "DEEP_FOCUS"));
    storage->insert_prediction(prediction(session.session_id, 85.0, 0.15, "DEEP_FOCUS"));
    storage->end_session(session.session_id);

    const FocusLabel label = storage->save_auto_session_label(session.session_id);
    CHECK(label == FocusLabel::DeepFocus);

    TempDir temp;
    const auto exported =
        storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.label_count == 1);

    const auto labels = read_file(temp.path / "labels.csv");
    CHECK(labels.find(",auto,") != std::string::npos);
    CHECK(labels.find("inferred from session recap") != std::string::npos);
}

TEST_CASE("storage exports feature snapshots and labels as CSV") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session = storage->create_session("Export training", FocusMode::Deep);

    FeatureVector f;
    f.seconds_since_session_start() = 240.0;
    f.keystroke_rate() = 3.25;
    f.is_ide() = 1.0;
    f.focus_momentum() = 0.9;
    storage->insert_feature_snapshot(session.session_id, f);
    storage->insert_label(session.session_id, FocusLabel::DeepFocus, "manual");

    TempDir temp;
    storage->export_training_csv(temp.path, std::nullopt);

    const auto features = read_file(temp.path / "features.csv");
    CHECK(features.find("timestamp,seconds_since_session_start") != std::string::npos);
    CHECK(features.find("session_id,session_goal,focus_mode") != std::string::npos);
    CHECK(features.find("Export training") != std::string::npos);
    CHECK(features.find("deep") != std::string::npos);

    const auto labels = read_file(temp.path / "labels.csv");
    CHECK(labels.find("timestamp,label,source,session_id,notes") != std::string::npos);
    CHECK(labels.find(",2,manual,") != std::string::npos);
}

TEST_CASE("storage export_training_csv filters by session id") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session_a = storage->create_session("Session A", FocusMode::Normal);
    FeatureVector fa;
    fa.seconds_since_session_start() = 60.0;
    fa.keystroke_rate() = 2.0;
    storage->insert_feature_snapshot(session_a.session_id, fa);
    storage->insert_label(session_a.session_id, FocusLabel::Productive, "manual");
    storage->end_session(session_a.session_id);

    auto session_b = storage->create_session("Session B", FocusMode::Deep);
    FeatureVector fb;
    fb.seconds_since_session_start() = 120.0;
    fb.keystroke_rate() = 3.0;
    storage->insert_feature_snapshot(session_b.session_id, fb);
    storage->end_session(session_b.session_id);

    TempDir temp_a;
    const auto export_a =
        storage->export_training_csv(temp_a.path, session_a.session_id);
    CHECK(export_a.feature_count == 1);
    CHECK(export_a.label_count == 1);
    const auto labels_a = read_file(temp_a.path / "labels.csv");
    CHECK(labels_a.find(session_a.session_id) != std::string::npos);
    CHECK(labels_a.find(session_b.session_id) == std::string::npos);

    TempDir temp_all;
    const auto export_all = storage->export_training_csv(temp_all.path, std::nullopt);
    CHECK(export_all.feature_count == 2);
    CHECK(export_all.label_count == 1);
    const auto features_all = read_file(temp_all.path / "features.csv");
    CHECK(features_all.find("Session A") != std::string::npos);
    CHECK(features_all.find("Session B") != std::string::npos);
}

TEST_CASE("storage prune_runtime_data removes old rows from all three runtime tables") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto session = storage->create_session("Retention", FocusMode::Normal);

    PredictionRecord old_pred = prediction(session.session_id, 50.0, 0.2, "PRODUCTIVE");
    old_pred.timestamp = "2020-01-01T00:00:00Z";
    storage->insert_prediction(old_pred);

    ContextSnapshotDto old_ctx;
    old_ctx.app_name = "Cursor";
    old_ctx.window_title = "old.cpp";
    old_ctx.summary = "old context";
    old_ctx.timestamp = "2020-01-01T00:00:00Z";
    storage->save_context_snapshot(session.session_id, old_ctx);

    // insert_feature_snapshot stamps rows with unix_now_secs(), so this row is "now" and
    // must survive a cutoff in the past.
    FeatureVector f;
    f.seconds_since_session_start() = 10.0;
    storage->insert_feature_snapshot(session.session_id, f);

    // 2024-01-01 as RFC3339 and as Unix epoch seconds — the same instant in the two
    // formats the tables use.
    constexpr double kCutoffUnix = 1704067200.0;
    const PruneSummary summary =
        storage->prune_runtime_data("2024-01-01T00:00:00Z", kCutoffUnix);
    CHECK(summary.predictions_deleted == 1);
    CHECK(summary.context_snapshots_deleted == 1);
    CHECK(summary.feature_snapshots_deleted == 0);  // stamped now, newer than the cutoff
    CHECK(should_vacuum_after_prune(summary.total()) == false);

    TempDir temp;
    const auto exported =
        storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.feature_count == 1);
}

TEST_CASE("storage prune_runtime_data deletes feature snapshots past the cutoff") {
    // The regression this closes: feature_snapshots is the highest-volume table (one row
    // per prediction tick) and was excluded from the prune entirely, so it grew forever
    // while predictions/context_snapshots stayed flat at the retention window.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto session = storage->create_session("Retention", FocusMode::Normal);
    FeatureVector f;
    f.seconds_since_session_start() = 10.0;
    storage->insert_feature_snapshot(session.session_id, f);

    // A cutoff far in the future makes the just-written row "old".
    constexpr double kFarFuture = 4102444800.0;  // 2100-01-01
    const PruneSummary summary =
        storage->prune_runtime_data("2100-01-01T00:00:00Z", kFarFuture);
    CHECK(summary.feature_snapshots_deleted == 1);

    TempDir temp;
    const auto exported =
        storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.feature_count == 0);
}

TEST_CASE("should_vacuum_after_prune uses the configured threshold") {
    CHECK_FALSE(should_vacuum_after_prune(0));
    CHECK_FALSE(should_vacuum_after_prune(kVacuumMinDeletedRows - 1));
    CHECK(should_vacuum_after_prune(kVacuumMinDeletedRows));
}

TEST_CASE("Storage::open routes the startup prune message through an injected logger") {
    TempDir temp;
    {
        // Seed one prediction old enough for the on-open prune (kDefaultRetentionDays)
        // to catch, then let this Storage go out of scope so the reopen below is a real
        // cold start against the file on disk.
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        const auto session = storage->create_session("Retention", FocusMode::Normal);
        PredictionRecord old_pred = prediction(session.session_id, 50.0, 0.2, "PRODUCTIVE");
        old_pred.timestamp = "2000-01-01T00:00:00Z";
        storage->insert_prediction(old_pred);
    }

    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info, [] { return std::string("2026-07-19T00:00:00Z"); });
    auto reopened = Storage::open(temp.path, &logger);
    REQUIRE(reopened.has_value());

    CHECK(log_out.str().find("[INFO]") != std::string::npos);
    CHECK(log_out.str().find("pruned 1 rows") != std::string::npos);
}

TEST_CASE("storage schema indexes the hot read paths") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto names = storage->index_names();
    auto has = [&](const std::string& name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    CHECK(has("idx_predictions_session_ts"));
    CHECK(has("idx_predictions_ts"));
    CHECK(has("idx_feature_snapshots_session_ts"));
    CHECK(has("idx_sessions_status_started"));
    CHECK(has("idx_context_snapshots_session_ts"));
    CHECK(has("idx_snapback_events_session"));
    CHECK(has("idx_labels_session"));
}

TEST_CASE("storage hot queries use an index instead of scanning") {
    // Presence isn't enough — a composite index whose leading column the query doesn't
    // filter on is unusable, which is exactly why latest_prediction() scanned despite
    // idx_predictions_session_ts existing. Assert the planner actually picks one, and
    // that no query needs a temp B-tree to satisfy its ORDER BY.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto uses_index = [&](const std::string& sql) {
        bool indexed = false;
        for (const auto& step : storage->query_plan(sql)) {
            if (step.find("USING INDEX") != std::string::npos ||
                step.find("USING COVERING INDEX") != std::string::npos) {
                indexed = true;
            }
            // A temp B-tree means the index didn't supply the ordering.
            CHECK(step.find("TEMP B-TREE") == std::string::npos);
        }
        return indexed;
    };

    CHECK(uses_index("SELECT session_id FROM predictions ORDER BY timestamp DESC LIMIT 1"));
    CHECK(uses_index(
        "SELECT session_id FROM predictions WHERE timestamp >= '2026-07-10T00:00:00Z' "
        "ORDER BY timestamp DESC"));
    CHECK(uses_index(
        "SELECT session_id FROM sessions WHERE status = 'ACTIVE' ORDER BY started_at DESC LIMIT 1"));
    CHECK(uses_index(
        "SELECT app_name FROM context_snapshots WHERE session_id = 'x' ORDER BY timestamp ASC"));
    CHECK(uses_index(
        "SELECT COUNT(*) FROM snapback_events WHERE session_id = 'x'"));
    CHECK(uses_index("SELECT id FROM labels WHERE session_id = 'x'"));
}

TEST_CASE("storage prediction windows stay in SQL and preserve the cutoff") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto session = storage->create_session("Prediction window", FocusMode::Normal);
    auto before = prediction(session.session_id, 40.0, 0.8, "DISTRACTED");
    before.timestamp = "2026-07-09T00:00:00Z";
    auto after = prediction(session.session_id, 80.0, 0.2, "PRODUCTIVE");
    after.timestamp = "2026-07-11T00:00:00Z";
    storage->insert_prediction(before);
    storage->insert_prediction(after);

    const auto rows = storage->predictions_since("2026-07-10T00:00:00Z");
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().focus_score == 80.0);
}

TEST_CASE("Storage::open explains why it failed instead of returning a bare nullopt") {
    // The old outer `catch (...)` returned nullopt with no diagnostic, so a corrupt DB, a
    // permissions problem, and a full disk all looked identical — the user just saw the app
    // decline to start. Force a real failure by putting a directory where the DB file goes:
    // sqlite can't open it, and the reason must reach the injected logger.
    TempDir temp;
    std::filesystem::create_directories(temp.path / "focoflow.db");

    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info);
    auto storage = Storage::open(temp.path, &logger);

    CHECK_FALSE(storage.has_value());
    const auto logged = log_out.str();
    CHECK(logged.find("ERROR") != std::string::npos);
    CHECK(logged.find("focoflow.db") != std::string::npos);
}

TEST_CASE("export reports a write failure instead of a bogus success") {
    // Only the open() was checked before, so a mid-write failure (classically a full disk)
    // left a truncated file behind while export still returned a success result whose
    // feature_count described rows that never made it to disk.
    //
    // Simulate an unwritable destination by making labels.csv a directory: the open
    // succeeds or fails depending on platform, but either way export must not claim success.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("Export failure", FocusMode::Normal);
    FeatureVector f;
    f.seconds_since_session_start() = 5.0;
    storage->insert_feature_snapshot(session.session_id, f);

    TempDir temp;
    std::filesystem::create_directories(temp.path / "labels.csv");
    CHECK_THROWS_AS(storage->export_training_csv(temp.path, session.session_id),
                    std::runtime_error);
}

TEST_CASE("export still succeeds on a writable destination") {
    // Guards the check above from being over-eager: a normal export must stay clean.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("Export ok", FocusMode::Normal);
    FeatureVector f;
    f.seconds_since_session_start() = 5.0;
    storage->insert_feature_snapshot(session.session_id, f);

    TempDir temp;
    const auto exported = storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.feature_count == 1);
    CHECK(std::filesystem::is_regular_file(temp.path / "features.csv"));
    CHECK(std::filesystem::is_regular_file(temp.path / "labels.csv"));
}

// --- ROADMAP 7.11: the large fixture -----------------------------------------------------
//
// Five of the six fixture shapes landed on 2026-07-29; this is the sixth, and it was left
// open deliberately because it is the entry point for the questions 7.12 and 4.4 could not
// ask. "Does the plan still use an index at scale" and "did the 10,000-row cap really go
// away" cannot be asked of a database with four rows in it, and until now **no test in this
// repo seeded more than a handful.**
//
// That matters most for 7.1. Its own write-up specified the regression test — *"seed >10,000
// predictions across several days, assert the weekly sample_count exceeds 10,000, watch it
// go red against today's code"* — and 7.1 was marked DONE without it ever being written. The
// fix is real (the window is in SQL now), but nothing pinned it, so a future change could
// reintroduce a cap and every existing test would still pass. Every test seeds far fewer
// than 10,000 rows, which is precisely what made the original bug structurally invisible.
namespace {

// Builds a database with more rows than any cap the code has ever had. Sessions are spread
// across `days` so window queries have something to actually exclude.
struct LargeFixture {
    static constexpr std::size_t kSessions = 60;
    static constexpr std::size_t kPredictionsPerSession = 200;  // 12,000 predictions
    static constexpr std::size_t kSnapshotsPerSession = 5;

    std::vector<std::string> session_ids;
    std::size_t recent_predictions = 0;  // predictions inside the last 24h

    void seed(Storage& storage) {
        // One transaction for the whole seed: 12,000 autocommitted inserts would each be
        // their own fsync and turn a 2-second test into a 2-minute one.
        Storage::Transaction txn(storage);
        for (std::size_t s = 0; s < kSessions; ++s) {
            const auto session =
                storage.create_session("Large fixture " + std::to_string(s), FocusMode::Normal);
            session_ids.push_back(session.session_id);

            // Sessions march backwards one day at a time from 2026-07-20.
            const int day = 20 - static_cast<int>(s % 20);
            char started[32];
            std::snprintf(started, sizeof(started), "2026-07-%02dT08:00:00Z", day);
            storage.backdate_session_for_test(session.session_id, started);

            for (std::size_t p = 0; p < kPredictionsPerSession; ++p) {
                auto record = prediction(session.session_id, 40.0 + (p % 60),
                                         (p % 10) / 10.0,
                                         p % 3 == 0 ? "DISTRACTED" : "PRODUCTIVE");
                char stamp[32];
                std::snprintf(stamp, sizeof(stamp), "2026-07-%02dT%02d:%02d:%02dZ", day,
                              8 + static_cast<int>(p / 60) % 12,
                              static_cast<int>(p % 60), static_cast<int>(p % 60));
                record.timestamp = stamp;
                storage.insert_prediction(record);
                if (day == 20) ++recent_predictions;
            }

            for (std::size_t c = 0; c < kSnapshotsPerSession; ++c) {
                ContextSnapshotDto snap;
                snap.app_name = c % 2 == 0 ? "Cursor" : "Chrome";
                snap.window_title = "file" + std::to_string(c) + ".cpp";
                std::snprintf(started, sizeof(started), "2026-07-%02dT09:%02d:00Z", day,
                              static_cast<int>(c));
                snap.timestamp = started;
                storage.save_context_snapshot(session.session_id, snap);
            }
            storage.end_session(session.session_id);
        }
        // Transaction rolls back in its destructor unless committed. Forgetting this made
        // every assertion below read 0 rows on the first run — a seed that silently seeds
        // nothing looks exactly like a query that returns nothing.
        txn.commit();
    }

    static constexpr std::size_t total_predictions() {
        return kSessions * kPredictionsPerSession;
    }
};

}  // namespace

TEST_CASE("a large database answers window queries past the old row cap") {
    // The regression test 7.1 asked for and never got. 12,000 predictions is above the
    // 10,000-row ceiling that recent_predictions(10000) used to impose, so a reintroduced cap
    // shows up here as a short count rather than as a plausible-looking wrong answer.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);

    const auto all = storage->predictions_since(std::nullopt);
    CHECK(all.size() == LargeFixture::total_predictions());
    CHECK(all.size() > 10'000);

    // A window that excludes most of the corpus must still be computed in SQL over the whole
    // table, not over a truncated prefix of it.
    const auto windowed = storage->predictions_since("2026-07-20T00:00:00Z");
    CHECK(windowed.size() == fixture.recent_predictions);
    CHECK(windowed.size() > 0);
    CHECK(windowed.size() < all.size());
    for (const auto& row : windowed) {
        CHECK(row.timestamp >= "2026-07-20T00:00:00Z");
    }
}

TEST_CASE("a large database still serves the hot queries from an index") {
    // The plan assertions elsewhere in this file run against an *empty* database. Without
    // stats SQLite plans structurally, so an empty table cannot distinguish "the planner
    // will use this index" from "the planner has no reason not to". ANALYZE gives it real
    // counts, which is the state a long-lived install drifts toward and the only state in
    // which the question 7.12 asked is actually being answered.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);
    storage->analyze_for_test();

    auto uses_index = [&](const std::string& sql) {
        bool indexed = false;
        for (const auto& step : storage->query_plan(sql)) {
            if (step.find("USING INDEX") != std::string::npos ||
                step.find("USING COVERING INDEX") != std::string::npos) {
                indexed = true;
            }
            CHECK(step.find("TEMP B-TREE") == std::string::npos);
        }
        return indexed;
    };

    CHECK(uses_index("SELECT session_id FROM predictions ORDER BY timestamp DESC LIMIT 1"));
    CHECK(uses_index(
        "SELECT session_id FROM predictions WHERE timestamp >= '2026-07-20T00:00:00Z' "
        "ORDER BY timestamp DESC"));
    CHECK(uses_index(
        "SELECT session_id FROM sessions WHERE status = 'ACTIVE' ORDER BY started_at DESC LIMIT 1"));
    CHECK(uses_index(
        "SELECT COUNT(*) FROM snapback_events WHERE session_id = '" +
        fixture.session_ids.front() + "'"));
}

TEST_CASE("batched aggregation over a large database matches the per-session path") {
    // 7.12 replaced 1 + 5N round trips with three queries and proved parity on a database of
    // a few rows. Parity on four rows does not exercise the window function's partitioning,
    // the per-session cap, or the tie-breaking that 7.16 forced into the ORDER BY. This runs
    // the same field-by-field comparison against 60 sessions.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);

    const auto summaries = storage->recent_session_summaries(LargeFixture::kSessions);
    REQUIRE(summaries.size() == LargeFixture::kSessions);

    for (const auto& summary : summaries) {
        const auto expected = storage->recap(summary.record.session_id);
        CHECK(summary.recap.session_id == expected.session_id);
        CHECK(summary.recap.avg_focus_score == doctest::Approx(expected.avg_focus_score));
        CHECK(summary.recap.avg_distraction_risk ==
              doctest::Approx(expected.avg_distraction_risk));
        CHECK(summary.recap.deep_focus_pct == doctest::Approx(expected.deep_focus_pct));
        CHECK(summary.recap.snapback_count == expected.snapback_count);
        CHECK(summary.recap.thrash_spikes == expected.thrash_spikes);
    }

    // The per-session snapshot cap is what stops one long session dominating the ranking, so
    // it has to hold when there are enough sessions for that to matter.
    const auto counts = storage->context_app_counts(LargeFixture::kSessions, 3);
    std::size_t total = 0;
    for (const auto& [app, count] : counts) total += count;
    CHECK(total == LargeFixture::kSessions * 3);
    CHECK(counts.at("Cursor") > 0);
}
