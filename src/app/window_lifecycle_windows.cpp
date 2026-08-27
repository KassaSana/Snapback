#include "app/window_lifecycle.hpp"

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

#include <functional>
#include <utility>

namespace snapback {
namespace {

constexpr wchar_t kPrevWndProcProp[] = L"SnapbackPrevWndProc";

// See the header: one main window per process, so the hook is a file-local rather than a
// context pointer threaded through the subclass.
std::function<void()> g_on_hidden;

LRESULT CALLBACK close_to_tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto old_proc = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kPrevWndProcProp));

    if (msg == WM_CLOSE) {
        // Intercept close event: hide the window to tray instead of destroying it.
        ShowWindow(hwnd, SW_HIDE);
        // After the hide, not before: the hook may put a notification on screen, and it should
        // arrive next to a tray icon the user can already see rather than over the window that
        // is about to vanish. We are on the UI thread here, which is what the hook requires.
        if (g_on_hidden) g_on_hidden();
        return 0;
    }

    if (msg == WM_NCDESTROY) {
        RemovePropW(hwnd, kPrevWndProcProp);
        if (old_proc) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(old_proc));
            return CallWindowProcW(old_proc, hwnd, msg, wparam, lparam);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (old_proc) {
        return CallWindowProcW(old_proc, hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void enable_close_to_tray(void* native_window, std::function<void()> on_hidden) {
    g_on_hidden = std::move(on_hidden);
    enable_close_to_tray(native_window);
}

void enable_close_to_tray(void* native_window) {
    if (!native_window) return;
    HWND hwnd = reinterpret_cast<HWND>(native_window);
    if (GetPropW(hwnd, kPrevWndProcProp)) return;  // Already enabled

    auto old_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(close_to_tray_wnd_proc)));
    if (old_proc) {
        SetPropW(hwnd, kPrevWndProcProp, reinterpret_cast<HANDLE>(old_proc));
    }
}

void prepare_app_exit(void* native_window) {
    // Cleared first, and unconditionally: an explicit Quit is not a close-to-tray hide, so
    // nothing should tell the user the app is still running while it is on its way out.
    g_on_hidden = nullptr;
    if (!native_window) return;
    HWND hwnd = reinterpret_cast<HWND>(native_window);
    auto old_proc = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kPrevWndProcProp));
    if (old_proc) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(old_proc));
        RemovePropW(hwnd, kPrevWndProcProp);
    }
}

bool is_close_to_tray_enabled(void* native_window) {
    if (!native_window) return false;
    HWND hwnd = reinterpret_cast<HWND>(native_window);
    return GetPropW(hwnd, kPrevWndProcProp) != nullptr;
}

}  // namespace snapback
#endif
