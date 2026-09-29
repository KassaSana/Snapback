// Import a database from a file. Replace, not merge: the incoming history replaces the current
// one, which is kept as a backup. An older file is migrated forward; a newer one is refused up
// front. Everything goes through VACUUM INTO (a consistent snapshot) rather than copying bytes,
// since a copy of a live WAL database can be torn.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace snapback {

class Logger;

// Why a candidate file cannot be imported. Distinct values rather than a bare bool because the
// UI has to say something the user can act on, and "that file is from a newer Snapback" and
// "that file is not a database" call for completely different next steps.
enum class ImportRejection {
    kNone,
    kMissingFile,
    kNotADatabase,
    kNotSnapbackData,
    kSchemaTooNew,
    kSameFile,
};

struct ImportCandidate {
    bool acceptable = false;
    ImportRejection rejection = ImportRejection::kMissingFile;
    // User-facing, already phrased for display. Empty when `acceptable`.
    std::string message;
    // `user_version` carried by the incoming file. 0 for a pre-versioning or fresh database.
    int schema_version = 0;
    // What the user is about to adopt, so the confirmation can state it. A replace that says
    // "this will discard your history" should be able to say what it is replacing it *with*.
    std::int64_t session_count = 0;
};

// Read-only inspection on its own connection, safe for a preview. Refuses importing the live
// database over itself.
ImportCandidate inspect_import_candidate(const std::filesystem::path& incoming,
                                         const std::filesystem::path& current_db_path);

struct ImportOutcome {
    bool ok = false;
    // User-facing result, success or failure.
    std::string message;
    // Where the *replaced* database was kept. Populated on success, and the reason an import
    // is recoverable: the user's previous history is still on disk under this name.
    std::filesystem::path backup_path;
    int schema_version = 0;
    std::int64_t session_count = 0;
};

// Replace the database at `db_path` with `incoming`. The live Storage must be closed first.
//
//   1. inspect the candidate; refuse before touching anything
//   2. snapshot the current database to `backup_path`
//   3. snapshot the incoming file to a staging path (validates it, drops -wal baggage)
//   4. move staging over the destination and remove the old -wal/-shm
//   5. open the result and migrate it forward if older
//
// Failures at 1-3 change nothing the app reads.
ImportOutcome import_database(const std::filesystem::path& incoming,
                              const std::filesystem::path& db_path, Logger* logger);

// The name the replaced database is kept under, beside the live one. Exposed so the UI can name
// it in the confirmation and the tests can assert it without duplicating the string.
std::string replaced_database_backup_name();

// The name a verified import waits under between being chosen and being applied.
std::string staged_import_name();

// --------------------------------------------------------------------------- Staging: the
// running app cannot close its own database, so an import is verified and staged now and
// applied at the next launch before anything opens the database. Deleting the staged file
// undoes it. ---------------------------------------------------------------------------

struct StageOutcome {
    bool ok = false;
    std::string message;
    int schema_version = 0;
    std::int64_t session_count = 0;
};

// Verify `incoming` and park a consistent copy of it beside the live database. Safe to call
// while Snapback is running: it never touches the database in use.
StageOutcome stage_import(const std::filesystem::path& incoming,
                          const std::filesystem::path& db_path, Logger* logger);

// Discard a staged import that has not been applied yet. Returns true if one was removed.
bool cancel_staged_import(const std::filesystem::path& db_path);

// True when a verified import is parked and waiting for the next launch.
bool has_staged_import(const std::filesystem::path& db_path);

// Apply a staged import, if there is one. **Call before opening Storage**, early in startup.
// Returns nullopt when nothing was staged, which is the ordinary case on every launch.
std::optional<ImportOutcome> apply_staged_import(const std::filesystem::path& db_path,
                                                 Logger* logger);

}  // namespace snapback
