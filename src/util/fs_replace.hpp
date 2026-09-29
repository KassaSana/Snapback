// Portable "copy over an existing file". libstdc++ on MinGW ignores
// copy_options::overwrite_existing (throwing, or with an error_code silently not copying), so
// remove the destination first.
#pragma once

#include <filesystem>
#include <system_error>

namespace snapback {

// Replaces `to` with a copy of `from`; throws filesystem_error on failure. Not atomic: use
// rename where atomicity matters.
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
