#include "app/file_dialog.hpp"

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
#include <commdlg.h>

#include <filesystem>
#include <string>
#include <vector>

namespace snapback {
namespace {

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n,
                        nullptr, nullptr);
    return s;
}

std::vector<wchar_t> build_filter_buffer(const std::vector<FileDialogFilter>& filters) {
    std::vector<wchar_t> buf;
    if (filters.empty()) {
        const std::wstring all_files_name = L"All Files (*.*)";
        const std::wstring all_files_pattern = L"*.*";
        buf.insert(buf.end(), all_files_name.begin(), all_files_name.end());
        buf.push_back(L'\0');
        buf.insert(buf.end(), all_files_pattern.begin(), all_files_pattern.end());
        buf.push_back(L'\0');
    } else {
        for (const auto& filter : filters) {
            std::wstring name = to_wide(filter.name.empty() ? filter.pattern : filter.name);
            std::wstring pattern = to_wide(filter.pattern.empty() ? "*.*" : filter.pattern);
            buf.insert(buf.end(), name.begin(), name.end());
            buf.push_back(L'\0');
            buf.insert(buf.end(), pattern.begin(), pattern.end());
            buf.push_back(L'\0');
        }
    }
    buf.push_back(L'\0');
    return buf;
}

}  // namespace

bool file_dialog_supported() {
    return true;
}

FileDialogResult pick_open_file(const FileDialogOptions& options) {
    return detail::pick_open_file_native(options);
}

FileDialogResult pick_save_file(const FileDialogOptions& options) {
    return detail::pick_save_file_native(options);
}

namespace detail {

FileDialogResult pick_open_file_native(const FileDialogOptions& options) {
    wchar_t file_buf[MAX_PATH * 2] = {0};

    if (!options.default_name.empty()) {
        std::wstring def_name = to_wide(options.default_name);
        wcsncpy_s(file_buf, def_name.c_str(), _countof(file_buf) - 1);
    }

    std::wstring title_w = to_wide(options.title.empty() ? "Open File" : options.title);
    std::wstring initial_dir_w = to_wide(options.default_path);
    auto filter_buf = build_filter_buffer(options.filters);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = nullptr;  // Application modal / unparented
    ofn.lpstrFilter = filter_buf.data();
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = static_cast<DWORD>(_countof(file_buf));
    ofn.lpstrInitialDir = initial_dir_w.empty() ? nullptr : initial_dir_w.c_str();
    ofn.lpstrTitle = title_w.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        return FileDialogResult{true, false, to_utf8(file_buf), ""};
    }

    const DWORD err = CommDlgExtendedError();
    if (err == 0) {
        return FileDialogResult{false, true, "", "Cancelled by user"};
    }
    return FileDialogResult{false, false, "", "File dialog error: " + std::to_string(err)};
}

FileDialogResult pick_save_file_native(const FileDialogOptions& options) {
    wchar_t file_buf[MAX_PATH * 2] = {0};

    if (!options.default_name.empty()) {
        std::wstring def_name = to_wide(options.default_name);
        wcsncpy_s(file_buf, def_name.c_str(), _countof(file_buf) - 1);
    }

    std::wstring title_w = to_wide(options.title.empty() ? "Save File" : options.title);
    std::wstring initial_dir_w = to_wide(options.default_path);
    auto filter_buf = build_filter_buffer(options.filters);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filter_buf.data();
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = static_cast<DWORD>(_countof(file_buf));
    ofn.lpstrInitialDir = initial_dir_w.empty() ? nullptr : initial_dir_w.c_str();
    ofn.lpstrTitle = title_w.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetSaveFileNameW(&ofn)) {
        return FileDialogResult{true, false, to_utf8(file_buf), ""};
    }

    const DWORD err = CommDlgExtendedError();
    if (err == 0) {
        return FileDialogResult{false, true, "", "Cancelled by user"};
    }
    return FileDialogResult{false, false, "", "File dialog error: " + std::to_string(err)};
}

}  // namespace detail
}  // namespace snapback

#endif  // _WIN32
