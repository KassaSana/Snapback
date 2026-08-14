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

namespace snapback {
namespace {

constexpr wchar_t kPrevWndProcProp[] = L"SnapbackPrevWndProc";

LRESULT CALLBACK close_to_tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto old_proc = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kPrevWndProcProp));

    if (msg == WM_CLOSE) {
        // Intercept close event: hide the window to tray instead of destroying it.
        ShowWindow(hwnd, SW_HIDE);
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
