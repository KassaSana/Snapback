// Notification payload builders. Roadmap 1.4 (first slice: the text, not the OS toast).
//
// Separates *what a notification says* from *how the OS shows it*. These pure builders
// produce a title/body from app state; the Win32 toast call (and macOS/Linux variants)
// consumes a NotificationPayload later. Keeping the copy here means it's unit-testable and
// identical across platforms — the per-OS layer only handles delivery.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace snapback {

struct NotificationPayload {
    std::string title;
    std::string body;
};

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

// Fired when a session runs past the mode's hyperfocus window without a break.
inline NotificationPayload build_hyperfocus_notification(std::uint64_t continuous_minutes) {
    NotificationPayload n;
    n.title = "Time for a break";
    n.body = "You've been locked in for " + std::to_string(continuous_minutes) +
             " minutes straight. Stand up and stretch.";
    return n;
}

}  // namespace snapback
