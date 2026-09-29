// Flush a file, and after rename its directory, to durable storage. Rename alone prevents a
// half-written file being seen, not a reported save being lost on power failure. Apply to the
// temp file before rename and to the parent directory after.
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
