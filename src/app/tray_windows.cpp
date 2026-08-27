// Windows system tray: Shell_NotifyIcon + a hidden message-only window that receives the
// tray callback and the popup-menu WM_COMMANDs. Pumped by main.cpp's webview run loop
// (same UI thread), so no separate message loop is needed.
#include "app/tray.hpp"

#include <string>
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

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) return {};
    const int input_length = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           input_length, nullptr, 0);
    if (length <= 0) return {};

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_length, result.data(),
                        length);
    return result;
}

template <std::size_t N>
void copy_notification_text(wchar_t (&destination)[N], std::string_view source) {
    const auto wide = utf8_to_wide(source);
    wcsncpy_s(destination, N, wide.c_str(), _TRUNCATE);
}

class WindowsTray final : public Tray {
public:
    ~WindowsTray() override {
        if (installed_) Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (hwnd_) DestroyWindow(hwnd_);
    }

    bool install(TrayCallbacks callbacks) override {
        callbacks_ = std::move(callbacks);

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
        if (!hwnd_) return false;
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
        if (installed_) {
            // Roadmap 2.16. Required for the balloon to report clicks at all: NIN_* callbacks
            // (NIN_BALLOONUSERCLICK among them) are only delivered to an icon that has
            // declared version 3 or later. Without this the wnd_proc arm below is unreachable
            // and the notification silently is not clickable -- which looks exactly like a
            // click handler that does not work.
            //
            // Version 4 also moves the cursor position from lParam into wParam. That costs
            // nothing here: the event id is read from LOWORD(lParam) either way, and the popup
            // menu asks GetCursorPos for the position rather than taking it off the message.
            nid_.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &nid_);
        }
        return installed_;
    }

    bool show_notification(const NotificationPayload& payload, AlertEvent event,
                           std::int64_t alert_id) override {
        if (!installed_ || !hwnd_ || !notification_payload_is_valid(payload)) return false;

        // Written down before the balloon is raised, because the click that comes back carries
        // nothing. Overwritten by each notification: only the newest balloon is on screen, and
        // Windows collapses a replacement rather than stacking it.
        //
        // Recorded even when Shell_NotifyIcon goes on to fail. A balloon that was refused
        // cannot be clicked, so the stale pair is unreachable -- and clearing it on failure
        // would be one more branch guarding something that cannot happen.
        last_notification_event_ = event;
        last_notification_alert_id_ = alert_id;

        NOTIFYICONDATAW notification = nid_;
        notification.uFlags |= NIF_INFO;
        copy_notification_text(notification.szInfoTitle, payload.title);
        copy_notification_text(notification.szInfo, payload.body);
        notification.dwInfoFlags = NIIF_INFO;
        return Shell_NotifyIconW(NIM_MODIFY, &notification) == TRUE;
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
            } else if (event == NIN_BALLOONUSERCLICK) {
                // Roadmap 2.16. The user clicked the balloon body. Deliberately not
                // NIN_BALLOONTIMEOUT, which is the same notification being *dismissed* -- by
                // the timer or by the user's X -- and acting on that would open a window for
                // somebody who just closed one.
                self->notification_clicked();
            }
            return 0;
        }
        if (self && msg == WM_COMMAND) {
            self->fire(tray_action_for(LOWORD(wparam)));
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void notification_clicked() {
        if (!callbacks_.on_notification_click) return;
        callbacks_.on_notification_click(last_notification_event_, last_notification_alert_id_);
    }

    void fire(TrayAction action) {
        if (action == TrayAction::Show && callbacks_.on_show) callbacks_.on_show();
        else if (action == TrayAction::Quit && callbacks_.on_quit) callbacks_.on_quit();
        else if (action == TrayAction::PauseRecording && callbacks_.on_pause_recording)
            callbacks_.on_pause_recording();
        else if (action == TrayAction::ResumeRecording && callbacks_.on_resume_recording)
            callbacks_.on_resume_recording();
        else if (action == TrayAction::SnoozeAlerts && callbacks_.on_snooze_alerts)
            callbacks_.on_snooze_alerts();
        else if (action == TrayAction::ResumeAlerts && callbacks_.on_resume_alerts)
            callbacks_.on_resume_alerts();
    }

    void show_menu() {
        POINT pt{};
        GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        // Built from the shared model rather than a literal list, so a menu item added
        // here cannot silently go missing from the macOS menu (tray_macos.mm) or vice
        // versa. The labels are ASCII today; utf8_to_wide keeps that from being a
        // constraint.
        const auto status =
            callbacks_.recording_status ? callbacks_.recording_status() : RecordingStatus{};
        for (const TrayMenuEntry& entry : tray_menu_entries(status)) {
            if (tray_menu_entry_is_separator(entry)) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                continue;
            }
            AppendMenuW(menu, MF_STRING, entry.command_id, utf8_to_wide(entry.label).c_str());
        }
        // Required so the menu dismisses correctly when the user clicks elsewhere.
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    TrayCallbacks callbacks_;
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    bool installed_ = false;
    // Roadmap 2.16. What the most recent balloon was about. The id is 0 until an actionable
    // alert raises one, so a click on the close-to-tray explanation claims nothing.
    AlertEvent last_notification_event_ = AlertEvent::Snapback;
    std::int64_t last_notification_alert_id_ = 0;
};

}  // namespace

Tray& Tray::instance() {
    static WindowsTray tray;
    return tray;
}

}  // namespace snapback
