#include "app/autostart.hpp"

#include "app/autostart_launchd.hpp"
#include "app/autostart_systemd.hpp"

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
#endif

namespace snapback {

#if defined(_WIN32)
namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"Snapback";

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

}  // namespace

bool autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LONG result = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool autostart_supported() { return true; }

bool set_autostart_enabled(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enabled) {
        const auto executable_path = current_executable_path();
        if (executable_path.empty()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + executable_path + L"\"";
        const auto* bytes = reinterpret_cast<const BYTE*>(command.c_str());
        const DWORD size = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        ok = RegSetValueExW(key, kValueName, 0, REG_SZ, bytes, size) == ERROR_SUCCESS;
    } else {
        const LONG result = RegDeleteValueW(key, kValueName);
        ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;  // already absent = ok
    }
    RegCloseKey(key);
    return ok;
}

#elif defined(__APPLE__)  // Roadmap 3.0

// The mechanism lives in autostart_launchd.cpp so its plist text and install/remove logic are
// compiled and tested on every OS; this file only decides *where* the agent goes.
bool autostart_enabled() { return launchd::agent_installed(launchd::user_agent_dir()); }

bool autostart_supported() { return true; }

bool set_autostart_enabled(bool enabled) {
    const auto dir = launchd::user_agent_dir();
    if (dir.empty()) return false;  // no HOME: not a login session, so no login item
    if (!enabled) return launchd::remove_agent(dir);

    const auto executable = launchd::current_executable_path();
    if (executable.empty()) return false;
    return launchd::install_agent(dir, executable);
}

#elif defined(__linux__)  // Roadmap 3.0

// Same shape as the launchd branch: the mechanism is in autostart_systemd.cpp so it is
// compiled and tested on all three CI hosts, and this file only chooses the directory.
bool autostart_enabled() { return systemd::unit_enabled(systemd::user_unit_dir()); }

bool autostart_supported() { return true; }

bool set_autostart_enabled(bool enabled) {
    const auto dir = systemd::user_unit_dir();
    if (dir.empty()) return false;  // neither XDG_CONFIG_HOME nor HOME: no user session
    if (!enabled) return systemd::remove_unit(dir);

    const auto executable = systemd::current_executable_path();
    if (executable.empty()) return false;
    return systemd::install_unit(dir, executable);
}

#else  // no start-on-login backend on this platform

bool autostart_enabled() { return false; }

bool autostart_supported() { return false; }

bool set_autostart_enabled(bool) { return false; }

#endif

}  // namespace snapback
