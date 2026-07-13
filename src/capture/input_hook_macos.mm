#if defined(__APPLE__)

#include "capture/input_hook.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
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
    void run(InputCallback on_event) override {
        callback_ = std::move(on_event);
        running_.store(true, std::memory_order_relaxed);

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
            run_polling_fallback();
            return;
        }

        run_loop_source_ = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source_, kCFRunLoopCommonModes);
        CGEventTapEnable(tap_, true);

        while (running_.load(std::memory_order_relaxed)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, true);
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
    }

    void stop() override {
        running_.store(false, std::memory_order_relaxed);
        CFRunLoopStop(CFRunLoopGetCurrent());
    }

private:
    static CGEventRef tap_callback(CGEventTapProxy, CGEventType type, CGEventRef event,
                                   void* user) {
        auto* self = static_cast<MacInputHook*>(user);
        if (!self || !self->callback_ || type == kCGEventTapDisabledByTimeout ||
            type == kCGEventTapDisabledByUserInput) {
            return event;
        }

        try {
            CaptureEvent ev;
            ev.event_type = map_event(type);
            ev.timestamp_secs = now_secs();
            if (auto active = query_active_window()) {
                ev.app_name = active->app_name;
                ev.window_title = active->window_title;
            }
            if (ev.event_type == EventType::MouseMove) {
                ev.mouse_speed = 0;
            }
            self->callback_(ev);
        } catch (...) {
            // Never throw through the event tap callback.
        }
        return event;
    }

    void run_polling_fallback() {
        std::string last_app;
        std::string last_title;
        while (running_.load(std::memory_order_relaxed)) {
            if (auto active = query_active_window()) {
                if (active->app_name != last_app || active->window_title != last_title) {
                    CaptureEvent ev;
                    ev.event_type = active->app_name != last_app ? EventType::WindowFocusChange
                                                                 : EventType::WindowTitleChange;
                    ev.timestamp_secs = now_secs();
                    ev.app_name = active->app_name;
                    ev.window_title = active->window_title;
                    callback_(ev);
                    last_app = active->app_name;
                    last_title = active->window_title;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    InputCallback callback_;
    std::atomic<bool> running_{false};
    CFMachPortRef tap_ = nullptr;
    CFRunLoopSourceRef run_loop_source_ = nullptr;
};

}  // namespace

InputHook& InputHook::instance() {
    static MacInputHook hook;
    return hook;
}

}  // namespace snapback

#endif  // __APPLE__
