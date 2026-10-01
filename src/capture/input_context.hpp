#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <functional>
#include "capture/active_window.hpp"

#include "types.hpp"

namespace snapback::detail {

// Input captured while the foreground window differs from the context snapshot must be
// dropped. Guessing with stale context can bypass per-app privacy exclusions.
inline bool context_matches_foreground(const void* foreground, const void* captured_window,
                                       bool has_context) {
    return has_context && foreground != nullptr && foreground == captured_window;
}

// Mouse translation to CaptureEvent's uint32 speed. Windows timestamps are ms-resolution, so
// the interval is bounded away from zero; coordinates are widened before subtracting and the
// result validated before narrowing.
inline std::uint32_t mouse_speed_for_points(std::int32_t current_x, std::int32_t current_y,
                                             std::int32_t previous_x, std::int32_t previous_y,
                                             double elapsed_secs) {
    constexpr double kMinimumElapsedSecs = 1e-6;
    constexpr double kMaxSpeed =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());

    const double safe_elapsed_secs =
        std::isfinite(elapsed_secs) && elapsed_secs > kMinimumElapsedSecs
            ? elapsed_secs
            : kMinimumElapsedSecs;
    const double dx = static_cast<double>(current_x) - static_cast<double>(previous_x);
    const double dy = static_cast<double>(current_y) - static_cast<double>(previous_y);
    const double speed = std::hypot(dx, dy) / safe_elapsed_secs;
    if (!std::isfinite(speed) || speed >= kMaxSpeed) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(speed);
}

#if defined(_WIN32)
// Button presses count once; releases are ignored. Scroll has its own activity event.
std::optional<EventType> classify_mouse_message(unsigned message);
#endif

// The evdev numbers are protocol constants, allowing fixtures on every host.
inline std::optional<EventType> classify_linux_input(unsigned type, unsigned code, int value) {
    if (type == 1) {  // EV_KEY
        if (code >= 0x110 && code <= 0x117)
            return value == 1 ? std::optional{EventType::MouseClick} : std::nullopt;
        if (code == 0 || code >= 0x100) return std::nullopt;
        if (value == 0) return EventType::KeyRelease;
        if (value == 1 || value == 2) return EventType::KeyPress;
    }
    if (type == 2 && (code == 6 || code == 8 || code == 11 || code == 12))
        return value != 0 ? std::optional{EventType::MouseScroll} : std::nullopt;
    if ((type == 2 || type == 3) && (code == 0 || code == 1)) return EventType::MouseMove;
    return std::nullopt;
}

class PointerMotion {
public:
    void observe(double x, double y, double now_secs, CaptureEvent& event) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(now_secs)) {
            previous_.reset();
            return;
        }
        const auto coordinate = [](double value) {
            return static_cast<std::int32_t>(std::clamp(value,
                static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                static_cast<double>(std::numeric_limits<std::int32_t>::max())));
        };
        event.mouse_x = coordinate(x);
        event.mouse_y = coordinate(y);
        if (previous_) event.mouse_speed = mouse_speed_for_points(event.mouse_x, event.mouse_y,
            previous_->x, previous_->y, now_secs - previous_->at_secs);
        previous_ = Point{event.mouse_x, event.mouse_y, now_secs};
    }
    void reset() { previous_.reset(); }
private:
    struct Point { std::int32_t x, y; double at_secs; };
    std::optional<Point> previous_;
};

// Hook-thread owned. Event translation only reads value(); probes happen on the loop cadence.
class CaptureContextProvider {
public:
    using Probe = std::function<std::optional<ActiveWindow>()>;
    explicit CaptureContextProvider(Probe probe) : probe_(std::move(probe)) {}
    std::optional<CaptureEvent> refresh(double now_secs, double wall_secs) {
        if (refreshed_at_ && now_secs - *refreshed_at_ < 0.5) return std::nullopt;
        refreshed_at_ = now_secs;
        const auto window = probe_();
        CaptureContext next;
        if (window) { next.app_name = window->app_name; next.window_title = window->window_title; }
        const bool initial = !context_;
        const bool app_changed = context_ && next.app_name != context_->app_name;
        const bool title_changed = context_ && next.window_title != context_->window_title;
        if (!initial && !app_changed && !title_changed) return std::nullopt;
        context_ = std::make_shared<const CaptureContext>(std::move(next));
        if (initial) return std::nullopt;
        CaptureEvent event;
        event.event_type = app_changed ? EventType::WindowFocusChange : EventType::WindowTitleChange;
        event.timestamp_secs = now_secs;
        event.wall_clock_secs = wall_secs;
        event.captured_context = context_;
        return event;
    }
    const std::shared_ptr<const CaptureContext>& value() const { return context_; }
private:
    Probe probe_;
    std::optional<double> refreshed_at_;
    std::shared_ptr<const CaptureContext> context_;
};

}  // namespace snapback::detail
