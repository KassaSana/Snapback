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

// Fired when a session runs past the mode's hyperfocus window without a break.
// Roadmap 2.7 / ADR-0005. Fired when someone has been working steadily with no session open.
//
// It asks rather than acts. Auto-starting a session would have to invent a goal, and
// `goal_alignment` is a real model input that 2.5 scores and Tier 13 trains on — an invented
// one poisons the corpus. It would also change what gets recorded without being asked, which
// the onboarding promise does not cover. So the app says "you look like you're working" and
// leaves the decision where it belongs.
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

// Fired on the return-from-distraction edge (ContextTracker::build_snapback). This is a
// "welcome back" card, not a drifting-off nudge: by the time SnapbackPayload exists the
// user has already returned to the on-task app, so the copy reuses payload.summary (e.g.
// "Return to auth.ts") — the same line the native overlay already shows — so the toast
// and overlay never disagree.
inline NotificationPayload build_snapback_notification(const SnapbackPayload& payload) {
    NotificationPayload n;
    n.title = "Welcome back";
    n.body = payload.summary.empty()
                 ? "You're back on task. Pick up where you left off."
                 : payload.summary;
    return n;
}

// Roadmap 9.15. Said once, the first time closing the window leaves the app in the tray.
//
// Closing a window is the universal "I am done with this program", and close-to-tray quietly
// makes it mean something else. Left unexplained, the honest reading of what happened is "it
// crashed" -- and the user who believes that has no reason to look in the notification area
// for the thing they think they closed.
//
// It deliberately does **not** go through `route_alert`. Quiet hours and snooze govern
// interruptions *the app initiates*; this is feedback on a control the user just clicked, one
// second earlier, and suppressing it would leave exactly the confused user this exists to
// help. Every other notification in this binary is routed, so the exception is written down
// here rather than left to read as an oversight.
//
// No preview mode either: there is nothing personal in it. It names no app, title, file,
// project, goal, or duration -- the whole content is that Snapback is still running.
inline NotificationPayload build_close_to_tray_notification() {
    NotificationPayload n;
    n.title = "Still running";
    n.body = "Snapback is in the tray and still recording. Quit from there to stop it.";
    return n;
}

// Roadmap 2.16. The same events, said in a way a stranger may read.
//
// A native notification is not only shown once: the OS copies it into a notification history
// and renders it on a lock screen that a colleague, a partner, or a person behind you on a
// train may be looking at. `payload.summary` is "Return to auth.ts" — a filename, sometimes a
// client's project name — and the hyperfocus and untracked copy carry a duration that says how
// long someone has been at their desk. None of that is the app's to broadcast.
//
// These are **fixed strings** with nothing interpolated, which is the property the tests
// assert. A generic builder that formatted anything from the payload would be one refactor
// away from leaking again, and the leak would be invisible until someone saw it on a lock
// screen. The count is left out too: "locked in for 214 minutes" is itself a disclosure.
//
// Only the native channel has this mode. The overlay draws on the user's own unlocked screen
// and the in-app card is inside the app, so both keep the detailed copy above.
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
