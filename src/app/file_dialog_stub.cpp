#if !defined(_WIN32) && !defined(__APPLE__)

#include "app/file_dialog.hpp"

namespace snapback {

bool file_dialog_supported() {
    return false;
}

FileDialogResult pick_open_file(const FileDialogOptions&) {
    return FileDialogResult{false, false, "", "Native file dialogs not supported on this platform"};
}

FileDialogResult pick_save_file(const FileDialogOptions&) {
    return FileDialogResult{false, false, "", "Native file dialogs not supported on this platform"};
}

namespace detail {

FileDialogResult pick_open_file_native(const FileDialogOptions&) {
    return FileDialogResult{false, false, "", "Native file dialogs not supported on this platform"};
}

FileDialogResult pick_save_file_native(const FileDialogOptions&) {
    return FileDialogResult{false, false, "", "Native file dialogs not supported on this platform"};
}

}  // namespace detail
}  // namespace snapback

#endif  // !_WIN32 && !__APPLE__
