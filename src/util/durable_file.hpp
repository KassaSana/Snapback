// Flush a file (and, after rename, its directory) to durable storage. Roadmap 7.21.
//
// Atomic rename (7.19) stops a half-written settings.json from being observed. It does not
// stop a power loss from discarding a save the app already reported as succeeded: without a
// sync, the new bytes and the directory entry that points at them can still sit in the OS
// cache. This is the missing half.
//
// Applied to the temp file *before* rename and to the parent directory *after* — the directory
// entry is a separate write, so syncing only the file still permits losing the rename.
#pragma once

#include <filesystem>

namespace snapback {

// Flush the named file's contents (and metadata the platform couples to that flush) to disk.
// Returns false when the path cannot be opened or the OS refuses the flush.
bool durable_sync_file(const std::filesystem::path& path);

// Flush the directory entry updates under `dir` so a just-completed rename is as durable as
// the file contents. Returns false on open/flush failure.
bool durable_sync_directory(const std::filesystem::path& dir);

}  // namespace snapback
