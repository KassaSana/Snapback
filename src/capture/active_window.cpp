#include "capture/active_window.hpp"

#include <array>
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace snapback {
namespace {

#if defined(_WIN32)
std::string utf8_from_wide(const wchar_t* value) {
    if (!value || value[0] == L'\0') return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), needed, nullptr, nullptr);
    return out;
}
#endif

std::optional<std::string> run_command(const char* command) {
#if defined(_WIN32)
    (void)command;
    return std::nullopt;
#else
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(command, "r");
    if (!pipe) return std::nullopt;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    const int rc = pclose(pipe);
    if (rc != 0 || output.empty()) return std::nullopt;
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
#endif
}

}  // namespace

std::optional<ActiveWindow> query_active_window() {
#if defined(_WIN32)
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return std::nullopt;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t app_name[MAX_PATH] = {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                 FALSE, pid);
    if (process) {
        if (GetModuleBaseNameW(process, nullptr, app_name, MAX_PATH) == 0) {
            DWORD size = MAX_PATH;
            QueryFullProcessImageNameW(process, 0, app_name, &size);
        }
        CloseHandle(process);
    }

    ActiveWindow win;
    win.app_name = utf8_from_wide(app_name);
    win.window_title = utf8_from_wide(title);
    return win;
#elif defined(__APPLE__)
    auto output = run_command(
        "osascript "
        "-e 'tell application \"System Events\" to set frontApp to name of first application process whose frontmost is true' "
        "-e 'tell application \"System Events\" to tell process frontApp to set winTitle to name of front window' "
        "-e 'return frontApp & tab & winTitle' 2>/dev/null");
    if (!output) return std::nullopt;
    const auto tab = output->find('\t');
    if (tab == std::string::npos) return std::nullopt;
    ActiveWindow win;
    win.app_name = output->substr(0, tab);
    win.window_title = output->substr(tab + 1);
    return win;
#elif defined(__linux__)
    auto output = run_command(
        "sh -c 'wid=$(xdotool getactivewindow 2>/dev/null) || exit 1; "
        "title=$(xdotool getwindowname \"$wid\" 2>/dev/null); "
        "pid=$(xdotool getwindowpid \"$wid\" 2>/dev/null); "
        "app=$(ps -p \"$pid\" -o comm= 2>/dev/null); "
        "printf \"%s\\t%s\\n\" \"$app\" \"$title\"'");
    if (!output) return std::nullopt;
    const auto tab = output->find('\t');
    if (tab == std::string::npos) return std::nullopt;
    ActiveWindow win;
    win.app_name = output->substr(0, tab);
    win.window_title = output->substr(tab + 1);
    return win;
#else
    return std::nullopt;
#endif
}

}  // namespace snapback
