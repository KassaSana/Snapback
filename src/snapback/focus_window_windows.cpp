#include "snapback/focus_window.hpp"

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
#include <psapi.h>

#include <algorithm>
#include <cctype>
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

std::wstring lower_copy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

struct WindowSearchContext {
    std::wstring target_app;
    std::wstring target_title;
    HWND best_match{nullptr};
    bool exact_title_match{false};
};

std::wstring get_window_text_safe(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return std::wstring();
    std::wstring title(static_cast<std::size_t>(len + 1), L'\0');
    const int written = GetWindowTextW(hwnd, title.data(), len + 1);
    if (written > 0) {
        title.resize(static_cast<std::size_t>(written));
        return title;
    }
    return std::wstring();
}

std::wstring get_process_name_safe(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return std::wstring();

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return std::wstring();

    std::wstring path(MAX_PATH, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
        path.resize(size);
        CloseHandle(process);
        const auto pos = path.find_last_of(L"\\/");
        return pos == std::wstring::npos ? path : path.substr(pos + 1);
    }
    CloseHandle(process);
    return std::wstring();
}

BOOL CALLBACK enum_windows_callback(HWND hwnd, LPARAM lparam) {
    auto* ctx = reinterpret_cast<WindowSearchContext*>(lparam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    // Ignore tool windows or cloaked UWP windows
    const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex_style & WS_EX_TOOLWINDOW) return TRUE;

    const std::wstring title = lower_copy(get_window_text_safe(hwnd));
    const std::wstring proc = lower_copy(get_process_name_safe(hwnd));

    if (!ctx->target_title.empty() && title == ctx->target_title) {
        ctx->best_match = hwnd;
        ctx->exact_title_match = true;
        return FALSE;  // Found exact title match, stop search
    }

    if (!ctx->best_match) {
        if (!ctx->target_title.empty() && title.find(ctx->target_title) != std::wstring::npos) {
            ctx->best_match = hwnd;
        } else if (!ctx->target_app.empty() && (proc == ctx->target_app || proc.find(ctx->target_app) != std::wstring::npos)) {
            ctx->best_match = hwnd;
        }
    }

    return TRUE;
}

}  // namespace

bool focus_window_supported() {
    return true;
}

FocusTargetResult focus_window(const std::string& app_name, const std::string& window_title) {
    const std::string clean_app = trim_copy(app_name);
    const std::string clean_title = trim_copy(window_title);

    if (clean_app.empty() && clean_title.empty()) {
        return FocusTargetResult{false, "No target application or window specified"};
    }

    return detail::focus_window_native(clean_app, clean_title);
}

namespace detail {

FocusTargetResult focus_window_native(const std::string& app_name,
                                      const std::string& window_title) {
    WindowSearchContext ctx;
    ctx.target_app = lower_copy(to_wide(app_name));
    ctx.target_title = lower_copy(to_wide(window_title));

    EnumWindows(enum_windows_callback, reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.best_match) {
        return FocusTargetResult{
            false,
            "Could not find an active window for '" + (app_name.empty() ? window_title : app_name) + "'"};
    }

    HWND target = ctx.best_match;
    if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
    }

    // Windows focus-stealing bypass: attach thread input if foreground belongs to another thread
    HWND fg = GetForegroundWindow();
    const DWORD fg_thread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD current_thread = GetCurrentThreadId();
    const DWORD target_thread = GetWindowThreadProcessId(target, nullptr);

    bool attached = false;
    if (fg_thread != 0 && fg_thread != target_thread) {
        attached = AttachThreadInput(current_thread, fg_thread, TRUE) != FALSE;
    }

    SetForegroundWindow(target);
    SetFocus(target);

    if (attached) {
        AttachThreadInput(current_thread, fg_thread, FALSE);
    }

    return FocusTargetResult{true, "Window activated successfully"};
}

}  // namespace detail
}  // namespace snapback

#endif  // _WIN32
