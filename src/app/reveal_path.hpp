// Reveal a directory in the OS file manager. Roadmap 7.6, "open the data folder".
//
// Snapback records window titles and keystroke timing into `focoflow.db`. The promise is that
// the data is local and inspectable, and "inspectable" is only true if the user can actually
// reach the folder — so this sits alongside deleting a session and exporting in a legible form.
//
// Two rules shape the implementation, and both are about a path being untrusted input (it
// contains a user-chosen home directory name):
//
//  1. **A path is data, never program text.** There is no `system()` or `popen()` here.
//     Windows uses `ShellExecuteW` and macOS uses `NSWorkspace` — neither starts a shell, and
//     macOS starts no child process at all. POSIX spawns `xdg-open` with an argv *array*, so a
//     path containing a space, a quote, or `$(...)` is one argument instead of shell syntax.
//  2. **Refuse rather than guess.** A path that is missing or is not a directory returns
//     false, instead of handing the OS something whose error dialog we cannot explain.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace snapback {

// True when this build has a real file-manager backend. False on platforms that fall through
// to the no-op, so the UI can hide the control instead of offering a button that does nothing.
bool reveal_supported();

// Open `dir` in the OS file manager. Returns false when the path is not an existing directory,
// when the platform has no backend, or when the OS refused the request — never throws, because
// this is called from an IPC handler where an exception becomes an opaque error envelope.
bool reveal_directory(const std::filesystem::path& dir);

// Pure, and compiled on every OS so the argv contract is testable off Linux: the exact argument
// vector the POSIX backend passes to `posix_spawnp`. The path occupies its own element — that is
// the whole point, and the reason a test pins it.
inline std::vector<std::string> file_manager_argv(const std::filesystem::path& dir) {
    return {"xdg-open", dir.string()};
}

namespace detail {

// The one per-platform seam. `reveal_directory` above owns the validation so it is written
// once, and every backend may assume `dir` is an existing directory. Implemented in
// reveal_path.cpp for Windows and POSIX, and in reveal_path_macos.mm for AppKit — the split
// exists because the macOS backend has to be Objective-C++ and the rest must not be.
bool reveal_existing_directory(const std::filesystem::path& dir);

}  // namespace detail

}  // namespace snapback
