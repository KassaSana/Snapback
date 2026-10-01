#if defined(__APPLE__)

#include "capture/input_hook.hpp"
#include "capture/input_context.hpp"

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

std::optional<EventType> map_event(CGEventType type) {
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
        case kCGEventScrollWheel: return EventType::MouseScroll;
        default: return std::nullopt;
    }
}

class MacInputHook final : public InputHook {
public:
    void run(InputCallback on_event,
             const std::atomic<bool>& stop_requested) override {
        callback_ = std::move(on_event);
        motion_.reset();
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
                           CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventScrollWheel);

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
        // Stop the hook thread's run loop, not the caller's.
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

        // macOS disables the tap when sending these; re-enable or capture silently dies.
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            if (self->tap_) CGEventTapEnable(self->tap_, true);
            return event;
        }
        if (!self->callback_) return event;

        try {
            CaptureEvent ev;
            const auto mapped = map_event(type);
            if (!mapped) return event;
            ev.event_type = *mapped;
            ev.timestamp_secs = now_secs();
            ev.wall_clock_secs = wall_clock_secs_now();
            // Read the cache; never query here (osascript per event would overrun the tap
            // deadline).
            ev.captured_context = self->cached_context_;
            if (ev.event_type == EventType::MouseMove) {
                const auto point = CGEventGetLocation(event);
                self->motion_.observe(point.x, point.y, ev.timestamp_secs, ev);
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

        // Emit window changes as events, not just into the cache: context-switch and
        // title-churn features, and snapback recovery, depend on them. `force` is the startup
        // seed, not a change.
        if ((app_changed || title_changed) && callback_) {
            CaptureEvent ev;
            ev.event_type =
                app_changed ? EventType::WindowFocusChange : EventType::WindowTitleChange;
            ev.timestamp_secs = now;
            ev.wall_clock_secs = wall_clock_secs_now();
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
                    ev.wall_clock_secs = wall_clock_secs_now();
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
    detail::PointerMotion motion_;
};

}  // namespace

InputHook& InputHook::instance() {
    static MacInputHook hook;
    return hook;
}

}  // namespace snapback

#endif  // __APPLE__
