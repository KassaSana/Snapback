#if defined(__APPLE__)

#include "capture/input_hook.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "capture/active_window.hpp"

namespace snapback {
namespace {

double now_secs() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

EventType map_event(CGEventType type) {
    switch (type) {
        case kCGEventKeyDown: return EventType::KeyPress;
        case kCGEventKeyUp: return EventType::KeyRelease;
        case kCGEventLeftMouseDown:
        case kCGEventRightMouseDown:
        case kCGEventOtherMouseDown:
            return EventType::MouseClick;
        case kCGEventMouseMoved:
        case kCGEventLeftMouseDragged:
        case kCGEventRightMouseDragged:
        case kCGEventOtherMouseDragged:
            return EventType::MouseMove;
        default: return EventType::KeyPress;
    }
}

class MacInputHook final : public InputHook {
public:
    void run(InputCallback on_event,
             const std::atomic<bool>& stop_requested) override {
        callback_ = std::move(on_event);
        if (stop_requested.load(std::memory_order_acquire)) return;

        // Publish THIS thread's run loop so stop() — which is called from the caller's
        // thread, not ours — can wake the right one. Retained because stop() may touch it
        // while this thread is still unwinding.
        CFRunLoopRef loop = CFRunLoopGetCurrent();
        CFRetain(loop);
        {
            std::lock_guard lock(run_loop_mutex_);
            run_loop_ = loop;
        }

        CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
                           CGEventMaskBit(kCGEventLeftMouseDown) |
                           CGEventMaskBit(kCGEventRightMouseDown) |
                           CGEventMaskBit(kCGEventOtherMouseDown) |
                           CGEventMaskBit(kCGEventMouseMoved) |
                           CGEventMaskBit(kCGEventLeftMouseDragged) |
                           CGEventMaskBit(kCGEventRightMouseDragged) |
                           CGEventMaskBit(kCGEventOtherMouseDragged);

        tap_ = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionListenOnly, mask, tap_callback, this);
        if (!tap_) {
            // No Accessibility / Input Monitoring permission. Degrade to window polling
            // rather than going silent (check_capture_permissions() reports the cause).
            run_polling_fallback(stop_requested);
            release_run_loop();
            return;
        }

        run_loop_source_ = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source_, kCFRunLoopCommonModes);
        CGEventTapEnable(tap_, true);

        refresh_active_window(true);  // seed the cache so the first event isn't blank
        while (!stop_requested.load(std::memory_order_acquire)) {
            // returnAfterSourceHandled=true, so this returns as soon as one event is
            // handled; the timeout doubles as the cache-refresh cadence when idle.
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, kWindowRefreshSecs, true);
            refresh_active_window(false);
        }

        CGEventTapEnable(tap_, false);
        if (run_loop_source_) {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), run_loop_source_, kCFRunLoopCommonModes);
            CFRelease(run_loop_source_);
            run_loop_source_ = nullptr;
        }
        if (tap_) {
            CFRelease(tap_);
            tap_ = nullptr;
        }
        release_run_loop();
    }

    void stop() noexcept override {
        // Stop the HOOK thread's run loop, not the caller's. This used to be
        // CFRunLoopStop(CFRunLoopGetCurrent()), which on the UI thread targeted the app's
        // own run loop — wrong loop, and under the webview potentially a live one. It only
        // appeared to work because run() polls with a timeout and rechecks running_.
        std::lock_guard lock(run_loop_mutex_);
        if (run_loop_) CFRunLoopStop(run_loop_);
    }

private:
    // How often the cached foreground window is refreshed. Matches the polling fallback's
    // cadence, so window-change granularity is the same on both paths.
    static constexpr double kWindowRefreshSecs = 0.5;

    static CGEventRef tap_callback(CGEventTapProxy, CGEventType type, CGEventRef event,
                                   void* user) {
        auto* self = static_cast<MacInputHook*>(user);
        if (!self) return event;

        // macOS DISABLES the tap when it sends either of these — recognizing the message
        // is not handling it. Without re-enabling, capture dies for the rest of the
        // process lifetime while HealthStatus still reports capture_running == true.
        // ByTimeout fires when a callback overruns the system's deadline; ByUserInput when
        // something disabled the tap out from under us. Both are recoverable.
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            if (self->tap_) CGEventTapEnable(self->tap_, true);
            return event;
        }
        if (!self->callback_) return event;

        try {
            CaptureEvent ev;
            ev.event_type = map_event(type);
            ev.timestamp_secs = now_secs();
            // Read the cache, never query here. This used to call query_active_window()
            // per event, which on macOS shells out to `osascript` (active_window.cpp) —
            // forking a process per keystroke and per mouse-move, inside the tap callback.
            // That blew the tap's deadline, which triggered the disable above. The cache
            // is refreshed by run() on this same thread, so no lock is needed.
            ev.captured_context = self->cached_context_;
            if (ev.event_type == EventType::MouseMove) {
                ev.mouse_speed = 0;
            }
            self->callback_(std::move(ev));
        } catch (...) {
            // Never throw through the event tap callback.
        }
        return event;
    }

    // Called only from the hook thread (run loop side), never from the tap callback, so
    // the cache needs no synchronization: the tap source is attached to this thread's run
    // loop, so its callback is delivered on this thread too.
    void refresh_active_window(bool force) {
        const double now = now_secs();
        if (!force && now - last_window_refresh_secs_ < kWindowRefreshSecs) return;
        last_window_refresh_secs_ = now;
        auto active = query_active_window();
        if (!active) return;

        const bool app_changed =
            !force && cached_context_ && active->app_name != cached_context_->app_name;
        const bool title_changed =
            !force && cached_context_ && active->window_title != cached_context_->window_title;
        if (!force && cached_context_ && !app_changed && !title_changed) return;
        cached_context_ = std::make_shared<CaptureContext>(
            CaptureContext{std::move(active->app_name), std::move(active->window_title)});

        // Emit the change as an event, not just into the cache.
        //
        // This was the whole reason macOS could not tell focus from distraction. The tap
        // path stamped every key/mouse event with the cached app, so features knew *which*
        // app you were in — but `WindowFocusChange`/`WindowTitleChange` were emitted only
        // by run_polling_fallback(), i.e. only when the tap failed to create. With a
        // working tap, the engine never saw a single window change, so
        // `context_switches_30s`, `context_switches_5min`, and `window_title_changed_30s`
        // were **permanently zero**.
        //
        // The consequences, measured over 714 real predictions on 2026-07-25: `thrash`
        // caps at 0.30 without switch counts and so can never reach the 0.75 DISTRACTED
        // threshold; `drift` caps at 0.30 without title churn and so can never reach the
        // 0.55 PSEUDO_PRODUCTIVE threshold. Two of the four focus states were unreachable
        // — every prediction ever made on this machine was PRODUCTIVE or DEEP_FOCUS. It
        // also starved ContextTracker, which only advances on window changes, so snapback
        // recovery could never fire either.
        //
        // `force` is the startup seed: the first observation is not a change.
        if ((app_changed || title_changed) && callback_) {
            CaptureEvent ev;
            ev.event_type =
                app_changed ? EventType::WindowFocusChange : EventType::WindowTitleChange;
            ev.timestamp_secs = now;
            ev.captured_context = cached_context_;
            callback_(std::move(ev));
        }
    }

    void release_run_loop() {
        CFRunLoopRef loop = nullptr;
        {
            std::lock_guard lock(run_loop_mutex_);
            loop = run_loop_;
            run_loop_ = nullptr;
        }
        if (loop) CFRelease(loop);
    }

    void run_polling_fallback(const std::atomic<bool>& stop_requested) {
        std::string last_app;
        std::string last_title;
        while (!stop_requested.load(std::memory_order_acquire)) {
            if (auto active = query_active_window()) {
                if (active->app_name != last_app || active->window_title != last_title) {
                    CaptureEvent ev;
                    ev.event_type = active->app_name != last_app ? EventType::WindowFocusChange
                                                                 : EventType::WindowTitleChange;
                    ev.timestamp_secs = now_secs();
                    ev.app_name = active->app_name;
                    ev.window_title = active->window_title;
                    callback_(std::move(ev));
                    last_app = active->app_name;
                    last_title = active->window_title;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    InputCallback callback_;
    CFMachPortRef tap_ = nullptr;
    CFRunLoopSourceRef run_loop_source_ = nullptr;
    // stop() and the hook thread touch the same retained CF object. The mutex protects the
    // pointee lifetime, not merely the pointer value: release cannot run until a concurrent
    // CFRunLoopStop has returned.
    std::mutex run_loop_mutex_;
    CFRunLoopRef run_loop_ = nullptr;

    // Foreground-window cache. Hook-thread-only (see refresh_active_window).
    std::shared_ptr<const CaptureContext> cached_context_;
    double last_window_refresh_secs_ = 0.0;
};

}  // namespace

InputHook& InputHook::instance() {
    static MacInputHook hook;
    return hook;
}

}  // namespace snapback

#endif  // __APPLE__
