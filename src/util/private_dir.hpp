// Owner-only ownership of the app's local data. Roadmap 8.13.
//
// Everything Snapback records about a person — window titles, session history, the readable
// export — lives under one directory, created with ordinary `create_directories` /
// `ofstream` / SQLite defaults. On POSIX those defaults are `mode & ~umask`, and the common
// umask of `022` leaves the directory `0755` and its files `0644`: on a shared machine, every
// other local account can read what the user was doing all day. The lock file was the only
// artifact that ever asked for `0600`.
//
// **The directory is the boundary, and that is deliberate.** On POSIX a `0700` directory
// cannot be traversed by anyone else, so every file beneath it is unreachable regardless of
// its own mode. Tightening individual files is defence in depth against a directory that is
// later loosened or a file that is later moved out — worth doing, but not the mechanism.
//
// **Race-safe creation, not create-then-tighten.** `mkdir(0700)` applies the mode as the
// directory comes into existence; `umask` can only clear bits, never add them, so no umask
// can widen it. Creating at `0755` and calling `chmod` afterwards leaves a window in which
// the directory is readable, which is exactly the window an attacker on a shared machine
// wants.
//
// **Fail closed, never fall back to a shared path.** With no home directory the app used to
// land on `<temp>/snapback` on both platforms — a predictable path in a directory every local
// account can write to. That is worse than not starting: the user gets a working app that is
// quietly recording their window titles somewhere anyone can read. There is no safe automatic
// answer, so this reports why instead of inventing one.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace snapback {

// What preparing the data root did, and what it could not do.
//
// `repaired` and `unprotected` are reported rather than summarized into a bool because the
// item is explicit that an install whose permissions cannot be changed must be *reported*
// rather than claimed as protected. Saying "secured" over an entry we failed to touch is the
// specific failure this structure prevents.
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

// The POSIX permission rules, as pure functions.
//
// Split out so the *decisions* are testable on every platform, not only the two that compile
// the syscalls. What is left platform-specific is plumbing — `lstat`, `mkdir`, `chmod` — while
// "is this exposed" and "what should it be" can be checked on the machine in front of you.
// Without this the substance of 8.13 would be verifiable only in CI.

// Owner-only: 0700 for a directory (traversal needs the execute bit), 0600 for a file.
constexpr int private_mode_for(bool is_directory) { return is_directory ? 0700 : 0600; }

// True when any group or other permission bit is set — i.e. when someone who is not the owner
// can reach it. Read, write, and execute all count: execute on a directory is traversal, which
// is the bit that exposes everything beneath it.
constexpr bool mode_exposes_others(int mode) { return (mode & 0077) != 0; }

// Creates `dir` as an owner-only directory, or verifies and repairs an existing one.
//
// Fails (rather than repairing) when the path exists and is not a directory, is a symlink or
// reparse point, or is owned by another account — all three are substitution attacks on a
// predictable path, and none has a safe automatic resolution.
//
// On Windows there is no umask and `%APPDATA%` is per-user by default; the check there is
// that the resolved root really is inside the user's own profile rather than somewhere with
// an inherited DACL nobody looked at.
PrivateDirResult prepare_private_dir(const std::filesystem::path& dir);

// Tightens one existing entry to owner-only. Returns false and fills `error` when it cannot,
// so the caller can report it rather than assume it worked. A no-op on Windows, where file
// permissions are inherited from the directory ACL this module already checks.
bool make_private(const std::filesystem::path& entry, std::string& error);

// True if `entry` is readable or writable by group or other. Always false on Windows, where
// the equivalent question is about the containing directory's ACL.
bool is_group_or_world_accessible(const std::filesystem::path& entry);

// Resolves the app data directory, or explains why there isn't a safe one.
//
// Split out of `main.cpp` so the fallback rule is testable: the old version silently returned
// a shared temp path, and nothing could observe the difference between "the user's private
// directory" and "a world-writable one with the same name for everybody".
struct DataDirChoice {
    std::filesystem::path path;
    bool ok = false;
    std::string reason;
};
DataDirChoice choose_data_dir(const std::filesystem::path& override_dir,
                              const std::filesystem::path& home_dir);

}  // namespace snapback
