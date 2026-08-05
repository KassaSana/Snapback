#include "app/autostart_run_key.hpp"

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

namespace snapback::run_key {

std::wstring user_run_key_path() {
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

std::wstring value_name() { return L"Snapback"; }

std::wstring current_executable_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (len == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(len);
    return buffer;
}

bool entry_present(const std::wstring& key_path) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        // An absent key means "not registered", not "failed to answer". Reads must be total.
        return false;
    }
    const LONG result =
        RegQueryValueExW(key, value_name().c_str(), nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool install_entry(const std::wstring& key_path, const std::wstring& executable) {
    if (executable.empty()) return false;

    // Create rather than open: the real Run key always exists, but a scratch key handed in by
    // a test does not until something makes it. RegCreateKeyEx opens an existing key
    // unchanged, so this is not a special case for tests.
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring command = L"\"" + executable + L"\"";
    const auto* bytes = reinterpret_cast<const BYTE*>(command.c_str());
    const DWORD size = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const bool ok =
        RegSetValueExW(key, value_name().c_str(), 0, REG_SZ, bytes, size) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

bool remove_entry(const std::wstring& key_path) {
    HKEY key = nullptr;
    const LONG opened =
        RegOpenKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return true;  // no key, so nothing registered
    if (opened != ERROR_SUCCESS) return false;

    const LONG result = RegDeleteValueW(key, value_name().c_str());
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;  // already absent = ok
}

bool delete_key(const std::wstring& key_path) {
    const LONG result = RegDeleteKeyW(HKEY_CURRENT_USER, key_path.c_str());
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

}  // namespace snapback::run_key

#endif  // _WIN32
