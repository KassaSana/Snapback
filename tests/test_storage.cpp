#include "doctest_wrapper.hpp"

#include "time_literals.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include <sqlite3.h>

#include "engine/focus_summary.hpp"
#include "storage/storage.hpp"
#include "util/logger.hpp"
#include "util/time.hpp"

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

// An RFC3339 instant as the epoch-millisecond literal the schema stores, for the fixtures that
// write rows through raw SQL instead of through Storage.
//
// Raw SQL bypasses the layer that would otherwise convert, and writing the literal text into an
// INTEGER column does not fail: SQLite keeps it as TEXT, because it is not a well-formed
// integer literal. Every later comparison is then between storage classes rather than between
// instants -- TEXT always sorts above INTEGER -- so the row silently never matches a cutoff.
// That is the ADR-0007 defect exactly, and a fixture gets no exemption from it.
std::string ms_literal(const std::string& rfc3339) {
    const auto ms = unix_ms_from_rfc3339(rfc3339);
    REQUIRE(ms.has_value());
    return std::to_string(*ms);
}

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
    p.timestamp_ms = ms("2026-07-11T19:00:00Z");
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
    CHECK(first.started_at_ms.has_value());
    CHECK(first.ended_at_ms == std::nullopt);

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
    CHECK(active->ended_at_ms == std::nullopt);

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
    context.timestamp_ms = ms("2026-07-11T19:00:00Z");
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

    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    REQUIRE(storage->close_session_span(session.session_id, ms("2026-08-05T09:10:00Z")));
    storage->begin_session_span(session.session_id, ms("2026-08-05T10:00:00Z"));
    REQUIRE(storage->close_session_span(session.session_id, ms("2026-08-05T10:10:00Z")));

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T11:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 20 * 60);
    CHECK_FALSE(storage->has_open_span(session.session_id));
}

TEST_CASE("a span cannot be opened on a session that is no longer ACTIVE") {
    // AUD-04a. The engine tick decides to open a span under one lock and writes it under
    // another; a Stop landing between the two used to insert a fresh open span on a
    // COMPLETED session. Every attendance query measures an open span as
    // COALESCE(ended_at, now), and nothing ever closes a stopped session's spans, so that
    // one row makes daily and weekly attended minutes climb forever.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("stopped", FocusMode::Normal);

    REQUIRE(storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z")));
    REQUIRE(storage->close_session_span(session.session_id, ms("2026-08-05T09:10:00Z")));
    storage->stop_session(session.session_id);

    CHECK_FALSE(storage->begin_session_span(session.session_id, ms("2026-08-05T09:20:00Z")));
    CHECK_FALSE(storage->begin_session_span_now(session.session_id));
    CHECK_FALSE(storage->has_open_span(session.session_id));

    // The refusal changes nothing at all -- in particular it does not re-stamp the closed
    // span, so the session's attended time is exactly what it was before the stale write.
    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T11:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 10 * 60);
}

TEST_CASE("a refused span leaves an unknown session untouched") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    CHECK_FALSE(storage->begin_session_span("no-such-session", ms("2026-08-05T09:00:00Z")));
    CHECK_FALSE(storage->has_open_span("no-such-session"));
}

TEST_CASE("an open span counts up to now") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("in progress", FocusMode::Normal);

    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    CHECK(storage->has_open_span(session.session_id));

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T09:30:00Z"));
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

    CHECK_FALSE(storage->active_secs(session.session_id, ms("2026-08-05T09:00:00Z")).has_value());
    CHECK_FALSE(storage->has_open_span(session.session_id));
}

TEST_CASE("reopening a span closes the previous one instead of overlapping it") {
    // A missed pause -- a crash, a dropped idle edge -- must not leave two open spans
    // double-counting the same minutes, because nothing later could tell which was wrong.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("overlap", FocusMode::Normal);

    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:30:00Z"));  // no close between

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T10:00:00Z"));
    REQUIRE(active.has_value());
    // 09:00-09:30 closed by the reopen, plus 09:30-10:00 still open. Sixty minutes, not
    // ninety -- which is what overlapping spans would have produced.
    CHECK(*active == 60 * 60);
}

TEST_CASE("closing a span twice is not an error and does not extend it") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("idempotent", FocusMode::Normal);

    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    CHECK(storage->close_session_span(session.session_id, ms("2026-08-05T09:10:00Z")));
    // Already paused: nothing open, so nothing to close. An ordinary outcome.
    CHECK_FALSE(storage->close_session_span(session.session_id, ms("2026-08-05T09:20:00Z")));

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T10:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 10 * 60);
}

TEST_CASE("a backwards clock cannot make a span subtract time") {
    // DST and NTP corrections move wall clock backwards. A span ending before it began would
    // contribute a negative to the SUM and silently reduce the total.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("time travel", FocusMode::Normal);

    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    REQUIRE(storage->close_session_span(session.session_id, ms("2026-08-05T08:00:00Z")));

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T10:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 0);
}

TEST_CASE("a dangling span closes at the newest evidence across every source") {
    // Roadmap 7.23. The end of a crashed session's span is unknowable, so it is set to the
    // last time the session recorded *anything* about the user. Two evidence tables are
    // seeded with the newest written first, so the query is proven to take the maximum rather
    // than whichever table it happens to read last. (`snapback_events` is the third source the
    // query reads; nothing writes that table yet -- Roadmap 2.15 -- so it cannot be seeded
    // here. It is queried anyway so this keeps working the day 2.15 lands.)
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("crashed", FocusMode::Normal);
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));

    ContextSnapshotDto snapshot;
    snapshot.app_name = "Cursor";
    snapshot.window_title = "storage.cpp";
    snapshot.summary = "editing";
    snapshot.timestamp_ms = ms("2026-08-05T09:45:00Z");  // the newest, and deliberately written first
    storage->save_context_snapshot(session.session_id, snapshot);

    PredictionRecord prediction;
    prediction.session_id = session.session_id;
    prediction.timestamp_ms = ms("2026-08-05T09:20:00Z");
    storage->insert_prediction(prediction);

    const auto closed_at = storage->close_dangling_session_span(session.session_id);
    REQUIRE(closed_at.has_value());
    CHECK(*closed_at == ms("2026-08-05T09:45:00Z"));
    CHECK_FALSE(storage->has_open_span(session.session_id));

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T18:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 45 * 60);  // not the nine hours since
}

TEST_CASE("a dangling span with no evidence closes where it started") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("silent", FocusMode::Normal);
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));

    const auto closed_at = storage->close_dangling_session_span(session.session_id);
    REQUIRE(closed_at.has_value());
    CHECK(*closed_at == ms("2026-08-05T09:00:00Z"));

    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T18:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 0);
}

TEST_CASE("evidence older than the span does not shorten it") {
    // Evidence from before the span opened belongs to an earlier stretch. Closing at it would
    // end the span before it began; MAX(started_at, ...) is what stops that, and this proves
    // the fallback is chosen rather than relying on the SQL clamp downstream.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("stale evidence", FocusMode::Normal);

    PredictionRecord prediction;
    prediction.session_id = session.session_id;
    prediction.timestamp_ms = ms("2026-08-05T08:00:00Z");
    storage->insert_prediction(prediction);
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));

    const auto closed_at = storage->close_dangling_session_span(session.session_id);
    REQUIRE(closed_at.has_value());
    CHECK(*closed_at == ms("2026-08-05T09:00:00Z"));
    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T18:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 0);
}

TEST_CASE("closing a dangling span reports nothing when none is open") {
    // The ordinary case on every clean start. It must be distinguishable from "closed one",
    // because the caller logs a warning for the crash case and should not cry wolf.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("clean", FocusMode::Normal);
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    REQUIRE(storage->close_session_span(session.session_id, ms("2026-08-05T09:10:00Z")));

    CHECK_FALSE(storage->close_dangling_session_span(session.session_id).has_value());
    CHECK_FALSE(storage->close_dangling_session_span("no-such-session").has_value());
    // The already-closed span is untouched.
    const auto active = storage->active_secs(session.session_id, ms("2026-08-05T10:00:00Z"));
    REQUIRE(active.has_value());
    CHECK(*active == 10 * 60);
}

TEST_CASE("deleting a session deletes its spans") {
    // delete_session enumerates child tables explicitly rather than trusting ON DELETE
    // CASCADE, because historical databases were not all created with it. A new child table
    // that is not added to that list leaves orphans.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("doomed", FocusMode::Normal);
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));
    storage->close_session_span(session.session_id, ms("2026-08-05T09:10:00Z"));

    REQUIRE(storage->delete_session(session.session_id));
    CHECK_FALSE(storage->active_secs(session.session_id, ms("2026-08-05T10:00:00Z")).has_value());
}

TEST_CASE("deleting all activity deletes spans too") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("wiped", FocusMode::Normal);
    storage->begin_session_span(session.session_id, ms("2026-08-05T09:00:00Z"));

    storage->delete_all_activity_data();
    CHECK_FALSE(storage->active_secs(session.session_id, ms("2026-08-05T10:00:00Z")).has_value());
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
    stamped.timestamp_ms = ms("2026-07-11T20:00:00Z");
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

TEST_CASE("a pre-2.15 snapback row survives the episode migration with no invented detail") {
    // Roadmap 2.15 added columns to a table that already existed, so the migration has to
    // adopt rows written before it. Those rows are real interruptions -- they were counted --
    // and nothing can reconstruct when they began or how long they lasted. They come back with
    // empty detail rather than a plausible-looking zero start time.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("older install", FocusMode::Normal).session_id;
    }

    // Write the row the old way -- summary and return time only -- then rewind the stamp so
    // the runner replays the migration over it.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const auto insert = "INSERT INTO snapback_events (session_id, summary, timestamp) VALUES ('" +
                        session_id + "', 'Return to old.cpp', '2026-07-30T09:00:00Z');" +
                        "PRAGMA user_version = 4;";
    REQUIRE(sqlite3_exec(db, insert.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);

    // Still counted, which is the guarantee that matters: a migration must not silently reduce
    // a number the user has already been shown.
    CHECK(reopened->recap(session_id).snapback_count == 1);

    const auto episodes = reopened->list_snapback_episodes(session_id, 10);
    REQUIRE(episodes.size() == 1);
    CHECK(episodes[0].summary == "Return to old.cpp");
    CHECK(episodes[0].ended_at_ms == ms("2026-07-30T09:00:00Z"));
    // nullopt rather than an empty string now: the column is NULL, and ADR-0007 kept that
    // distinct from an instant precisely so "nobody recorded this" stays sayable.
    CHECK_FALSE(episodes[0].started_at_ms.has_value());  // unknowable, and left that way
    CHECK(episodes[0].duration_secs == 0);

    // And the new UNIQUE index tolerates it: a NULL started_at is distinct from every other
    // NULL in SQLite, so a second legacy row does not collide with the first.
    sqlite3* again = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &again) == SQLITE_OK);
    const auto second = "INSERT INTO snapback_events (session_id, summary, timestamp) VALUES ('" +
                        session_id + "', 'Return to other.cpp', '2026-07-30T10:00:00Z');";
    CHECK(sqlite3_exec(again, second.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(again);
}

TEST_CASE("an episode is written once, however many times it is offered") {
    // The storage half of 2.15's idempotence. An episode is identified by its session and the
    // moment it began, so a delivery retry or a replayed tick cannot inflate a count the user
    // is shown -- while a genuinely later interruption still records.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("interrupted", FocusMode::Normal);

    SnapbackEpisode episode;
    episode.session_id = session.session_id;
    episode.summary = "Return to state.cpp";
    episode.app_name = "Cursor";
    episode.file_hint = "state.cpp";
    episode.started_at_ms = ms("2026-07-30T09:10:00Z");
    episode.ended_at_ms = ms("2026-07-30T09:12:00Z");
    episode.duration_secs = 120;

    CHECK(storage->insert_snapback_episode(episode));
    CHECK_FALSE(storage->insert_snapback_episode(episode));
    // Even a different summary and duration do not create a second row: the identity is the
    // start, not the payload. A retry that re-derives slightly different text is still a retry.
    auto restated = episode;
    restated.summary = "Return somewhere else";
    restated.duration_secs = 999;
    CHECK_FALSE(storage->insert_snapback_episode(restated));

    CHECK(storage->recap(session.session_id).snapback_count == 1);
    // The first write wins; a retry does not quietly rewrite what the user was shown.
    const auto stored = storage->list_snapback_episodes(session.session_id, 10);
    REQUIRE(stored.size() == 1);
    CHECK(stored[0].summary == "Return to state.cpp");
    CHECK(stored[0].duration_secs == 120);

    // A later interruption is a different episode.
    auto later = episode;
    later.started_at_ms = ms("2026-07-30T11:00:00Z");
    CHECK(storage->insert_snapback_episode(later));
    CHECK(storage->recap(session.session_id).snapback_count == 2);
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
        "timestamp) VALUES ('" + session_id + "', 40.0, 0.4, 'PRODUCTIVE', " +
        ms_literal("2020-01-01T00:00:00Z") + ");";
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
    snap.timestamp_ms = ms(timestamp);
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
        storage->backdate_session_for_test(session.session_id, ms("2026-07-11T19:00:00Z"));
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
    storage->backdate_session_for_test(older.session_id, ms("2020-01-01T00:00:00Z"));

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
    CHECK(storage->context_app_counts(10, 100, ms("2099-01-01T00:00:00Z")).empty());
    const auto included =
        storage->context_app_counts(10, 100, ms("2000-01-01T00:00:00Z"));
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
    old_pred.timestamp_ms = ms("2020-01-01T00:00:00Z");
    storage->insert_prediction(old_pred);

    ContextSnapshotDto old_ctx;
    old_ctx.app_name = "Cursor";
    old_ctx.window_title = "old.cpp";
    old_ctx.summary = "old context";
    old_ctx.timestamp_ms = ms("2020-01-01T00:00:00Z");
    storage->save_context_snapshot(session.session_id, old_ctx);

    // insert_feature_snapshot stamps rows with unix_now_secs(), so this row is "now" and
    // must survive a cutoff in the past.
    FeatureVector f;
    f.seconds_since_session_start() = 10.0;
    storage->insert_feature_snapshot(session.session_id, f);

    // 2024-01-01T00:00:00Z. One cutoff for all three tables now: ADR-0007 ended the format
    // disagreement that made this take two.
    constexpr std::int64_t kCutoffMs = 1704067200000;
    const PruneSummary summary = storage->prune_runtime_data(kCutoffMs);
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
    constexpr std::int64_t kFarFutureMs = 4102444800000;  // 2100-01-01
    const PruneSummary summary = storage->prune_runtime_data(kFarFutureMs);
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
        old_pred.timestamp_ms = ms("2000-01-01T00:00:00Z");
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
    before.timestamp_ms = ms("2026-07-09T00:00:00Z");
    auto after = prediction(session.session_id, 80.0, 0.2, "PRODUCTIVE");
    after.timestamp_ms = ms("2026-07-11T00:00:00Z");
    storage->insert_prediction(before);
    storage->insert_prediction(after);

    const auto rows = storage->predictions_since(ms("2026-07-10T00:00:00Z"));
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
            storage.backdate_session_for_test(session.session_id, ms(started));

            for (std::size_t p = 0; p < kPredictionsPerSession; ++p) {
                auto record = prediction(session.session_id, 40.0 + (p % 60),
                                         (p % 10) / 10.0,
                                         p % 3 == 0 ? "DISTRACTED" : "PRODUCTIVE");
                char stamp[32];
                std::snprintf(stamp, sizeof(stamp), "2026-07-%02dT%02d:%02d:%02dZ", day,
                              8 + static_cast<int>(p / 60) % 12,
                              static_cast<int>(p % 60), static_cast<int>(p % 60));
                record.timestamp_ms = ms(stamp);
                storage.insert_prediction(record);
                if (day == 20) ++recent_predictions;
            }

            for (std::size_t c = 0; c < kSnapshotsPerSession; ++c) {
                ContextSnapshotDto snap;
                snap.app_name = c % 2 == 0 ? "Cursor" : "Chrome";
                snap.window_title = "file" + std::to_string(c) + ".cpp";
                std::snprintf(started, sizeof(started), "2026-07-%02dT09:%02d:00Z", day,
                              static_cast<int>(c));
                snap.timestamp_ms = ms(started);
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
    const auto windowed = storage->predictions_since(ms("2026-07-20T00:00:00Z"));
    CHECK(windowed.size() == fixture.recent_predictions);
    CHECK(windowed.size() > 0);
    CHECK(windowed.size() < all.size());
    for (const auto& row : windowed) {
        CHECK(row.timestamp_ms >= ms("2026-07-20T00:00:00Z"));
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

namespace {

// The C++ fold that Roadmap 7.12 moved into SQL, kept here as the reference implementation the
// new queries are compared against. Not a rewrite of the SQL in another language: this is the
// code that used to run in `AppState::analytics` and `AppState::summary_report`, so a
// disagreement means the move changed an answer the user was already being shown.
struct ReferenceStats {
    std::size_t sample_count{};
    double avg_focus_score{};
    std::size_t distracted_count{};
    std::uint64_t longest_focus_secs{};
    std::vector<AnalyticsHour> hourly;
};

ReferenceStats fold_in_cpp(const std::vector<PredictionRecord>& predictions) {
    ReferenceStats out;
    struct Bucket {
        std::size_t count{};
        double focus_sum{};
        std::size_t distracted{};
    };
    std::array<Bucket, 24> buckets{};
    // Roadmap 10.13. The focused stretch is a duration now, and its reference implementation is
    // the production one -- `summarize_predictions` -- rather than a second copy of the gap
    // rule, which would only prove the test agrees with itself.
    //
    // Grouped by session first, because a run belongs to one session and this fixture seeds
    // three concurrent sessions per day with identical timestamps. Feeding that interleaving
    // to a flat fold would measure the ordering artefact rather than the rule.
    {
        std::unordered_map<std::string, std::vector<PredictionRecord>> by_session;
        for (auto it = predictions.rbegin(); it != predictions.rend(); ++it) {
            by_session[it->session_id].push_back(*it);
        }
        for (const auto& [session_id, rows] : by_session) {
            out.longest_focus_secs =
                std::max(out.longest_focus_secs, summarize_predictions(rows).longest_focus_secs);
        }
    }
    for (const auto& prediction : predictions) {
        ++out.sample_count;
        out.avg_focus_score += prediction.focus_score;
        if (prediction.focus_state == "DISTRACTED") {
            ++out.distracted_count;
        }
        const int hour = local_hour_from_rfc3339(rfc3339_from_unix_ms(prediction.timestamp_ms));
        if (hour < 0 || hour >= 24) continue;
        auto& bucket = buckets[static_cast<std::size_t>(hour)];
        ++bucket.count;
        bucket.focus_sum += prediction.focus_score;
        if (prediction.focus_state == "DISTRACTED") ++bucket.distracted;
    }
    if (out.sample_count > 0) out.avg_focus_score /= static_cast<double>(out.sample_count);
    for (int hour = 0; hour < 24; ++hour) {
        const auto& bucket = buckets[static_cast<std::size_t>(hour)];
        if (bucket.count == 0) continue;
        out.hourly.push_back(AnalyticsHour{
            hour, bucket.count, bucket.focus_sum / static_cast<double>(bucket.count),
            static_cast<double>(bucket.distracted) / static_cast<double>(bucket.count)});
    }
    return out;
}

}  // namespace

TEST_CASE("SQL prediction aggregates match the C++ fold they replaced, at 12,000 rows") {
    // Roadmap 7.12. The point of the fixture is that these numbers cannot be checked by
    // inspection: 12,000 rows across 60 sessions and 20 days is where a wrong GROUP BY, a
    // silently truncated CAST, or a local-hour conversion that disagrees with the C library
    // shows up as a plausible number rather than an obvious one.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);

    for (const std::optional<std::int64_t> cutoff :
         {std::optional<std::int64_t>{}, std::optional<std::int64_t>{ms("2026-07-20T00:00:00Z")}}) {
        CAPTURE(cutoff.value_or(0));
        const auto expected = fold_in_cpp(storage->predictions_since(cutoff));
        const auto actual = storage->prediction_stats(cutoff);

        CHECK(actual.sample_count == expected.sample_count);
        CHECK(actual.sample_count > 0);
        CHECK(actual.avg_focus_score == doctest::Approx(expected.avg_focus_score));
        CHECK(actual.distracted_count == expected.distracted_count);
        CHECK(actual.longest_focus_secs == expected.longest_focus_secs);
        CHECK(actual.longest_focus_secs > 0);

        // The local-hour conversion is the part most likely to differ between SQLite's
        // `localtime` modifier and the C library call the C++ path used, so it is compared
        // bucket by bucket rather than by count.
        const auto hourly = storage->hourly_focus_buckets(cutoff);
        REQUIRE(hourly.size() == expected.hourly.size());
        for (std::size_t i = 0; i < hourly.size(); ++i) {
            CAPTURE(i);
            CHECK(hourly[i].hour == expected.hourly[i].hour);
            CHECK(hourly[i].sample_count == expected.hourly[i].sample_count);
            CHECK(hourly[i].avg_focus_score ==
                  doctest::Approx(expected.hourly[i].avg_focus_score));
            CHECK(hourly[i].distracted_fraction ==
                  doctest::Approx(expected.hourly[i].distracted_fraction));
        }
        CHECK(hourly.size() > 1);  // a single bucket would make the comparison vacuous
    }
}

TEST_CASE("the SQL productive-session streak matches the recap loop it replaced") {
    // Roadmap 7.12. The loop counted leading *completed* sessions whose recap average was at
    // or above the bar, skipping running ones and stopping at the first one below.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);

    const auto reference = [&](std::size_t limit, double bar) {
        std::size_t streak = 0;
        for (const auto& session : storage->recent_sessions(limit)) {
            if (session.status != "COMPLETED") continue;
            if (storage->recap(session.session_id).avg_focus_score >= bar) {
                ++streak;
            } else {
                break;
            }
        }
        return streak;
    };

    // Three bars: one every session clears, one nothing clears, and the production bar. A
    // single bar could be matched by a query that ignores the scores entirely.
    for (const double bar : {0.0, 70.0, 1000.0}) {
        CAPTURE(bar);
        CHECK(storage->productive_session_streak(200, bar) == reference(200, bar));
    }
    CHECK(storage->productive_session_streak(200, 0.0) == LargeFixture::kSessions);
    CHECK(storage->productive_session_streak(200, 1000.0) == 0);

    // A running session in front of the streak is skipped, not treated as a break in it.
    storage->create_session("still going", FocusMode::Normal);
    CHECK(storage->productive_session_streak(200, 0.0) == LargeFixture::kSessions);
}

TEST_CASE("SQL session-window totals match the summary loop they replaced") {
    // Roadmap 7.12. The cap applies to recency before the window filter, which is what makes
    // this worth pinning: applying them the other way round is an easy and invisible change.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);

    const std::int64_t cutoff = ms("2026-07-20T00:00:00Z");
    const auto reference = [&](std::size_t limit) {
        Storage::SessionWindowTotals totals;
        for (const auto& summary : storage->recent_session_summaries(limit)) {
            const auto& session = summary.record;
            if (!session.started_at_ms || *session.started_at_ms < cutoff) continue;
            ++totals.session_count;
            if (session.status == "COMPLETED") {
                ++totals.completed_session_count;
                totals.focus_seconds += summary.recap.duration_secs;
            }
        }
        return totals;
    };

    for (const std::size_t limit : {std::size_t{500}, std::size_t{10}}) {
        CAPTURE(limit);
        const auto expected = reference(limit);
        const auto actual = storage->session_window_totals(limit, cutoff);
        CHECK(actual.session_count == expected.session_count);
        CHECK(actual.completed_session_count == expected.completed_session_count);
        CHECK(actual.focus_seconds == expected.focus_seconds);
    }
    // Not vacuous: the window really does exclude most of the fixture.
    CHECK(storage->session_window_totals(500, cutoff).session_count > 0);
    CHECK(storage->session_window_totals(500, cutoff).session_count < LargeFixture::kSessions);
}

TEST_CASE("the analytics aggregates run in a bounded number of queries") {
    // Roadmap 7.12's actual acceptance boundary. Correctness parity above says the answers
    // match; this says the work to get them no longer grows with the database. The counter is
    // SQLite's own, so it counts every statement the queries prepare, including the ones a
    // future edit might add back inside a loop.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    LargeFixture fixture;
    fixture.seed(*storage);

    // Two statements for stats (totals + streak), one each for the buckets, the session
    // streak, and the window totals. The bound is deliberately loose -- the claim is
    // "constant", and 60 sessions x 200 predictions must not move it.
    CHECK(storage->count_statements_for_test([&] { (void)storage->prediction_stats(); }) == 2);
    CHECK(storage->count_statements_for_test([&] { (void)storage->hourly_focus_buckets(); }) == 1);
    CHECK(storage->count_statements_for_test(
              [&] { (void)storage->productive_session_streak(200, 70.0); }) == 1);
    CHECK(storage->count_statements_for_test([&] {
              (void)storage->session_window_totals(500, ms("2026-07-20T00:00:00Z"));
          }) == 1);

    // What it replaced, for contrast: five statements per completed session. Without this the
    // bounds above would look like arbitrary small numbers rather than the point of the item.
    const auto per_session = storage->count_statements_for_test([&] {
        for (const auto& session : storage->recent_sessions(200)) {
            if (session.status == "COMPLETED") (void)storage->recap(session.session_id);
        }
    });
    CHECK(per_session > 5 * LargeFixture::kSessions);
}

// --- Roadmap 2.14: optional end-of-session reflection --------------------------------------

TEST_CASE("a session reflection round-trips, and never answering stays distinguishable") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("ship the exporter", FocusMode::Deep);

    // The state every session starts in, and the one Skip leaves behind: no answer at all,
    // which is not the same as an empty answer.
    CHECK_FALSE(storage->get_session(session.session_id)->reflection_done.has_value());
    CHECK_FALSE(storage->get_session(session.session_id)->reflection_next_step.has_value());

    const auto saved = storage->save_session_reflection(session.session_id, "wired the CSV path",
                                                        "add the header row");
    REQUIRE(saved.has_value());
    CHECK(saved->reflection_done == "wired the CSV path");
    CHECK(saved->reflection_next_step == "add the header row");

    // Durable, not just returned.
    const auto reread = storage->get_session(session.session_id);
    REQUIRE(reread.has_value());
    CHECK(reread->reflection_done == "wired the CSV path");
    CHECK(reread->reflection_next_step == "add the header row");

    // And it survives the other readers, which each build their own SELECT.
    const auto recent = storage->recent_sessions(10);
    REQUIRE_FALSE(recent.empty());
    CHECK(recent[0].reflection_done == "wired the CSV path");
}

TEST_CASE("one half of a reflection can be answered without inventing the other") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("read the spec", FocusMode::Normal);

    const auto saved =
        storage->save_session_reflection(session.session_id, std::nullopt, "start the draft");
    REQUIRE(saved.has_value());
    CHECK_FALSE(saved->reflection_done.has_value());
    CHECK(saved->reflection_next_step == "start the draft");
}

TEST_CASE("clearing a reflection removes it rather than storing a blank") {
    // The edit path: an answer the user no longer wants goes back to the same NULL that means
    // "never answered". Storing "" instead would leave an empty heading in the export.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("triage", FocusMode::Recovery);
    storage->save_session_reflection(session.session_id, "closed 4 issues", "reply to review");

    const auto cleared =
        storage->save_session_reflection(session.session_id, std::nullopt, std::nullopt);
    REQUIRE(cleared.has_value());
    CHECK_FALSE(cleared->reflection_done.has_value());
    CHECK_FALSE(cleared->reflection_next_step.has_value());
}

TEST_CASE("reflecting on a session that does not exist reports it instead of silently passing") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    CHECK_FALSE(storage->save_session_reflection("no-such-session", "something", "next").has_value());
}

TEST_CASE("a pre-reflection database upgrades without disturbing the sessions in it") {
    // The real prior-version upgrade case 2.14 asks for: a v5 install, with a session already
    // recorded, meeting the v6 schema. The columns must appear, the session must survive
    // intact, and its reflection must read as never-answered rather than as anything else.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("written before reflections existed",
                                             FocusMode::Deep)
                         .session_id;
    }

    // Rewind to v5 and drop the columns the way that version had it: no ALTER ever ran.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const char* rewind =
        "ALTER TABLE sessions DROP COLUMN reflection_done;"
        "ALTER TABLE sessions DROP COLUMN reflection_next_step;"
        "PRAGMA user_version = 5;";
    REQUIRE(sqlite3_exec(db, rewind, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);

    const auto migrated = reopened->get_session(session_id);
    REQUIRE(migrated.has_value());
    CHECK(migrated->goal == "written before reflections existed");
    CHECK_FALSE(migrated->reflection_done.has_value());
    CHECK_FALSE(migrated->reflection_next_step.has_value());

    // Not merely readable: writable, which is what proves the columns are really there.
    const auto saved =
        reopened->save_session_reflection(session_id, "upgraded fine", std::nullopt);
    REQUIRE(saved.has_value());
    CHECK(saved->reflection_done == "upgraded fine");
}

TEST_CASE("the reflection migration is idempotent when the columns already exist") {
    // Rule 1 of kSchemaVersion: user_version 0 replays every migration over a database that
    // already has the full schema, so this must be a no-op rather than an error.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("already current", FocusMode::Normal).session_id;
        storage->save_session_reflection(session_id, "kept", "kept too");
    }

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "PRAGMA user_version = 0;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);
    const auto kept = reopened->get_session(session_id);
    REQUIRE(kept.has_value());
    CHECK(kept->reflection_done == "kept");
    CHECK(kept->reflection_next_step == "kept too");
}

// --- Roadmap 2.19: attended time inside a local day / week ---------------------------------

namespace {

// Writes a closed span directly. The production path opens and closes spans through idle
// transitions; these tests are about the window arithmetic, so the spans are given.
//
// The boundaries stay readable RFC3339 in the cases below and are converted here, because raw
// SQL bypasses the storage layer that would otherwise do it. Writing the literal text into an
// INTEGER column would not fail -- SQLite stores it as TEXT, since it is not a well-formed
// integer literal -- and every comparison against a real timestamp would then be a
// storage-class comparison that silently answers the wrong question. That is the same trap
// ADR-0007 closed in production, and a fixture is entitled to no exemption from it.
void seed_span(Storage& storage, const std::string& session_id, const std::string& started_at,
               const std::string& ended_at) {
    storage.execute_for_test("INSERT INTO session_spans (session_id, started_at, ended_at) "
                             "VALUES ('" + session_id + "', " + ms_literal(started_at) + ", " +
                             ms_literal(ended_at) + ")");
}

}  // namespace

TEST_CASE("attended time counts durable spans and nothing else") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("measured", FocusMode::Normal);

    // A session that is open for an hour but attended for thirty minutes reports thirty.
    seed_span(*storage, session.session_id, "2026-08-09T10:00:00Z", "2026-08-09T10:30:00Z");
    const auto day = storage->attended_secs_in_local_day(ms("2026-08-09T12:00:00Z"));
    CHECK(day == 30 * 60);
}

TEST_CASE("a session with no spans contributes nothing rather than its open duration") {
    // ADR-0005's rule, at the reporting end: attendance that was never measured is not zero
    // minutes of presence, but it must not be invented from how long the session was open.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    storage->create_session("legacy, never measured", FocusMode::Normal);
    CHECK(storage->attended_secs_in_local_day(ms("2026-08-09T12:00:00Z")) == 0);
    CHECK(storage->attended_secs_in_local_week(ms("2026-08-09T12:00:00Z")) == 0);
}

TEST_CASE("a span outside the window does not leak into it") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("yesterday", FocusMode::Normal);
    seed_span(*storage, session.session_id, "2026-08-01T10:00:00Z", "2026-08-01T11:00:00Z");
    CHECK(storage->attended_secs_in_local_day(ms("2026-08-09T12:00:00Z")) == 0);
}

TEST_CASE("an open span is counted only up to now, not to the end of time") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("still going", FocusMode::Normal);
    storage->execute_for_test("INSERT INTO session_spans (session_id, started_at) VALUES ('" +
                              session.session_id + "', " +
                              ms_literal("2026-08-09T10:00:00Z") + ")");
    CHECK(storage->attended_secs_in_local_day(ms("2026-08-09T10:20:00Z")) == 20 * 60);
}

TEST_CASE("the week window holds a day the daily window does not") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("earlier this week", FocusMode::Normal);
    // 2026-08-09 is a Sunday; the ISO week containing it began Monday 2026-08-03.
    seed_span(*storage, session.session_id, "2026-08-05T10:00:00Z", "2026-08-05T10:45:00Z");
    CHECK(storage->attended_secs_in_local_day(ms("2026-08-09T12:00:00Z")) == 0);
    CHECK(storage->attended_secs_in_local_week(ms("2026-08-09T12:00:00Z")) == 45 * 60);

    // ...and not into the week before it.
    CHECK(storage->attended_secs_in_local_week(ms("2026-08-02T12:00:00Z")) == 0);
}

TEST_CASE("a Monday sees its own week rather than the one that just ended") {
    // The off-by-a-week case: `weekday 1` moves *forward* to Monday, so asking on a Monday
    // without stepping back first lands on next Monday and reports an empty week.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("monday morning", FocusMode::Normal);
    seed_span(*storage, session.session_id, "2026-08-03T09:00:00Z", "2026-08-03T09:30:00Z");
    CHECK(storage->attended_secs_in_local_week(ms("2026-08-03T12:00:00Z")) == 30 * 60);
}

TEST_CASE("attended minutes are whole and never rounded up") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("partial", FocusMode::Normal);
    seed_span(*storage, session.session_id, "2026-08-09T10:00:00Z", "2026-08-09T10:00:59Z");
    // 59 seconds of presence is not a minute of attendance.
    CHECK(storage->attended_secs_in_local_day(ms("2026-08-09T12:00:00Z")) / 60 == 0);
}

TEST_CASE("attended_secs_since clips to an arbitrary Review lower bound") {
    // Roadmap 2.19 Review half. 30d / custom ranges are not calendar day/week windows; they
    // still must count the same clipped spans, just against a caller-supplied floor.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("custom range", FocusMode::Normal);
    seed_span(*storage, session.session_id, "2026-08-01T10:00:00Z", "2026-08-01T10:30:00Z");
    seed_span(*storage, session.session_id, "2026-08-08T10:00:00Z", "2026-08-08T10:20:00Z");

    // Floor after the first span: only the second twenty minutes remain.
    CHECK(storage->attended_secs_since(ms("2026-08-09T12:00:00Z"),
                                       ms("2026-08-05T00:00:00Z")) == 20 * 60);
    // No floor: both spans.
    CHECK(storage->attended_secs_since(ms("2026-08-09T12:00:00Z"), std::nullopt) == 50 * 60);
}

// --- daily_summary: the per-local-day series behind the Review trend surfaces --------------

namespace {

// The local midnight after `instant_ms`, derived independently of the SQL under test through
// the C library's localtime/mktime — the same conversion SQLite's 'localtime' modifier uses,
// so the two agree including across DST. Independent derivation is the point: a test that
// asked storage where midnight falls would be checking the query against itself.
std::int64_t next_local_midnight_ms(std::int64_t instant_ms) {
    std::time_t t = static_cast<std::time_t>(instant_ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday += 1;
    tm.tm_isdst = -1;  // mktime re-derives DST for the normalized date
    return static_cast<std::int64_t>(std::mktime(&tm)) * 1000;
}

}  // namespace

TEST_CASE("daily_summary splits a span crossing local midnight across both days") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("late night", FocusMode::Normal);

    // Thirty minutes before a local midnight to forty-five after: the recursive day axis
    // must clip the span at the boundary, not attribute it whole to either day.
    const std::int64_t midnight = next_local_midnight_ms(ms("2026-08-05T12:00:00Z"));
    storage->execute_for_test(
        "INSERT INTO session_spans (session_id, started_at, ended_at) VALUES ('" +
        session.session_id + "', " + std::to_string(midnight - 30 * 60 * 1000) + ", " +
        std::to_string(midnight + 45 * 60 * 1000) + ")");

    const auto days = storage->daily_summary(midnight + 3 * 60 * 60 * 1000,
                                             midnight - 2 * 24 * 60 * 60 * 1000);
    REQUIRE(days.size() == 2);
    CHECK(days[0].day < days[1].day);
    CHECK(days[0].attended_secs == 30 * 60);
    CHECK(days[1].attended_secs == 45 * 60);
}

TEST_CASE("daily_summary omits empty days and clips an open span to now") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("still going", FocusMode::Normal);
    storage->execute_for_test("INSERT INTO session_spans (session_id, started_at) VALUES ('" +
                              session.session_id + "', " + ms_literal("2026-08-07T10:00:00Z") +
                              ")");

    // A week-long axis with one active day: the series has one bucket, not seven — days
    // with nothing in them are the frontend's gaps to fill, matching hourly buckets.
    const auto days =
        storage->daily_summary(ms("2026-08-07T10:20:00Z"), ms("2026-08-01T00:00:00Z"));
    REQUIRE(days.size() == 1);
    CHECK(days[0].attended_secs == 20 * 60);
}

TEST_CASE("daily_summary sums focused and deep run gaps, not row counts") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("mixed day", FocusMode::Normal);

    const std::int64_t t0 = ms("2026-08-05T18:00:00Z");
    const auto at = [&](std::int64_t offset_secs, double focus, const std::string& state) {
        auto p = prediction(session.session_id, focus, 0.2, state);
        p.timestamp_ms = t0 + offset_secs * 1000;
        storage->insert_prediction(p);
    };
    at(0, 90.0, "DEEP_FOCUS");
    at(60, 88.0, "DEEP_FOCUS");    // deep gap: 60
    at(120, 70.0, "PRODUCTIVE");   // focused gap only: prior state no longer deep
    at(180, 30.0, "DISTRACTED");   // entering distraction is credited to neither
    at(240, 65.0, "PRODUCTIVE");   // leaving distraction is not focus time either
    at(300, 92.0, "DEEP_FOCUS");   // focused gap; not deep — the earlier row was PRODUCTIVE
    at(1000, 91.0, "DEEP_FOCUS");  // 700s of silence: a gap past the cap is a break

    const auto days =
        storage->daily_summary(t0 + 60 * 60 * 1000, t0 - 24 * 60 * 60 * 1000);
    REQUIRE(days.size() == 1);
    CHECK(days[0].sample_count == 7);
    CHECK(days[0].focused_secs == 180);
    CHECK(days[0].deep_focus_secs == 60);
    // DEEP_FOCUS implies non-DISTRACTED, so this holds for any input.
    CHECK(days[0].focused_secs >= days[0].deep_focus_secs);
    // No spans were recorded: measured attendance stays zero rather than being invented
    // from prediction activity (ADR-0005 at the reporting end).
    CHECK(days[0].attended_secs == 0);
}

TEST_CASE("daily_summary counts sessions and snapback episodes by local day") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("counted", FocusMode::Normal);
    storage->backdate_session_for_test(session.session_id, ms("2026-08-05T18:00:00Z"));

    SnapbackEpisode episode;
    episode.session_id = session.session_id;
    episode.summary = "Return to auth.ts";
    episode.app_name = "Cursor";
    episode.started_at_ms = ms("2026-08-05T18:05:00Z");
    episode.ended_at_ms = ms("2026-08-05T18:09:00Z");
    episode.duration_secs = 240;
    CHECK(storage->insert_snapback_episode(episode));

    const auto days =
        storage->daily_summary(ms("2026-08-05T20:00:00Z"), ms("2026-08-03T00:00:00Z"));
    REQUIRE(days.size() == 1);
    CHECK(days[0].session_count == 1);
    CHECK(days[0].snapback_count == 1);
}

// --- ADR-0007 / Roadmap 7.16: time is INTEGER epoch milliseconds ---------------------------

namespace {

// The storage class SQLite actually used for one cell, as `typeof()` reports it.
//
// Read straight from the file rather than through Storage, so it needs no test-only API on the
// shipping class (7.14) and cannot be satisfied by the layer under test. It has to be asserted
// directly because nothing else in the suite can see it: SQLite is dynamically typed, so
// writing "2026-08-09T10:00:00Z" into an INTEGER column succeeds and keeps it as TEXT, and
// every read, every ORDER BY, and every round trip through the DTOs then behaves exactly as it
// did before the migration. A suite that only checks values stays green against a schema that
// never really moved -- which is the state this tree was briefly in, between the migration
// landing and the writers being converted.
std::string cell_type(const std::filesystem::path& dir, const std::string& table,
                      const std::string& column) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((dir / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const std::string sql = "SELECT typeof(" + column + ") FROM " + table + " LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    std::string out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = sqlite3_column_text(stmt, 0);
        if (text) out = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

}  // namespace

TEST_CASE("every time column stores an integer after an ordinary write") {
    // The assertion that pins the migration. Every row here is written through the normal
    // production path rather than seeded, so this fails if any writer still binds RFC3339
    // text -- which is precisely the half-finished state a value-only test cannot see.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        const auto session = storage->create_session("typed", FocusMode::Normal);
        session_id = session.session_id;

        storage->insert_prediction(prediction(session_id, 50.0, 0.2, "PRODUCTIVE"));

        ContextSnapshotDto snap;
        snap.app_name = "Cursor";
        snap.window_title = "storage.cpp";
        snap.file_hint = "storage.cpp";
        snap.project_hint = "Snapback";
        snap.summary = "editing";
        snap.timestamp_ms = ms("2026-08-09T10:00:00Z");
        storage->save_context_snapshot(session_id, snap);

        storage->insert_label(session_id, FocusLabel::Productive, "manual", std::nullopt);
        storage->upsert_app_rule("youtube", AppRuleKind::Block, std::nullopt);

        FeatureVector f;
        f.seconds_since_session_start() = 10.0;
        storage->insert_feature_snapshot(session_id, f);
        storage->begin_session_span_now(session_id);

        SnapbackEpisode episode;
        episode.session_id = session_id;
        episode.summary = "back to storage.cpp";
        episode.started_at_ms = ms("2026-08-09T10:05:00Z");
        episode.ended_at_ms = ms("2026-08-09T10:09:00Z");
        episode.duration_secs = 240;
        REQUIRE(storage->insert_snapback_episode(episode));
    }

    CHECK(cell_type(temp.path, "sessions", "started_at") == "integer");
    CHECK(cell_type(temp.path, "predictions", "timestamp") == "integer");
    CHECK(cell_type(temp.path, "context_snapshots", "timestamp") == "integer");
    CHECK(cell_type(temp.path, "labels", "timestamp") == "integer");
    CHECK(cell_type(temp.path, "app_rules", "created_at") == "integer");
    CHECK(cell_type(temp.path, "app_rules", "updated_at") == "integer");
    CHECK(cell_type(temp.path, "feature_snapshots", "timestamp") == "integer");
    CHECK(cell_type(temp.path, "session_spans", "started_at") == "integer");
    CHECK(cell_type(temp.path, "snapback_events", "timestamp") == "integer");
    CHECK(cell_type(temp.path, "snapback_events", "started_at") == "integer");

    // And the value survives the round trip, so "integer" is not merely a well-typed zero.
    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    const auto rows = reopened->list_context_snapshots(session_id, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].timestamp_ms == ms("2026-08-09T10:00:00Z"));
}

TEST_CASE("migrating a v6 database converts its timestamps rather than reinterpreting them") {
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("older install", FocusMode::Normal).session_id;
    }

    // Rewrite the rows the way v6 held them -- RFC3339 text -- and rewind the stamp so the
    // runner replays migration 7 over them. This is what every existing install looks like.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const std::string seed =
        "UPDATE sessions SET started_at = '2026-08-09T10:00:00Z', "
        "  ended_at = '2026-08-09T11:00:00Z';"
        "INSERT INTO predictions (session_id, focus_score, distraction_risk, focus_state, "
        "  timestamp) VALUES ('" + session_id + "', 40.0, 0.4, 'PRODUCTIVE', "
        "  '2026-08-09T10:30:00Z');"
        "PRAGMA user_version = 6;";
    REQUIRE(sqlite3_exec(db, seed.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    {
        auto reopened = Storage::open(temp.path);
        REQUIRE(reopened.has_value());
        CHECK(reopened->schema_version() == kSchemaVersion);

        // The same instants, in a new representation -- not reinterpreted, and not lost.
        const auto session = reopened->get_session(session_id);
        REQUIRE(session.has_value());
        CHECK(session->started_at_ms == ms("2026-08-09T10:00:00Z"));
        REQUIRE(session->ended_at_ms.has_value());
        CHECK(*session->ended_at_ms == ms("2026-08-09T11:00:00Z"));

        const auto predictions = reopened->recent_predictions(10);
        REQUIRE(predictions.size() == 1);
        CHECK(predictions[0].timestamp_ms == ms("2026-08-09T10:30:00Z"));
    }
    CHECK(cell_type(temp.path, "sessions", "started_at") == "integer");
}

TEST_CASE("a timestamp that never parsed stops outliving retention") {
    // Roadmap 5.5, and the reason ADR-0007 rejected the one-sitting patch.
    //
    // `datetime(timestamp) < datetime(?1)` yielded NULL for a value it could not parse, and
    // `NULL < x` is NULL, so such a row survived every retention pass forever with nothing
    // surfaced. The migration maps it to the epoch, which is older than any retention window,
    // so the very next prune collects it under the ordinary policy.
    TempDir temp;
    std::string session_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        session_id = storage->create_session("corrupted", FocusMode::Normal).session_id;
    }

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const std::string seed =
        "INSERT INTO predictions (session_id, focus_score, distraction_risk, focus_state, "
        "  timestamp) VALUES ('" + session_id + "', 40.0, 0.4, 'PRODUCTIVE', 'not a date');"
        "PRAGMA user_version = 6;";
    REQUIRE(sqlite3_exec(db, seed.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    // Storage::open migrates, then prunes, so the row is gone by the time it returns. Under
    // the old comparison it would still be here -- and would have been on every open after
    // this one, for the life of the install.
    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->recent_predictions(10).empty());
    // The session is untouched: retention collects telemetry, not the user's history.
    CHECK(reopened->get_session(session_id).has_value());
}

TEST_CASE("the retention delete can use the timestamp index") {
    // The second half of 5.5. Wrapping the column in `datetime()` made it unusable as an index
    // key, so the prune full-scanned the two largest tables in the database on every startup.
    // A bare integer comparison is sargable. This needs its own assertion because the query
    // returns the same rows either way -- a regression here costs only time, which is exactly
    // the kind of defect that survives a value-based suite.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("planned", FocusMode::Normal);
    for (int i = 0; i < 50; ++i) {
        storage->insert_prediction(prediction(session.session_id, 50.0, 0.2, "PRODUCTIVE"));
    }
    storage->analyze_for_test();

    // Planned from the same constant production runs, so reintroducing the `datetime()`
    // wrapper fails here. Planning a copy of the SQL would assert only that some indexable
    // statement is possible, and would sail through the exact regression this guards.
    const auto uses_index = [&](const char* sql) {
        for (const auto& step : storage->query_plan(sql)) {
            if (step.find("USING INDEX") != std::string::npos ||
                step.find("USING COVERING INDEX") != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    CHECK(uses_index(kPrunePredictionsSql));

    // `context_snapshots` still scans, and that is recorded rather than asserted away.
    // 5.5 named only `idx_predictions_ts`, and it was right to: the sole index on this table is
    // `idx_context_snapshots_session_ts(session_id, timestamp)`, whose leading column is the
    // session, so a bare `timestamp <` cannot use it however the predicate is written.
    // Unwrapping the column was still necessary here -- it is what stops the NULL comparison
    // silently keeping rows -- but it does not make this one indexed.
    //
    // Deliberately not fixed by adding `context_snapshots(timestamp)`. That index would be
    // paid on every window change to save a scan that happens once per day of uptime, and
    // that trade belongs to the Tier 14 performance work with a measurement behind it, not to
    // a migration. Asserted as false so the day someone adds the index, this fails and they
    // are sent here to delete the paragraph.
    CHECK_FALSE(uses_index(kPruneContextSnapshotsSql));
}

TEST_CASE("migration keeps a genuine NULL and floors an unparseable value to the epoch") {
    // The two halves of the conversion rule, which are deliberately different. NULL is
    // load-bearing -- on sessions.ended_at_ms it means "still running" -- so folding a bad value
    // into NULL would resurrect a completed session as an active one. An unparseable value
    // becomes 0 instead, which is prunable rather than immortal.
    TempDir temp;
    std::string running_id;
    std::string broken_id;
    {
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        running_id = storage->create_session("still going", FocusMode::Normal).session_id;
        broken_id = storage->create_session("bad end", FocusMode::Normal).session_id;
    }

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open((temp.path / "focoflow.db").string().c_str(), &db) == SQLITE_OK);
    const std::string seed =
        "UPDATE sessions SET ended_at = NULL, status = 'ACTIVE' WHERE session_id = '" +
        running_id + "';"
        "UPDATE sessions SET ended_at = 'garbage', status = 'COMPLETED' WHERE session_id = '" +
        broken_id + "';"
        "PRAGMA user_version = 6;";
    REQUIRE(sqlite3_exec(db, seed.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = Storage::open(temp.path);
    REQUIRE(reopened.has_value());

    const auto running = reopened->get_session(running_id);
    REQUIRE(running.has_value());
    CHECK_FALSE(running->ended_at_ms.has_value());  // still open, not stamped with an invented end

    const auto broken = reopened->get_session(broken_id);
    REQUIRE(broken.has_value());
    REQUIRE(broken->ended_at_ms.has_value());  // still completed, rather than resurrected
    CHECK(*broken->ended_at_ms == ms("1970-01-01T00:00:00Z"));
}
