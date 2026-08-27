// Windows overlay: a borderless, always-on-top, non-activating card in the top-right.
// The overlay is a native Win32 window, pumped by main.cpp's webview run loop on the
// same UI thread, so no separate loop is required.
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
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

namespace snapback {
namespace {

constexpr wchar_t kClassName[] = L"SnapbackOverlayWindow";
constexpr UINT_PTR kDismissTimerId = 1;
constexpr UINT kAutoDismissMs = 9000;  // self-dismiss; also click-to-dismiss

// Roadmap 10.12. Per-monitor DPI, resolved at runtime.
//
// `GetDpiForMonitor` lives in Shcore.dll (Windows 8.1+) and `GetDpiForWindow` in User32
// (Windows 10 1607+). Both are loaded dynamically rather than link-time so the binary still
// starts on an older Windows, where the fallback is the system DPI — which is exactly what the
// overlay used to assume everywhere.
UINT monitor_dpi(HMONITOR monitor) {
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorFn get_dpi_for_monitor = [] {
        HMODULE shcore = LoadLibraryW(L"Shcore.dll");
        return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                            reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor")))
                      : nullptr;
    }();

    if (get_dpi_for_monitor && monitor) {
        UINT dpi_x = 0;
        UINT dpi_y = 0;
        // 0 == MDT_EFFECTIVE_DPI: the scale the user actually chose, not the panel's raw one.
        if (SUCCEEDED(get_dpi_for_monitor(monitor, 0, &dpi_x, &dpi_y)) && dpi_x > 0) {
            return dpi_x;
        }
    }

    // System DPI. Correct on a single-monitor machine and on any pre-8.1 Windows, and no worse
    // than what this code did before per-monitor DPI existed here.
    HDC screen = GetDC(nullptr);
    const UINT dpi = screen ? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX))
                            : static_cast<UINT>(kOverlayBaseDpi);
    if (screen) ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi : static_cast<UINT>(kOverlayBaseDpi);
}

// The monitor the card belongs on, following choose_overlay_monitor's policy.
//
// MONITOR_DEFAULTTONULL, deliberately: the point is to learn whether there *is* a valid
// foreground monitor, and MONITOR_DEFAULTTONEAREST would answer "yes" by silently substituting
// the primary one — collapsing the fallback chain into its last step.
HMONITOR target_monitor() {
    HWND foreground = GetForegroundWindow();
    HMONITOR from_window =
        foreground ? MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) : nullptr;

    POINT cursor{};
    HMONITOR from_cursor =
        GetCursorPos(&cursor) ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL) : nullptr;

    switch (choose_overlay_monitor(from_window != nullptr, from_cursor != nullptr)) {
        case OverlayMonitorSource::kForegroundWindow:
            return from_window;
        case OverlayMonitorSource::kCursor:
            return from_cursor;
        case OverlayMonitorSource::kPrimary:
            break;
    }
    // The primary monitor, reached by asking which one contains the origin.
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

// Where the card goes, in physical pixels on the target monitor.
OverlayRect placement() {
    HMONITOR monitor = target_monitor();

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        // rcWork, not rcMonitor: it already excludes the taskbar, on whichever edge it is
        // docked. This is also the query that replaced SPI_GETWORKAREA, which always answered
        // for the primary display no matter which monitor the user was looking at.
        const RECT& work = info.rcWork;
        return overlay_rect({work.left, work.top},
                            {work.right - work.left, work.bottom - work.top},
                            static_cast<int>(monitor_dpi(monitor)));
    }

    // The monitor vanished between the snapback firing and this call — an unplugged dock is
    // the ordinary way that happens. Fall back to the primary work area rather than placing
    // the card on a display that no longer exists.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return overlay_rect({work.left, work.top},
                        {work.right - work.left, work.bottom - work.top},
                        static_cast<int>(monitor_dpi(nullptr)));
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Roadmap 2.16. Defined below WindowsOverlay, declared here because overlay_proc runs first.
void overlay_action_clicked();

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
            // Roadmap 10.12. The inner padding scales too. Fixed pixels here would leave the
            // text crowded into the corner of a card that doubled in size at 200%, which is the
            // same "fixed pixels ignore per-monitor DPI" complaint one level down.
            const int dpi = static_cast<int>(monitor_dpi(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)));
            const ScreenPoint card{rc.right - rc.left, rc.bottom - rc.top};
            const OverlayRect action = overlay_action_rect(card, dpi);

            RECT pad = rc;
            pad.left += scale_for_dpi(20, dpi);
            pad.top += scale_for_dpi(18, dpi);
            pad.right -= scale_for_dpi(20, dpi);
            // Roadmap 2.16. The text stops above the button rather than beside it. A long
            // window title wrapping under "Take me back" would put unreadable text behind a
            // control the user is being invited to click.
            pad.bottom = action.y - scale_for_dpi(8, dpi);
            if (text) {
                DrawTextW(dc, text->c_str(), -1, &pad, DT_WORDBREAK | DT_NOPREFIX);
            }

            // The button. Drawn from the same rect the hit test uses, so the thing the user
            // aims at and the thing that responds cannot drift apart.
            RECT button{action.x, action.y, action.x + action.width, action.y + action.height};
            HBRUSH fill = CreateSolidBrush(RGB(58, 58, 78));
            FillRect(dc, &button, fill);
            DeleteObject(fill);
            SetTextColor(dc, RGB(245, 245, 255));
            const auto label = to_wide(overlay_action_label());
            DrawTextW(dc, label.c_str(), -1, &button,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wparam == kDismissTimerId) {
                KillTimer(hwnd, kDismissTimerId);
                Overlay::instance().dismiss();
            }
            return 0;
        case WM_LBUTTONUP: {
            // Roadmap 2.16. "Take me back" inside its region; dismiss anywhere else, which is
            // what a click on this card has always meant and what people already expect.
            RECT client{};
            GetClientRect(hwnd, &client);
            const ScreenPoint size{client.right - client.left, client.bottom - client.top};
            const ScreenPoint click{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const int dpi =
                static_cast<int>(monitor_dpi(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)));
            if (overlay_action_hit(size, dpi, click)) {
                overlay_action_clicked();
            } else {
                Overlay::instance().dismiss();
            }
            return 0;
        }
        // Roadmap 10.12. The card is visible and has just crossed onto a display with a
        // different scale — dragged, or the user changed the setting underneath it. Windows
        // hands over the rectangle it wants in the *new* DPI; ignoring it is what leaves a card
        // at half or double size until it is dismissed and shown again.
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            if (suggested) {
                SetWindowPos(hwnd, HWND_TOPMOST, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             // Still never takes focus, mid-move or not.
                             SWP_NOACTIVATE);
                // The padding in WM_PAINT is DPI-derived, so the card has to redraw for the
                // new scale rather than stretching what it drew at the old one.
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
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

        // Roadmap 10.12. Top-right of the *target* monitor's work area, sized for that
        // monitor's DPI. SWP_NOACTIVATE stays: better placement must not start stealing the
        // user's keyboard target, which is the one thing this window has always got right.
        const OverlayRect rect = placement();
        SetWindowPos(hwnd_, HWND_TOPMOST, rect.x, rect.y, rect.width, rect.height,
                     SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);  // present without stealing focus
        InvalidateRect(hwnd_, nullptr, TRUE);
        KillTimer(hwnd_, kDismissTimerId);
        SetTimer(hwnd_, kDismissTimerId, kAutoDismissMs, nullptr);
    }

    void dismiss() override {
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
        if (on_dismiss_) on_dismiss_();
    }

    void set_dismiss_callback(std::function<void()> on_dismiss) override {
        on_dismiss_ = std::move(on_dismiss);
    }

    void set_action_callback(std::function<void()> on_action) override {
        on_action_ = std::move(on_action);
    }

    // Roadmap 2.16. The card's "Take me back" region was clicked. Hides first, then acts:
    // restore_snapback_target raises another application's window, and leaving a TOPMOST card
    // floating over the window the user just asked to return to is not returning them to it.
    void action_clicked() {
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
        // The dismiss callback still runs. It is what unlatches ContextTracker's Recovering
        // state, and acting on a card is just as much "done with this card" as dismissing it
        // -- skipping it here is how the first click would silently disable every later
        // snapback, which is the defect 2.16's delivery half already had to fix once.
        if (on_dismiss_) on_dismiss_();
        if (on_action_) on_action_();
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
    std::function<void()> on_dismiss_;
    std::function<void()> on_action_;
};

void overlay_action_clicked() {
    static_cast<WindowsOverlay&>(Overlay::instance()).action_clicked();
}

}  // namespace

Overlay& Overlay::instance() {
    static WindowsOverlay overlay;
    return overlay;
}

}  // namespace snapback
