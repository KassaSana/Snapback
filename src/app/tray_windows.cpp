// Windows system tray: Shell_NotifyIcon + a hidden message-only window that receives the
// tray callback and the popup-menu WM_COMMANDs. Pumped by main.cpp's webview run loop
// (same UI thread), so no separate message loop is needed.
#include "app/tray.hpp"

#include <utility>

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
#include <shellapi.h>

namespace snapback {
namespace {

constexpr UINT kTrayCallbackMsg = WM_APP + 1;
constexpr wchar_t kTrayWndClass[] = L"SnapbackTrayWindow";

class WindowsTray final : public Tray {
public:
    ~WindowsTray() override {
        if (installed_) Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (hwnd_) DestroyWindow(hwnd_);
    }

    void install(std::function<void()> on_show, std::function<void()> on_quit) override {
        on_show_ = std::move(on_show);
        on_quit_ = std::move(on_quit);

        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = inst;
        wc.lpszClassName = kTrayWndClass;
        RegisterClassExW(&wc);  // harmless if already registered

        // Message-only window (HWND_MESSAGE): receives tray + menu messages, no UI.
        hwnd_ = CreateWindowExW(0, kTrayWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                inst, nullptr);
        if (!hwnd_) return;
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        nid_ = {};
        nid_.cbSize = sizeof(nid_);
        nid_.hWnd = hwnd_;
        nid_.uID = 1;
        nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid_.uCallbackMessage = kTrayCallbackMsg;
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(nid_.szTip, L"Snapback");
        installed_ = Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
    }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<WindowsTray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && msg == kTrayCallbackMsg) {
            const UINT event = LOWORD(lparam);
            if (event == WM_LBUTTONDBLCLK) {
                self->fire(TrayAction::Show);
            } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                self->show_menu();
            }
            return 0;
        }
        if (self && msg == WM_COMMAND) {
            self->fire(tray_action_for(LOWORD(wparam)));
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void fire(TrayAction action) {
        if (action == TrayAction::Show && on_show_) on_show_();
        else if (action == TrayAction::Quit && on_quit_) on_quit_();
    }

    void show_menu() {
        POINT pt{};
        GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kTrayCmdShow, L"Show Snapback");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayCmdQuit, L"Quit");
        // Required so the menu dismisses correctly when the user clicks elsewhere.
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    std::function<void()> on_show_;
    std::function<void()> on_quit_;
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    bool installed_ = false;
};

}  // namespace

Tray& Tray::instance() {
    static WindowsTray tray;
    return tray;
}

}  // namespace snapback
