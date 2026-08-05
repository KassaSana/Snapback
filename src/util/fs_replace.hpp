// Portable "copy over an existing file".
//
// ROADMAP 11.8. `std::filesystem::copy_file(from, to, copy_options::overwrite_existing)`
// looks like the obvious way to replace a file, and it is — except that libstdc++ on MinGW
// does not honour the flag. It throws `filesystem error: cannot copy file: File exists`
// instead of replacing, which is how 11.8's `rollback_model` failure was found.
//
// The failure mode is worse than a hard error when the call site passes an `std::error_code`:
// nothing throws, the copy simply does not happen, and the stale destination survives while
// the caller believes it wrote a fresh one. That is how a stale settings.json.bak went
// unnoticed — the two-save test never reached the case where the backup already existed.
//
// Removing the destination first is portable and means the same thing everywhere.
#pragma once

#include <filesystem>
#include <system_error>

namespace snapback {

// Replaces `to` with a copy of `from`. Throws std::filesystem::filesystem_error on failure.
//
// Not atomic: there is a window where `to` does not exist. Callers that need atomicity want
// std::filesystem::rename instead (see save_app_settings, which renames its staged temp file
// over the destination and uses this only for the best-effort backup copy).
inline void copy_over(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ignored;
    // "Already absent" is success here; a genuine failure surfaces from copy_file below,
    // which reports the real reason rather than a stale "could not remove".
    std::filesystem::remove(to, ignored);
    std::filesystem::copy_file(from, to);
}

// Same, but reports failure through `ec` instead of throwing.
inline void copy_over(const std::filesystem::path& from, const std::filesystem::path& to,
                      std::error_code& ec) {
    std::error_code ignored;
    std::filesystem::remove(to, ignored);
    std::filesystem::copy_file(from, to, ec);
}

}  // namespace snapback
