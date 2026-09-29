// Owner-only protection for the app's local data.
//
// The directory is the boundary: a 0700 directory cannot be traversed by others, so files
// beneath it are unreachable regardless of their modes (tightening files is defence in depth).
// Created race-free with mkdir(0700), not create-then-chmod. With no home directory, fail
// closed rather than fall back to a shared temp path.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace snapback {

// What preparing the data root did and could not do; failures are reported, never claimed as
// protected.
struct PrivateDirResult {
    bool ok = false;
    // Why not, in a sentence a user can act on. Empty when ok.
    std::string reason;
    // Entries whose permissions were widened before and have now been tightened. Non-empty on
    // the first run after an upgrade from a build that created them with defaults.
    std::vector<std::string> repaired;
    // Entries that are still readable by someone else and could not be changed. The directory
    // may still be private; these are individually exposed if they ever leave it.
    std::vector<std::string> unprotected;

    [[nodiscard]] bool fully_private() const { return ok && unprotected.empty(); }
};

// The POSIX permission rules as pure functions, so the decisions are testable everywhere.

// Owner-only: 0700 for a directory (traversal needs the execute bit), 0600 for a file.
constexpr int private_mode_for(bool is_directory) { return is_directory ? 0700 : 0600; }

// True when any group or other permission bit is set — i.e. when someone who is not the owner
// can reach it. Read, write, and execute all count: execute on a directory is traversal, which
// is the bit that exposes everything beneath it.
constexpr bool mode_exposes_others(int mode) { return (mode & 0077) != 0; }

// Creates `dir` owner-only, or verifies and repairs it. Fails when the path is not a directory,
// is a symlink/reparse point, or is owned by another account (substitution attacks). On
// Windows, checks the root is inside the user's own profile.
PrivateDirResult prepare_private_dir(const std::filesystem::path& dir);

// Tightens one existing entry to owner-only. Returns false and fills `error` when it cannot,
// so the caller can report it rather than assume it worked. A no-op on Windows, where file
// permissions are inherited from the directory ACL this module already checks.
bool make_private(const std::filesystem::path& entry, std::string& error);

// True if `entry` is readable or writable by group or other. Always false on Windows, where
// the equivalent question is about the containing directory's ACL.
bool is_group_or_world_accessible(const std::filesystem::path& entry);

// Resolves the app data directory, or explains why there is no safe one.
struct DataDirChoice {
    std::filesystem::path path;
    bool ok = false;
    std::string reason;
};
DataDirChoice choose_data_dir(const std::filesystem::path& override_dir,
                              const std::filesystem::path& home_dir);

}  // namespace snapback
