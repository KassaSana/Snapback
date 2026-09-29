// Reveal a directory in the OS file manager, so the local data is actually inspectable.
//
//  1. A path is data, never program text: no system()/popen(). Windows uses ShellExecuteW,
//     macOS NSWorkspace, POSIX spawns xdg-open with an argv array.
//  2. Refuse rather than guess: a missing path or non-directory returns false.
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

// The per-platform seam; reveal_directory owns validation. The macOS backend lives in
// reveal_path_macos.mm because it must be Objective-C++.
bool reveal_existing_directory(const std::filesystem::path& dir);

}  // namespace detail

}  // namespace snapback
