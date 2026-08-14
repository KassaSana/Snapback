// Native OS File Dialogs for Export & Import. Roadmap 10.14.
//
// Allows users to choose a file via the native platform file chooser (Win32 Common Dialogs
// on Windows, AppKit panels on macOS) without typing raw paths.
//
// Rules:
//   1. Never start a shell.
//   2. Non-throwing: user cancellation is reported as { ok: false, cancelled: true }
//      rather than an exception across the IPC boundary.
//   3. Supports custom titles, initial directories, and file extension filters.
#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace snapback {

// Check whether native file dialogs are supported on the current build.
bool file_dialog_supported();

// Prompt the user to pick an existing file to open/import.
FileDialogResult pick_open_file(const FileDialogOptions& options);

// Prompt the user to choose a destination file path to save/export.
FileDialogResult pick_save_file(const FileDialogOptions& options);

namespace detail {

// Platform-specific file picker implementations.
FileDialogResult pick_open_file_native(const FileDialogOptions& options);
FileDialogResult pick_save_file_native(const FileDialogOptions& options);

}  // namespace detail

}  // namespace snapback
