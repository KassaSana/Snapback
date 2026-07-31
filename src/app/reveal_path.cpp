#include "app/reveal_path.hpp"

#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#elif !defined(__APPLE__)
#include <spawn.h>
#include <sys/wait.h>

#include <vector>

// POSIX guarantees this symbol but not a declaration in every header set.
extern char** environ;
#endif

namespace snapback {

bool reveal_supported() {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    return true;
#else
    return false;
#endif
}

bool reveal_directory(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    if (!reveal_supported()) return false;

    // The error_code overload, so a permission error on a parent directory returns false
    // instead of throwing filesystem_error across an IPC handler.
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return false;

    return detail::reveal_existing_directory(dir);
}

namespace detail {

#if defined(_WIN32)

bool reveal_existing_directory(const std::filesystem::path& dir) {
    // No shell: the path crosses as one wide-string argument, so nothing inside it can be
    // read as a command. `SW_SHOWNORMAL` because the user asked for a visible window.
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", dir.native().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // ShellExecuteW returns a pseudo-handle, not a real HINSTANCE; values <= 32 are the
    // documented error sentinels.
    return reinterpret_cast<INT_PTR>(result) > 32;
}

#elif !defined(__APPLE__)

bool reveal_existing_directory(const std::filesystem::path& dir) {
    const auto argv_owned = file_manager_argv(dir);
    std::vector<char*> argv;
    argv.reserve(argv_owned.size() + 1);
    // execv's signature predates const-correctness; it does not modify these.
    for (const auto& arg : argv_owned) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    // posix_spawnp rather than fork()+exec: Snapback forks with a capture thread running and
    // a SQLite connection open, and a forked child inherits locks it must never touch. This
    // call is the fork/exec pair fused into one operation that can only ever exec.
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return false;

    // Reaped rather than abandoned, or the zombie outlives the click. The wait is bounded in
    // practice because xdg-open hands the URL to the desktop's opener and exits; a desktop
    // whose opener blocks would block this handler, which is one more reason Linux is post-v1
    // (ADR-0002) and gets the simple version.
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#endif  // platform backends (macOS lives in reveal_path_macos.mm)

}  // namespace detail

}  // namespace snapback
