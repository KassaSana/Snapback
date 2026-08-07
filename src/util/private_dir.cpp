#include "util/private_dir.hpp"

#include <system_error>

#if defined(_WIN32)
#include <cstdlib>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace snapback {
namespace {

#if !defined(_WIN32)
// Derived from the pure rules in the header rather than restated, so the tested definition and
// the applied one cannot drift.
constexpr ::mode_t kPrivateDirMode = static_cast<::mode_t>(private_mode_for(true));
constexpr ::mode_t kPrivateFileMode = static_cast<::mode_t>(private_mode_for(false));
#endif

std::string describe(const std::filesystem::path& path) { return path.string(); }

}  // namespace

bool is_group_or_world_accessible(const std::filesystem::path& entry) {
#if defined(_WIN32)
    (void)entry;
    // Windows has no umask and no mode bits. The equivalent exposure is the containing
    // directory's ACL, which prepare_private_dir checks; answering "no" here would be a lie
    // dressed as a check, so this states plainly that the question does not apply.
    return false;
#else
    struct ::stat info {};
    // lstat, not stat: a symlink pointing at something world-readable is itself the problem,
    // and stat would report the target's mode as though the link were safe.
    if (::lstat(entry.string().c_str(), &info) != 0) return false;
    return mode_exposes_others(static_cast<int>(info.st_mode));
#endif
}

bool make_private(const std::filesystem::path& entry, std::string& error) {
#if defined(_WIN32)
    (void)entry;
    (void)error;
    return true;
#else
    struct ::stat info {};
    if (::lstat(entry.string().c_str(), &info) != 0) {
        error = "could not read permissions for " + describe(entry);
        return false;
    }
    // Never follow a symlink to chmod its target: that would let a link planted in the data
    // directory retarget the permission change at a file outside it.
    if (S_ISLNK(info.st_mode)) {
        error = describe(entry) + " is a symbolic link and was left alone";
        return false;
    }
    const ::mode_t wanted = S_ISDIR(info.st_mode) ? kPrivateDirMode : kPrivateFileMode;
    if ((info.st_mode & 07777) == wanted) return true;
    if (::chmod(entry.string().c_str(), wanted) != 0) {
        error = "could not restrict permissions on " + describe(entry);
        return false;
    }
    // Verified rather than assumed: chmod can succeed on a filesystem that does not store the
    // bits (some network and FAT mounts), and reporting success there would claim a protection
    // that is not present.
    struct ::stat after {};
    if (::lstat(entry.string().c_str(), &after) != 0 ||
        mode_exposes_others(static_cast<int>(after.st_mode))) {
        error = describe(entry) + " does not keep permissions (is it on a non-POSIX filesystem?)";
        return false;
    }
    return true;
#endif
}

PrivateDirResult prepare_private_dir(const std::filesystem::path& dir) {
    PrivateDirResult result;
    if (dir.empty()) {
        result.reason = "no data directory was provided";
        return result;
    }

    std::error_code ec;
    const auto status = std::filesystem::symlink_status(dir, ec);
    const bool exists = !ec && status.type() != std::filesystem::file_type::not_found;

    if (exists) {
        // A symlink or reparse point where the data root should be is a substitution attack on
        // a predictable path, not a configuration to adopt. There is no safe way to "repair"
        // it, so this fails closed rather than following it.
        if (status.type() == std::filesystem::file_type::symlink) {
            result.reason = describe(dir) + " is a symbolic link, which is not a safe place " +
                            "for private data. Move or remove it and restart.";
            return result;
        }
        if (status.type() != std::filesystem::file_type::directory) {
            result.reason = describe(dir) + " exists and is not a directory.";
            return result;
        }
    }

#if !defined(_WIN32)
    if (!exists) {
        // mkdir applies the mode as the directory comes into being. umask can only clear bits,
        // and 0700 has none to clear beyond the owner's, so no umask can widen this. The
        // alternative — create_directories then chmod — leaves the directory readable for the
        // interval between the two calls.
        std::filesystem::create_directories(dir.parent_path(), ec);
        if (::mkdir(dir.string().c_str(), kPrivateDirMode) != 0) {
            result.reason = "could not create " + describe(dir);
            return result;
        }
    }

    struct ::stat info {};
    if (::lstat(dir.string().c_str(), &info) != 0) {
        result.reason = "could not read permissions for " + describe(dir);
        return result;
    }
    // Another account owning the directory means it was created for us, which is the same
    // substitution problem as the symlink above.
    if (info.st_uid != ::geteuid()) {
        result.reason = describe(dir) + " is owned by another user account.";
        return result;
    }
    if (mode_exposes_others(static_cast<int>(info.st_mode))) {
        std::string error;
        if (!make_private(dir, error)) {
            result.reason = error;
            return result;
        }
        result.repaired.push_back(describe(dir));
    }
#else
    if (!exists) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            result.reason = "could not create " + describe(dir) + ": " + ec.message();
            return result;
        }
    }
#endif

    // Sweep what is already inside. The directory mode is what actually protects these on
    // POSIX, but an install upgraded from a build that created them with defaults still has
    // individually-readable files, and they stay readable if one is ever copied out.
    std::error_code walk_ec;
    for (std::filesystem::recursive_directory_iterator it(
             dir, std::filesystem::directory_options::skip_permission_denied, walk_ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(walk_ec)) {
        if (walk_ec) break;
        if (!is_group_or_world_accessible(it->path())) continue;
        std::string error;
        if (make_private(it->path(), error)) {
            result.repaired.push_back(describe(it->path()));
        } else {
            // Reported, not swallowed: an entry we could not tighten must not be counted as
            // protected. 8.13 is explicit that a partial repair has to say so.
            result.unprotected.push_back(error);
        }
    }

    result.ok = true;
    return result;
}

DataDirChoice choose_data_dir(const std::filesystem::path& override_dir,
                              const std::filesystem::path& home_dir) {
    DataDirChoice choice;
    if (!override_dir.empty()) {
        // An explicit SNAPBACK_DATA_DIR is the user's own decision about where their data
        // lives. It still gets the owner-only treatment; it just does not get second-guessed.
        choice.path = override_dir;
        choice.ok = true;
        return choice;
    }
    if (!home_dir.empty()) {
        choice.path = home_dir;
        choice.ok = true;
        return choice;
    }

    // Roadmap 8.13. This used to be `<temp>/snapback` on both platforms — the same predictable
    // path for every account, inside a directory every local account can write to. A user with
    // no HOME (or no APPDATA) got a working app quietly recording their window titles
    // somewhere anyone could read, with nothing said. There is no safe automatic answer, so
    // there is no automatic answer.
    choice.reason =
        "Snapback could not find your user profile directory, so it has nowhere private to "
        "keep your data. Set SNAPBACK_DATA_DIR to a directory only you can read, then restart.";
    return choice;
}

}  // namespace snapback
