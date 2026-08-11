#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <sqlite3.h>

#include "app/data_import.hpp"
#include "storage/storage.hpp"
#include "util/logger.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_import_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::ostream& discard() {
    static std::ostream sink(nullptr);
    return sink;
}

// A real Snapback database at the current schema, with `sessions` rows in it.
void seed_database(const std::filesystem::path& dir, int sessions) {
    Logger log(discard());
    auto storage = Storage::open(dir, &log);
    REQUIRE(storage.has_value());
    for (int i = 0; i < sessions; ++i) {
        storage->create_session("goal " + std::to_string(i), FocusMode::Normal);
    }
}

std::int64_t count_sessions(const std::filesystem::path& db_path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
            SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sessions", -1, &stmt, nullptr) ==
            SQLITE_OK);
    std::int64_t count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

void set_user_version(const std::filesystem::path& db_path, int version) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    const std::string sql = "PRAGMA user_version = " + std::to_string(version);
    REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("inspect_import_candidate refuses a file that is not there") {
    TempDir dir;
    const auto candidate = inspect_import_candidate(dir.path / "nope.db", dir.path / "live.db");
    CHECK_FALSE(candidate.acceptable);
    CHECK(candidate.rejection == ImportRejection::kMissingFile);
    CHECK_FALSE(candidate.message.empty());
}

TEST_CASE("inspect_import_candidate refuses bytes that are not a database") {
    TempDir dir;
    const auto junk = dir.path / "holiday.jpg";
    {
        std::ofstream out(junk, std::ios::binary);
        out << "\xFF\xD8\xFF\xE0 not a database at all";
    }

    // The point of the case: sqlite3_open *succeeds* on this file — it defers reading until a
    // page is touched — so a check that trusted the open call would accept a JPEG and then
    // replace the user's entire history with it.
    const auto candidate = inspect_import_candidate(junk, dir.path / "live.db");
    CHECK_FALSE(candidate.acceptable);
    CHECK(candidate.rejection == ImportRejection::kNotADatabase);
}

TEST_CASE("inspect_import_candidate refuses a valid database that is not Snapback's") {
    TempDir dir;
    const auto foreign = dir.path / "someone_elses.db";
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(foreign.string().c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db, "CREATE TABLE bookmarks(url TEXT)", nullptr, nullptr, nullptr) ==
                SQLITE_OK);
        sqlite3_close(db);
    }

    // A perfectly good SQLite file with no `sessions` table would otherwise import cleanly and
    // leave the product with an empty history it believes is the user's.
    const auto candidate = inspect_import_candidate(foreign, dir.path / "live.db");
    CHECK_FALSE(candidate.acceptable);
    CHECK(candidate.rejection == ImportRejection::kNotSnapbackData);
}

TEST_CASE("inspect_import_candidate refuses a database from a newer Snapback") {
    TempDir source;
    seed_database(source.path, 1);
    const auto incoming = source.path / "focoflow.db";
    set_user_version(incoming, kSchemaVersion + 1);

    TempDir live;
    const auto candidate = inspect_import_candidate(incoming, live.path / "focoflow.db");

    // 7.3's downgrade guard, enforced *before* the swap. Letting this through is exactly how
    // the hand-copy workaround strands people: the file lands, and then the app refuses to open
    // its own database with no route back from inside the product.
    CHECK_FALSE(candidate.acceptable);
    CHECK(candidate.rejection == ImportRejection::kSchemaTooNew);
    CHECK(contains(candidate.message, "newer version"));
}

TEST_CASE("inspect_import_candidate refuses the live database as its own source") {
    TempDir dir;
    seed_database(dir.path, 1);
    const auto live = dir.path / "focoflow.db";

    const auto candidate = inspect_import_candidate(live, live);
    CHECK_FALSE(candidate.acceptable);
    CHECK(candidate.rejection == ImportRejection::kSameFile);
}

TEST_CASE("inspect_import_candidate reports what the user is about to adopt") {
    TempDir source;
    seed_database(source.path, 3);

    TempDir live;
    const auto candidate =
        inspect_import_candidate(source.path / "focoflow.db", live.path / "focoflow.db");

    CHECK(candidate.acceptable);
    CHECK(candidate.rejection == ImportRejection::kNone);
    CHECK(candidate.session_count == 3);
    CHECK(candidate.schema_version == kSchemaVersion);
}

TEST_CASE("import_database replaces the database and keeps the old one") {
    TempDir source;
    seed_database(source.path, 3);

    TempDir live;
    seed_database(live.path, 7);
    const auto db_path = live.path / "focoflow.db";
    REQUIRE(count_sessions(db_path) == 7);

    Logger log(discard());
    const auto outcome = import_database(source.path / "focoflow.db", db_path, &log);

    REQUIRE(outcome.ok);
    // Replace, not merge: exactly the incoming history, not 10 sessions.
    CHECK(count_sessions(db_path) == 3);
    CHECK(outcome.session_count == 3);

    // The replaced history is still on disk, which is what makes a destructive import
    // defensible — and the message says where.
    REQUIRE(std::filesystem::exists(outcome.backup_path));
    CHECK(count_sessions(outcome.backup_path) == 7);
    CHECK(contains(outcome.message, outcome.backup_path.string()));
    CHECK(outcome.backup_path.filename().string() == replaced_database_backup_name());
}

TEST_CASE("import_database upgrades an older database on the way in") {
    TempDir source;
    seed_database(source.path, 2);
    const auto incoming = source.path / "focoflow.db";
    // A file from an older install: the rows are current-shaped, but the stamp says v1, which
    // is what a real older export carries.
    set_user_version(incoming, 1);

    TempDir live;
    seed_database(live.path, 1);
    const auto db_path = live.path / "focoflow.db";

    Logger log(discard());
    const auto outcome = import_database(incoming, db_path, &log);

    REQUIRE(outcome.ok);
    CHECK(count_sessions(db_path) == 2);
    // Migrated forward rather than left at v1, so the app can actually open what it imported.
    CHECK(outcome.schema_version == kSchemaVersion);

    Logger reopen_log(discard());
    auto reopened = Storage::open(live.path, &reopen_log);
    REQUIRE(reopened.has_value());
    CHECK(reopened->schema_version() == kSchemaVersion);
}

TEST_CASE("import_database changes nothing when the candidate is refused") {
    TempDir dir;
    seed_database(dir.path, 4);
    const auto db_path = dir.path / "focoflow.db";

    const auto junk = dir.path / "notes.txt";
    {
        std::ofstream out(junk);
        out << "just some text";
    }

    Logger log(discard());
    const auto outcome = import_database(junk, db_path, &log);

    CHECK_FALSE(outcome.ok);
    CHECK_FALSE(outcome.message.empty());
    // The refusal happens before anything is touched: no swap, and no backup file left behind
    // to imply one was attempted.
    CHECK(count_sessions(db_path) == 4);
    CHECK_FALSE(std::filesystem::exists(dir.path / replaced_database_backup_name()));
    CHECK_FALSE(std::filesystem::exists(dir.path / "focoflow.db.incoming"));
}

TEST_CASE("import_database clears the replaced database's write-ahead log") {
    TempDir source;
    seed_database(source.path, 2);

    TempDir live;
    seed_database(live.path, 9);
    const auto db_path = live.path / "focoflow.db";

    // Stand in for a -wal left by the connection that was just closed. It describes the
    // database being replaced; SQLite would try to apply it to the new file and either fail to
    // open or resurrect rows from a history the user just discarded.
    {
        std::ofstream out(db_path.string() + "-wal", std::ios::binary);
        out << "stale write-ahead log";
    }

    Logger log(discard());
    const auto outcome = import_database(source.path / "focoflow.db", db_path, &log);

    REQUIRE(outcome.ok);
    CHECK_FALSE(std::filesystem::exists(db_path.string() + "-wal"));
    CHECK(count_sessions(db_path) == 2);
}

TEST_CASE("import_database leaves no staging file behind on success") {
    TempDir source;
    seed_database(source.path, 1);

    TempDir live;
    seed_database(live.path, 1);

    Logger log(discard());
    const auto outcome = import_database(source.path / "focoflow.db", live.path / "focoflow.db",
                                         &log);

    REQUIRE(outcome.ok);
    CHECK_FALSE(std::filesystem::exists(live.path / "focoflow.db.incoming"));
}

TEST_CASE("stage_import changes nothing until it is applied") {
    TempDir source;
    seed_database(source.path, 3);

    TempDir live;
    seed_database(live.path, 8);
    const auto db_path = live.path / "focoflow.db";

    Logger log(discard());
    const auto staged = stage_import(source.path / "focoflow.db", db_path, &log);

    REQUIRE(staged.ok);
    CHECK(staged.session_count == 3);
    // The whole point of staging: the app is still running on its own data, so the live
    // database must be untouched until the next launch.
    CHECK(count_sessions(db_path) == 8);
    CHECK(has_staged_import(db_path));
    CHECK(contains(staged.message, "Restart"));

    // ...and then the next launch applies it.
    const auto applied = apply_staged_import(db_path, &log);
    REQUIRE(applied.has_value());
    REQUIRE(applied->ok);
    CHECK(count_sessions(db_path) == 3);
    CHECK_FALSE(has_staged_import(db_path));
}

TEST_CASE("stage_import refuses the same files a direct import would") {
    TempDir dir;
    seed_database(dir.path, 2);
    const auto db_path = dir.path / "focoflow.db";

    const auto junk = dir.path / "notes.txt";
    {
        std::ofstream out(junk);
        out << "not a database";
    }

    Logger log(discard());
    const auto staged = stage_import(junk, db_path, &log);
    CHECK_FALSE(staged.ok);
    CHECK_FALSE(has_staged_import(db_path));
    CHECK(count_sessions(db_path) == 2);
}

TEST_CASE("a staged import can be cancelled before it is applied") {
    TempDir source;
    seed_database(source.path, 5);

    TempDir live;
    seed_database(live.path, 1);
    const auto db_path = live.path / "focoflow.db";

    Logger log(discard());
    REQUIRE(stage_import(source.path / "focoflow.db", db_path, &log).ok);
    REQUIRE(has_staged_import(db_path));

    CHECK(cancel_staged_import(db_path));
    CHECK_FALSE(has_staged_import(db_path));
    // Cancelling is a real undo: the next launch does nothing at all.
    CHECK_FALSE(apply_staged_import(db_path, &log).has_value());
    CHECK(count_sessions(db_path) == 1);
    // Cancelling twice is harmless rather than an error.
    CHECK_FALSE(cancel_staged_import(db_path));
}

TEST_CASE("apply_staged_import is a no-op on an ordinary launch") {
    TempDir dir;
    seed_database(dir.path, 4);

    Logger log(discard());
    CHECK_FALSE(apply_staged_import(dir.path / "focoflow.db", &log).has_value());
    CHECK(count_sessions(dir.path / "focoflow.db") == 4);
}

TEST_CASE("a staged import is consumed even when applying it fails") {
    TempDir live;
    seed_database(live.path, 6);
    const auto db_path = live.path / "focoflow.db";

    // A staged file that is not a usable database — a truncated write, a full disk. Retrying it
    // on every launch would turn one bad import into an app that never starts properly again.
    {
        std::ofstream out(live.path / staged_import_name(), std::ios::binary);
        out << "not a database";
    }
    REQUIRE(has_staged_import(db_path));

    Logger log(discard());
    const auto applied = apply_staged_import(db_path, &log);
    REQUIRE(applied.has_value());
    CHECK_FALSE(applied->ok);
    CHECK_FALSE(has_staged_import(db_path));
    // And the user's data survived the failed apply untouched.
    CHECK(count_sessions(db_path) == 6);
}

TEST_CASE("import_database works with no existing database to replace") {
    TempDir source;
    seed_database(source.path, 5);

    TempDir live;
    const auto db_path = live.path / "focoflow.db";
    REQUIRE_FALSE(std::filesystem::exists(db_path));

    // The new-laptop case the item is written for: nothing to back up, and the import must
    // still land rather than failing on the missing backup source.
    Logger log(discard());
    const auto outcome = import_database(source.path / "focoflow.db", db_path, &log);

    REQUIRE(outcome.ok);
    CHECK(count_sessions(db_path) == 5);
}
