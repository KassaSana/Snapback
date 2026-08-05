// Windows global input capture via low-level hooks.
// Windows low-level hook backend.
#if defined(_WIN32)

#include "capture/input_hook.hpp"

#include "capture/active_window.hpp"
#include "capture/input_context.hpp"

#include <atomic>
#include <cmath>
#include <memory>
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

// Immutable foreground context is refreshed by the message loop's timer, outside the
// low-level callbacks. The callbacks only copy this shared pointer and move the event into
// the queue: no process lookup, UTF conversion, string copy, or allocation can block the
// system-wide Windows input path.
std::shared_ptr<const CaptureContext> g_cached_context;
HWND g_cached_hwnd = nullptr;

void refresh_context(bool emit_change) {
    const HWND foreground = GetForegroundWindow();
    CaptureContext next;
    if (auto active = query_active_window_for_native_handle(foreground)) {
        next.app_name = std::move(active->app_name);
        next.window_title = std::move(active->window_title);
    }

    const bool had_context = static_cast<bool>(g_cached_context);
    const bool app_changed = had_context && next.app_name != g_cached_context->app_name;
    const bool title_changed =
        had_context && next.window_title != g_cached_context->window_title;
    if (had_context && !app_changed && !title_changed) {
        g_cached_hwnd = foreground;
        return;
    }

    g_cached_context = std::make_shared<CaptureContext>(std::move(next));
    g_cached_hwnd = foreground;
    if (emit_change && had_context && g_on_event) {
        CaptureEvent event;
        event.event_type =
            app_changed ? EventType::WindowFocusChange : EventType::WindowTitleChange;
        event.timestamp_secs = now_secs();
        event.wall_clock_secs = wall_clock_secs_now();
        event.captured_context = g_cached_context;
        g_on_event(std::move(event));
    }
}

LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_on_event) {
        if (!detail::context_matches_foreground(GetForegroundWindow(), g_cached_hwnd,
                                                static_cast<bool>(g_cached_context))) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        CaptureEvent ev;
        ev.timestamp_secs = now_secs();
        ev.wall_clock_secs = wall_clock_secs_now();
        ev.event_type = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)
                            ? EventType::KeyPress
                            : EventType::KeyRelease;
        ev.captured_context = g_cached_context;
        g_on_event(std::move(ev));
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_on_event) {
        if (!detail::context_matches_foreground(GetForegroundWindow(), g_cached_hwnd,
                                                static_cast<bool>(g_cached_context))) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        CaptureEvent ev;
        ev.timestamp_secs = now_secs();
        ev.wall_clock_secs = wall_clock_secs_now();
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
        ev.captured_context = g_cached_context;
        g_on_event(std::move(ev));
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
        g_cached_context.reset();
        g_cached_hwnd = nullptr;
        g_have_last_mouse = false;
        refresh_context(false);
        HHOOK keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, nullptr, 0);
        HHOOK mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, nullptr, 0);
        if (!keyboard_hook || !mouse_hook) {
            if (keyboard_hook) UnhookWindowsHookEx(keyboard_hook);
            if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
            g_on_event = {};
            g_cached_context.reset();
            g_cached_hwnd = nullptr;
            g_hook_thread_id.store(0, std::memory_order_release);
            return;
        }

        constexpr UINT kContextRefreshMs = 500;
        const UINT_PTR context_timer = SetTimer(nullptr, 0, kContextRefreshMs, nullptr);
        // A low-level hook only fires while this thread pumps messages.
        while (!stop_requested.load(std::memory_order_acquire) &&
               GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (context_timer != 0 && msg.message == WM_TIMER &&
                msg.wParam == context_timer) {
                refresh_context(true);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (context_timer != 0) KillTimer(nullptr, context_timer);
        UnhookWindowsHookEx(keyboard_hook);
        UnhookWindowsHookEx(mouse_hook);
        g_on_event = {};
        g_cached_context.reset();
        g_cached_hwnd = nullptr;
        g_hook_thread_id.store(0, std::memory_order_release);
    }

    void stop() noexcept override {
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
