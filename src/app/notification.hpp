// Notification text, separate from OS delivery, so copy is identical across platforms and
// unit-testable.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "types.hpp"

namespace snapback {

struct NotificationPayload {
    std::string title;
    std::string body;
};

// Native notification APIs treat an empty title or body as an invalid/poorly-formed
// request. Keep that boundary explicit so OS adapters can reject bad payloads before
// crossing their FFI/API boundary.
inline bool notification_payload_is_valid(const NotificationPayload& payload) {
    return !payload.title.empty() && !payload.body.empty();
}

// Fired when the user drifts off-task. Names the app so the nudge is specific, not naggy.
inline NotificationPayload build_distraction_notification(std::string_view app_name) {
    NotificationPayload n;
    n.title = "Drifting off?";
    n.body = app_name.empty()
                 ? "Looks like you've wandered off your goal. Jump back in when ready."
                 : "You're on " + std::string(app_name) +
                       ". Jump back to your goal when ready.";
    return n;
}

// Fired when someone has been working steadily with no session open (ADR-0005). It asks rather
// than auto-starting, which would have to invent a goal.
inline NotificationPayload build_untracked_work_notification(std::uint64_t active_minutes) {
    NotificationPayload n;
    n.title = "Not tracking this";
    n.body = "You've been working for " + std::to_string(active_minutes) +
             " minutes with no session running. Start one to record it.";
    return n;
}

inline NotificationPayload build_hyperfocus_notification(std::uint64_t continuous_minutes) {
    NotificationPayload n;
    n.title = "Time for a break";
    n.body = "You've been locked in for " + std::to_string(continuous_minutes) +
             " minutes straight. Stand up and stretch.";
    return n;
}

// The return-from-distraction card. Reuses payload.summary, the same line the overlay shows.
inline NotificationPayload build_snapback_notification(const SnapbackPayload& payload) {
    NotificationPayload n;
    n.title = "Welcome back";
    n.body = payload.summary.empty()
                 ? "You're back on task. Pick up where you left off."
                 : payload.summary;
    return n;
}

// Shown once, the first time closing the window leaves the app in the tray. Not routed through
// route_alert: it is feedback on a click, not an interruption. Nothing personal in it.
inline NotificationPayload build_close_to_tray_notification() {
    NotificationPayload n;
    n.title = "Still running";
    n.body = "Snapback is in the tray and still recording. Quit from there to stop it.";
    return n;
}

// Generic copy for native notifications, which land in OS history and on lock screens. Fixed
// strings with nothing interpolated (tests assert this). The overlay and in-app card keep the
// detailed copy.
inline NotificationPayload build_generic_snapback_notification() {
    NotificationPayload n;
    n.title = "Snapback";
    n.body = "You're back on task.";
    return n;
}

inline NotificationPayload build_generic_hyperfocus_notification() {
    NotificationPayload n;
    n.title = "Snapback";
    n.body = "Time for a break.";
    return n;
}

inline NotificationPayload build_generic_untracked_work_notification() {
    NotificationPayload n;
    n.title = "Snapback";
    n.body = "You may want to start a session.";
    return n;
}

// The two above, chosen by mode, so a delivery site reads its preference once and never
// branches on it again. A call site that picked the builder itself is a call site that can
// forget to.
inline NotificationPayload build_snapback_notification(const SnapbackPayload& payload,
                                                       AlertPreviewMode preview) {
    return preview == AlertPreviewMode::Generic ? build_generic_snapback_notification()
                                                : build_snapback_notification(payload);
}

inline NotificationPayload build_hyperfocus_notification(std::uint64_t continuous_minutes,
                                                         AlertPreviewMode preview) {
    return preview == AlertPreviewMode::Generic
               ? build_generic_hyperfocus_notification()
               : build_hyperfocus_notification(continuous_minutes);
}

inline NotificationPayload build_untracked_work_notification(std::uint64_t active_minutes,
                                                             AlertPreviewMode preview) {
    return preview == AlertPreviewMode::Generic
               ? build_generic_untracked_work_notification()
               : build_untracked_work_notification(active_minutes);
}

}  // namespace snapback
