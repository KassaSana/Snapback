// Windows global input capture via low-level hooks.
// Windows low-level hook backend.
#if defined(_WIN32)

#include "capture/input_hook.hpp"

#include "capture/active_window.hpp"

#include <atomic>
#include <cmath>
#include <string>
#include <windows.h>

namespace snapback {
namespace {

// Low-level hooks are process-global and the callback signature carries no user
// pointer, so the active callback has to live at namespace scope. Single hook
// instance only — matches how capture runs as one engine.
InputCallback g_on_event;
std::atomic<DWORD> g_hook_thread_id{0};
POINT g_last_mouse_pos{};
double g_last_mouse_ts = 0.0;
bool g_have_last_mouse = false;

double now_secs() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

// Cache the active-window lookup by foreground HWND. query_active_window() does an
// OpenProcess + GetModuleBaseNameW + UTF-16->UTF-8 conversion that must NOT run on every
// keystroke/mouse-move inside a low-level hook callback: slow LL-hook callbacks cause
// system-wide input lag and Windows silently drops events past LowLevelHooksTimeout.
// GetForegroundWindow() is cheap, so we only pay the expensive query when the focused
// window actually changes; otherwise we reuse the cached strings. (Hook-thread only, so
// no synchronization is needed on these statics.)
HWND g_cached_hwnd = nullptr;
std::string g_cached_app;
std::string g_cached_title;

void enrich_context(CaptureEvent& ev) {
    const HWND foreground = GetForegroundWindow();
    if (foreground != g_cached_hwnd) {
        g_cached_hwnd = foreground;
        if (auto active = query_active_window()) {
            g_cached_app = active->app_name;
            g_cached_title = active->window_title;
        } else {
            g_cached_app.clear();
            g_cached_title.clear();
        }
    }
    ev.app_name = g_cached_app;
    ev.window_title = g_cached_title;
}

LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_on_event) {
        CaptureEvent ev;
        ev.timestamp_secs = now_secs();
        ev.event_type = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)
                            ? EventType::KeyPress
                            : EventType::KeyRelease;
        enrich_context(ev);
        g_on_event(ev);
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_on_event) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        CaptureEvent ev;
        ev.timestamp_secs = now_secs();
        ev.event_type = (wparam == WM_MOUSEMOVE) ? EventType::MouseMove
                                                 : EventType::MouseClick;
        if (info) {
            ev.mouse_x = info->pt.x;
            ev.mouse_y = info->pt.y;
            if (wparam == WM_MOUSEMOVE && g_have_last_mouse) {
                const double dx = static_cast<double>(info->pt.x - g_last_mouse_pos.x);
                const double dy = static_cast<double>(info->pt.y - g_last_mouse_pos.y);
                const double dt = (ev.timestamp_secs - g_last_mouse_ts) > 1e-6
                                      ? ev.timestamp_secs - g_last_mouse_ts
                                      : 1e-6;
                ev.mouse_speed = static_cast<std::uint32_t>(
                    std::sqrt(dx * dx + dy * dy) / dt);
            }
            if (wparam == WM_MOUSEMOVE) {
                g_last_mouse_pos = info->pt;
                g_last_mouse_ts = ev.timestamp_secs;
                g_have_last_mouse = true;
            }
        }
        enrich_context(ev);
        g_on_event(ev);
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

class WindowsInputHook final : public InputHook {
public:
    void run(InputCallback on_event,
             const std::atomic<bool>& stop_requested) override {
        if (stop_requested.load(std::memory_order_acquire)) return;

        // PostThreadMessage fails until the target thread owns a message queue.
        // Create it before publishing the id used by stop().
        MSG msg{};
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        g_hook_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
        if (stop_requested.load(std::memory_order_acquire)) {
            g_hook_thread_id.store(0, std::memory_order_release);
            return;
        }

        g_on_event = std::move(on_event);
        HHOOK keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, nullptr, 0);
        HHOOK mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, nullptr, 0);
        if (!keyboard_hook || !mouse_hook) {
            if (keyboard_hook) UnhookWindowsHookEx(keyboard_hook);
            if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
            g_on_event = {};
            g_hook_thread_id.store(0, std::memory_order_release);
            return;
        }

        // A low-level hook only fires while this thread pumps messages.
        while (!stop_requested.load(std::memory_order_acquire) &&
               GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        UnhookWindowsHookEx(keyboard_hook);
        UnhookWindowsHookEx(mouse_hook);
        g_on_event = {};
        g_hook_thread_id.store(0, std::memory_order_release);
    }

    void stop() override {
        if (const DWORD thread_id = g_hook_thread_id.load(std::memory_order_acquire);
            thread_id != 0) {
            PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        }
    }
};

}  // namespace

InputHook& InputHook::instance() {
    static WindowsInputHook hook;
    return hook;
}

}  // namespace snapback

#endif  // _WIN32
