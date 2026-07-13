// Windows overlay: a borderless, always-on-top, non-activating card in the top-right.
// Rust used a second Tauri webview window; here it's a native Win32 window so we don't
// need a second WebView2 message loop. Pumped by main.cpp's webview run loop (same UI
// thread), so no separate loop is required.
#include "snapback/overlay.hpp"

#include <string>

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

constexpr wchar_t kClassName[] = L"SnapbackOverlayWindow";
constexpr UINT_PTR kDismissTimerId = 1;
constexpr UINT kAutoDismissMs = 9000;  // self-dismiss; also click-to-dismiss

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// The card text is owned as a heap wstring pointed to by GWLP_USERDATA, so WM_PAINT can
// render it without reaching back into the Overlay object. Replaced on each show(),
// freed on WM_DESTROY.
LRESULT CALLBACK overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(24, 24, 32));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(235, 235, 245));
            auto* text = reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            RECT pad = rc;
            pad.left += 20;
            pad.top += 18;
            pad.right -= 20;
            pad.bottom -= 18;
            if (text) {
                DrawTextW(dc, text->c_str(), -1, &pad, DT_WORDBREAK | DT_NOPREFIX);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wparam == kDismissTimerId) {
                KillTimer(hwnd, kDismissTimerId);
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
        case WM_LBUTTONUP:  // click to dismiss
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY: {
            delete reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

class WindowsOverlay final : public Overlay {
public:
    ~WindowsOverlay() override {
        if (hwnd_) DestroyWindow(hwnd_);
    }

    void show(const SnapbackPayload& payload) override {
        ensure_window();
        if (!hwnd_) return;

        // Replace the owned text (freed here or on WM_DESTROY).
        delete reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hwnd_, GWLP_USERDATA));
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(new std::wstring(to_wide(overlay_text(payload)))));

        // Position top-right of the usable work area (excludes the taskbar).
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const ScreenPoint pos = top_right_position({work.left, work.top},
                                                   {work.right - work.left, work.bottom - work.top},
                                                   kOverlayWidth, kScreenMargin);
        SetWindowPos(hwnd_, HWND_TOPMOST, pos.x, pos.y, kOverlayWidth, kOverlayHeight,
                     SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);  // present without stealing focus
        InvalidateRect(hwnd_, nullptr, TRUE);
        KillTimer(hwnd_, kDismissTimerId);
        SetTimer(hwnd_, kDismissTimerId, kAutoDismissMs, nullptr);
    }

    void dismiss() override {
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    }

private:
    void ensure_window() {
        if (hwnd_) return;
        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = overlay_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);  // harmless if already registered

        // WS_EX_NOACTIVATE + WS_EX_TOOLWINDOW: never steal focus, never hit the taskbar.
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                kClassName, L"Snapback", WS_POPUP, 0, 0, kOverlayWidth,
                                kOverlayHeight, nullptr, nullptr, inst, nullptr);
    }

    HWND hwnd_ = nullptr;
};

}  // namespace

Overlay& Overlay::instance() {
    static WindowsOverlay overlay;
    return overlay;
}

}  // namespace snapback
