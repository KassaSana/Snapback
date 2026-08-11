// "Get my data back." Roadmap 9.14.
//
// Snapback has four ways to get data out and, until this, none to get it back in. For a
// cloud-synced product that is a non-feature; for this one it is a hole in the central promise.
// 1.6 and the onboarding wizard both say the data is local and yours, and local-only also means
// **nothing else is holding a copy**. A new laptop, a reinstall, or a restored disk image loses
// the history outright — and history *is* the product: trends, streaks, and the Tier 13 corpus
// are all derived from it.
//
// **Scope: replace, not merge. Stated here so it cannot be assumed otherwise.**
// Importing swaps the whole database for the incoming one. Merging two histories is a genuinely
// larger problem — session UUIDs will not collide, but retention windows, duplicate detection,
// and conflicting settings all need answers this item deliberately does not attempt. An import
// therefore ends with exactly the incoming history, and the replaced database is kept as a
// backup rather than discarded.
//
// Two things make the obvious manual workaround worse than it looks, and both shape the design:
//
//  - Copying `focoflow.db` by hand works only until the schema versions differ, and 7.3's
//    downgrade guard then refuses the file — correctly, but with no way forward. So an import
//    of an *older* file migrates it up, and an import of a *newer* one is refused here, with an
//    explanation, rather than after the swap when the app can no longer open its own database.
//  - 9.8's process-lifetime lock means a copy taken while Snapback is running can be a torn
//    read of a WAL database. Everything here goes through `VACUUM INTO`, which asks SQLite for
//    a consistent single-file snapshot, rather than copying bytes.
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

// Read-only inspection. Opens the candidate on its own connection and never writes to it, so
// this is safe to call for a preview before the user commits to anything.
//
// `current_db_path` is compared against the candidate so importing the live database over
// itself is refused rather than performing a destructive no-op.
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

// Replace the database at `db_path` with the contents of `incoming`.
//
// **The live Storage must be closed before calling this.** The file is replaced underneath, and
// an open SQLite connection to the old inode would keep writing to a database that is no longer
// the app's. The caller owns that ordering; this function owns everything after it.
//
// Order is chosen so that every failure leaves the user with a working database:
//   1. inspect the candidate, and refuse before touching anything
//   2. snapshot the current database to the returned `backup_path` (VACUUM INTO)
//   3. snapshot the *incoming* file to a staging path beside the destination — this both
//      validates it end to end and yields a clean single file with no -wal baggage
//   4. move staging over the destination, and remove the old -wal/-shm, which belong to the
//      database that was just replaced and would otherwise be applied to the new one
//   5. open the result and migrate it forward if it is older
//
// A failure at 1-3 has changed nothing the app reads. Only 4 is destructive, and by then the
// replacement is a verified file sitting beside its destination.
ImportOutcome import_database(const std::filesystem::path& incoming,
                              const std::filesystem::path& db_path, Logger* logger);

// The name the replaced database is kept under, beside the live one. Exposed so the UI can name
// it in the confirmation and the tests can assert it without duplicating the string.
std::string replaced_database_backup_name();

// The name a verified import waits under between being chosen and being applied.
std::string staged_import_name();

// ---------------------------------------------------------------------------
// Staging: why the swap does not happen while the app is running.
//
// `import_database` requires the live Storage closed, and the running app cannot close it
// underneath itself — on Windows the rename would simply fail against the open handle, and on
// every platform the engine thread would carry on writing into a database that is no longer the
// one the user is looking at. 9.8's process-lifetime lock exists to prevent exactly that class
// of confusion, so this respects it rather than working around it.
//
// So the choice is verified and staged now, and applied at the next launch before anything opens
// the database. That also gives the user a free undo: a staged import that has not been applied
// is one file deletion away from never having happened.
// ---------------------------------------------------------------------------

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
