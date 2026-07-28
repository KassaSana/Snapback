#include "capture/active_window.hpp"

#include <array>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace snapback {

#if defined(_WIN32)
namespace detail {
std::string utf8_from_wide(const wchar_t* value) {
    if (!value || value[0] == L'\0') return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    // `needed` includes the trailing NUL, so the writable buffer must include it too.
    // The previous code allocated needed - 1 bytes and then authorized a needed-byte
    // write, which wrote one byte beyond the string's size.
    std::string out(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), needed, nullptr, nullptr) !=
        needed) {
        return {};
    }
    out.pop_back();
    return out;
}
}  // namespace detail
#endif

namespace {

std::optional<std::string> run_command(const std::string& command) {
#if defined(_WIN32)
    (void)command;
    return std::nullopt;
#else
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
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

#if defined(_WIN32)
std::optional<ActiveWindow> query_active_window_for_hwnd(HWND hwnd) {
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
    win.app_name = detail::utf8_from_wide(app_name);
    win.window_title = detail::utf8_from_wide(title);
    return win;
}
#endif

#if defined(__APPLE__)
// Ask a browser for its own tab title.
//
// Chromium-family apps do not expose their windows through System Events' accessibility
// window list — `name of front window` fails outright with "Can't get window 1 of process
// ... Invalid index. (-1719)". Measured 2026-07-25: Cursor and Obsidian return titles fine,
// Google Chrome errors. That matters more than it sounds: for a browser the title is the
// *only* thing distinguishing work from distraction, and `kDistractingTitleKeywords`
// ("youtube", "reddit", "twitter", …) matches against the title. With it empty, an
// afternoon of video reads exactly like an afternoon of documentation.
//
// Chromium apps share Chrome's scripting terminology, so one `using terms from` block
// serves all of them. It runs as a *separate* osascript invocation on purpose: `using terms
// from` resolves at compile time, so folding it into the main script would break the whole
// query on a machine without Chrome installed.
std::optional<std::string> browser_tab_title(const std::string& app_name) {
    static constexpr std::array<const char*, 5> kChromium = {
        "Google Chrome", "Brave Browser", "Microsoft Edge", "Chromium", "Vivaldi"};

    // Use the matched literal rather than the caller's string: app_name comes from the OS
    // and is interpolated into a shell command, so it must never be attacker-controlled.
    for (const char* candidate : kChromium) {
        if (app_name != candidate) continue;
        return run_command(std::string("osascript ") +
                           "-e 'using terms from application \"Google Chrome\"' " +
                           "-e 'tell application \"" + candidate +
                           "\" to get title of active tab of front window' " +
                           "-e 'end using terms from' 2>/dev/null");
    }
    if (app_name == "Safari") {
        return run_command(
            "osascript -e 'tell application \"Safari\" to get name of current tab of front "
            "window' 2>/dev/null");
    }
    return std::nullopt;
}
#endif

}  // namespace

std::optional<ActiveWindow> query_active_window() {
#if defined(_WIN32)
    return query_active_window_for_hwnd(GetForegroundWindow());
#elif defined(__APPLE__)
    // The title lookup is wrapped in `try` so that losing it does not cost us the app name
    // too. Before this, both statements were unguarded: `name of front window` fails for
    // Chromium browsers, which made the whole script error, produced no tab in the output,
    // and returned nullopt — so the engine learned *nothing* about the foreground app at the
    // exact moment it most needed to (a browser). Every app-category flag read zero.
    auto output = run_command(
        "osascript "
        "-e 'set appName to \"\"' "
        "-e 'set winTitle to \"\"' "
        "-e 'tell application \"System Events\" to set appName to name of first application "
        "process whose frontmost is true' "
        "-e 'try' "
        "-e 'tell application \"System Events\" to tell process appName to set winTitle to "
        "name of front window' "
        "-e 'end try' "
        "-e 'return appName & tab & winTitle' 2>/dev/null");
    if (!output) return std::nullopt;
    const auto tab = output->find('\t');
    if (tab == std::string::npos) return std::nullopt;
    ActiveWindow win;
    win.app_name = output->substr(0, tab);
    win.window_title = output->substr(tab + 1);
    if (win.app_name.empty()) return std::nullopt;
    if (win.window_title.empty()) {
        if (auto title = browser_tab_title(win.app_name)) win.window_title = *title;
    }
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

#if defined(_WIN32)
std::optional<ActiveWindow> query_active_window_for_native_handle(void* native_handle) {
    return query_active_window_for_hwnd(static_cast<HWND>(native_handle));
}
#endif

}  // namespace snapback
