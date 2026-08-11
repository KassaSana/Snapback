#include "app/data_import.hpp"

#include "storage/storage.hpp"
#include "util/logger.hpp"

#include <sqlite3.h>

#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace snapback {
namespace {

// A minimal RAII connection. Storage is deliberately not reused here: it opens WAL, runs
// retention, and *migrates on open*, all of which are wrong for a file we have not yet agreed
// to accept. Inspection has to be able to read a database this build would refuse.
class RawDb {
  public:
    RawDb(const std::filesystem::path& path, int flags) {
        if (sqlite3_open_v2(path.string().c_str(), &db_, flags, nullptr) != SQLITE_OK) {
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
        }
    }
    ~RawDb() {
        if (db_) sqlite3_close(db_);
    }
    RawDb(const RawDb&) = delete;
    RawDb& operator=(const RawDb&) = delete;

    [[nodiscard]] bool ok() const { return db_ != nullptr; }
    [[nodiscard]] sqlite3* get() const { return db_; }

  private:
    sqlite3* db_ = nullptr;
};

// Single-column integer query. Returns fallback when the statement will not prepare or step,
// which is the normal outcome for a file that is not the database we hoped for.
std::int64_t scalar(sqlite3* db, const char* sql, std::int64_t fallback) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return fallback;
    }
    std::int64_t value = fallback;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

// `sqlite3_open` succeeds on a path that does not exist and on one holding arbitrary bytes —
// it defers the real read. Touching a page is what actually distinguishes a database from a
// JPEG someone renamed, so integrity is established by a query rather than by the open call.
bool looks_like_sqlite(sqlite3* db) {
    return scalar(db, "SELECT COUNT(*) FROM sqlite_master", -1) >= 0;
}

bool has_table(sqlite3* db, const char* name) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

// VACUUM INTO, the same mechanism 7.22's pre-migration backup uses and for the same reason:
// the source may be a live WAL database whose recent commits are not in the .db file yet, so
// copying bytes can produce a torn read. Destination must not exist; VACUUM INTO refuses to
// overwrite.
void vacuum_into(sqlite3* db, const std::filesystem::path& destination) {
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "VACUUM INTO ?1", -1, &stmt, nullptr) != SQLITE_OK) {
        const std::string err = sqlite3_errmsg(db);
        if (stmt) sqlite3_finalize(stmt);
        throw std::runtime_error("could not prepare VACUUM INTO: " + err);
    }
    // Bound, not interpolated: a data directory can legitimately contain a quote.
    const std::string path = destination.string();
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    const std::string err = rc == SQLITE_DONE ? std::string() : sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw std::runtime_error("VACUUM INTO failed: " + err);
}

// Two paths naming the same file. Compared by `equivalent` rather than by string so that a
// relative path, a trailing separator, or a symlink to the live database is still caught —
// importing the live database over itself is a destructive no-op, not a harmless one.
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    const bool equal = std::filesystem::equivalent(a, b, ec);
    return !ec && equal;
}

ImportCandidate reject(ImportRejection why, std::string message) {
    ImportCandidate candidate;
    candidate.acceptable = false;
    candidate.rejection = why;
    candidate.message = std::move(message);
    return candidate;
}

}  // namespace

std::string replaced_database_backup_name() { return "focoflow.db.pre-import.bak"; }

std::string staged_import_name() { return "focoflow.db.import-pending"; }

StageOutcome stage_import(const std::filesystem::path& incoming,
                          const std::filesystem::path& db_path, Logger* logger) {
    StageOutcome outcome;

    const ImportCandidate candidate = inspect_import_candidate(incoming, db_path);
    if (!candidate.acceptable) {
        outcome.message = candidate.message;
        return outcome;
    }

    const auto staged = db_path.parent_path() / staged_import_name();
    try {
        RawDb source(incoming, SQLITE_OPEN_READONLY);
        if (!source.ok()) throw std::runtime_error("could not open the file to import");
        // Snapshotted rather than copied, for the same reason the swap is: an import source
        // taken from a running Snapback keeps recent commits in a -wal beside it, and copying
        // the .db alone would silently drop the newest sessions.
        vacuum_into(source.get(), staged);
    } catch (const std::exception& err) {
        if (logger) logger->error(std::string("import: could not stage: ") + err.what());
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
        outcome.message =
            "Could not read that database all the way through, so nothing was changed. "
            "The file may be damaged.";
        return outcome;
    }

    std::ostringstream msg;
    msg << "Ready to import " << candidate.session_count
        << (candidate.session_count == 1 ? " session" : " sessions")
        << ". Restart Snapback to replace your current data — until then nothing has changed.";
    outcome.ok = true;
    outcome.message = msg.str();
    outcome.schema_version = candidate.schema_version;
    outcome.session_count = candidate.session_count;

    if (logger) {
        logger->info("import: staged " + incoming.string() + " (" +
                     std::to_string(candidate.session_count) + " sessions) for next launch");
    }
    return outcome;
}

bool has_staged_import(const std::filesystem::path& db_path) {
    std::error_code ec;
    const auto staged = db_path.parent_path() / staged_import_name();
    return std::filesystem::exists(staged, ec) && !ec;
}

bool cancel_staged_import(const std::filesystem::path& db_path) {
    std::error_code ec;
    const auto staged = db_path.parent_path() / staged_import_name();
    const bool removed = std::filesystem::remove(staged, ec);
    return removed && !ec;
}

std::optional<ImportOutcome> apply_staged_import(const std::filesystem::path& db_path,
                                                 Logger* logger) {
    if (!has_staged_import(db_path)) return std::nullopt;

    const auto staged = db_path.parent_path() / staged_import_name();
    ImportOutcome outcome = import_database(staged, db_path, logger);

    // Consumed either way. A staged file that survived a failed apply would retry on every
    // launch, which turns one bad import into a permanently broken app — the opposite of the
    // recoverability this whole feature is for.
    std::error_code ignored;
    std::filesystem::remove(staged, ignored);
    return outcome;
}

ImportCandidate inspect_import_candidate(const std::filesystem::path& incoming,
                                         const std::filesystem::path& current_db_path) {
    std::error_code ec;
    if (incoming.empty() || !std::filesystem::exists(incoming, ec) ||
        !std::filesystem::is_regular_file(incoming, ec)) {
        return reject(ImportRejection::kMissingFile, "That file does not exist.");
    }

    if (!current_db_path.empty() && same_file(incoming, current_db_path)) {
        return reject(ImportRejection::kSameFile,
                      "That is the database Snapback is already using. Choose an exported copy "
                      "from another machine or a backup file.");
    }

    RawDb db(incoming, SQLITE_OPEN_READONLY);
    if (!db.ok() || !looks_like_sqlite(db.get())) {
        return reject(ImportRejection::kNotADatabase,
                      "That file is not a Snapback database. Choose a focoflow.db file or one of "
                      "Snapback's .bak backups.");
    }

    // A valid SQLite file that is not *ours* — someone's browser history, say — would otherwise
    // be accepted and then replace the entire product's data with an empty history.
    if (!has_table(db.get(), "sessions")) {
        return reject(ImportRejection::kNotSnapbackData,
                      "That file is a database, but not one of Snapback's — it has no session "
                      "history in it.");
    }

    const int version = static_cast<int>(scalar(db.get(), "PRAGMA user_version", 0));

    // 7.3's downgrade guard, enforced *before* the swap rather than after. Letting this through
    // is how the manual copy workaround strands people: the file lands, the app then refuses to
    // open its own database, and there is no way back from inside the product.
    if (version > kSchemaVersion) {
        std::ostringstream msg;
        msg << "That database was written by a newer version of Snapback (format v" << version
            << "; this build understands v" << kSchemaVersion
            << "). Update Snapback first, then import it.";
        return reject(ImportRejection::kSchemaTooNew, msg.str());
    }

    ImportCandidate candidate;
    candidate.acceptable = true;
    candidate.rejection = ImportRejection::kNone;
    candidate.schema_version = version;
    candidate.session_count = scalar(db.get(), "SELECT COUNT(*) FROM sessions", 0);
    return candidate;
}

ImportOutcome import_database(const std::filesystem::path& incoming,
                              const std::filesystem::path& db_path, Logger* logger) {
    ImportOutcome outcome;

    const ImportCandidate candidate = inspect_import_candidate(incoming, db_path);
    if (!candidate.acceptable) {
        outcome.ok = false;
        outcome.message = candidate.message;
        return outcome;
    }

    const auto directory = db_path.parent_path();
    const auto backup = directory / replaced_database_backup_name();
    const auto staging = directory / "focoflow.db.incoming";

    // Step 2. Snapshot what is about to be replaced. This is the whole reason a destructive
    // import is defensible: the previous history is still on disk afterwards, under a name the
    // message names. A failure here aborts before anything is overwritten.
    std::error_code ec;
    if (std::filesystem::exists(db_path, ec)) {
        try {
            RawDb current(db_path, SQLITE_OPEN_READONLY);
            if (!current.ok()) throw std::runtime_error("could not open the current database");
            vacuum_into(current.get(), backup);
        } catch (const std::exception& err) {
            if (logger) {
                logger->error(std::string("import: could not back up the current database: ") +
                              err.what());
            }
            outcome.ok = false;
            outcome.message =
                "Could not back up your current data, so nothing was changed. "
                "Free some disk space and try again.";
            return outcome;
        }
    }

    // Step 3. Snapshot the incoming file into place beside the destination. Doing this through
    // VACUUM INTO rather than a copy is what makes the swap safe: an import source taken from a
    // running Snapback has a -wal beside it, and copying only the .db would silently drop the
    // most recent sessions — the exact data loss this feature exists to prevent.
    try {
        RawDb source(incoming, SQLITE_OPEN_READONLY);
        if (!source.ok()) throw std::runtime_error("could not reopen the file to import");
        vacuum_into(source.get(), staging);
    } catch (const std::exception& err) {
        if (logger) {
            logger->error(std::string("import: could not stage the incoming database: ") +
                          err.what());
        }
        std::filesystem::remove(staging, ec);
        outcome.ok = false;
        outcome.message =
            "Could not read that database all the way through, so nothing was changed. "
            "The file may be damaged.";
        return outcome;
    }

    // Step 4. The only destructive moment, and by now the replacement is a verified file one
    // rename away. The old -wal/-shm must go with it: they describe the database that was just
    // replaced, and SQLite would otherwise try to apply them to the new one.
    std::filesystem::remove(std::filesystem::path(db_path.string() + "-wal"), ec);
    std::filesystem::remove(std::filesystem::path(db_path.string() + "-shm"), ec);
    std::filesystem::rename(staging, db_path, ec);
    if (ec) {
        if (logger) logger->error("import: could not replace the database: " + ec.message());
        std::error_code cleanup;
        std::filesystem::remove(staging, cleanup);
        outcome.ok = false;
        outcome.message =
            "Could not replace the database file. Your existing data is unchanged and a backup "
            "is at " + backup.string() + ".";
        return outcome;
    }

    // Step 5. Bring an older import up to this build's schema. `Storage::open` migrates, and
    // 7.22's own pre-migration backup runs inside it, so an import that is two versions behind
    // gets the same protection an upgrade would have.
    outcome.schema_version = candidate.schema_version;
    outcome.session_count = candidate.session_count;
    outcome.backup_path = backup;

    if (candidate.schema_version < kSchemaVersion) {
        auto migrated = Storage::open(directory, logger);
        if (!migrated) {
            // The imported file is in place but unusable. Say exactly that, and name the backup:
            // the user's own history is still recoverable and they need to know it.
            outcome.ok = false;
            outcome.message =
                "Imported the database, but it could not be upgraded to this version's format. "
                "Your previous data is still saved at " + backup.string() + ".";
            return outcome;
        }
        outcome.schema_version = migrated->schema_version();
    }

    std::ostringstream msg;
    msg << "Imported " << outcome.session_count
        << (outcome.session_count == 1 ? " session" : " sessions")
        << ". Your previous data was replaced and saved at " << backup.string() << ".";
    outcome.ok = true;
    outcome.message = msg.str();

    if (logger) {
        logger->info("import: replaced the database from " + incoming.string() + " (" +
                     std::to_string(outcome.session_count) + " sessions, format v" +
                     std::to_string(outcome.schema_version) + ")");
    }
    return outcome;
}

}  // namespace snapback
