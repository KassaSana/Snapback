// Native file pickers (Win32 common dialogs, AppKit panels). Never starts a shell; a cancel is
// reported as { ok: false, cancelled: true }, never thrown.
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
