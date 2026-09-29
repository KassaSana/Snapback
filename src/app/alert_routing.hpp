// The whole "may this interruption reach the user, and how" decision, as one pure function. No
// clock, settings load, or filesystem: every input is an argument, so across-midnight and
// clock-change cases are testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "types.hpp"

namespace snapback {

// The interruptions this policy governs. UntrackedWork has a fixed channel set but is still
// subject to snooze and quiet hours.
enum class AlertEvent { Snapback, Hyperfocus, Pomodoro, UntrackedWork };

// Kept beside the enum so adding an event is one edit.
inline constexpr std::size_t kAlertEventCount = 4;

inline constexpr std::size_t alert_event_index(AlertEvent event) noexcept {
    return static_cast<std::size_t>(event);
}

// Why nothing fired, carried so it can be logged and shown.
enum class AlertSuppression {
    None,
    Snoozed,
    QuietHours,
    ChannelsOff,
    // The platform could not say what time it is locally. See `route_alert`: this fails open,
    // so the value means "delivered without checking quiet hours", not "suppressed".
    ConversionFailed,
};

inline const char* alert_suppression_as_str(AlertSuppression s) noexcept {
    switch (s) {
        case AlertSuppression::Snoozed:
            return "snoozed";
        case AlertSuppression::QuietHours:
            return "quiet hours";
        case AlertSuppression::ChannelsOff:
            return "channels off";
        case AlertSuppression::ConversionFailed:
            return "local time unavailable";
        case AlertSuppression::None:
        default:
            return "none";
    }
}

// Where a click on the alert goes, decided here rather than at each delivery site.
enum class AlertAction {
    None,                 // nothing to open -- see the note on a suppressed route below
    ReturnToWork,         // 2.8's "Take me back": raise the window the snapback recorded
    OpenSessionComposer,  // 2.7's nudge: offer to start a session, never start one
    OpenPomodoro,         // the running phase, or the break it just moved into
};

inline const char* alert_action_as_str(AlertAction a) noexcept {
    switch (a) {
        case AlertAction::ReturnToWork:
            return "return to work";
        case AlertAction::OpenSessionComposer:
            return "open session composer";
        case AlertAction::OpenPomodoro:
            return "open pomodoro";
        case AlertAction::None:
        default:
            return "none";
    }
}

// What the delivery layer should do with one interruption.
struct AlertRoute {
    AlertChannels channels;
    AlertPreviewMode preview{AlertPreviewMode::Detailed};
    AlertSuppression suppressed_by{AlertSuppression::None};
    AlertAction action{AlertAction::None};

    bool visible() const { return channels.any(); }
};

// The destination each event opens: a fact about the event, not a preference.
inline AlertAction alert_action_for(AlertEvent event) noexcept {
    switch (event) {
        case AlertEvent::Snapback:
            return AlertAction::ReturnToWork;
        case AlertEvent::UntrackedWork:
            return AlertAction::OpenSessionComposer;
        case AlertEvent::Pomodoro:
        case AlertEvent::Hyperfocus:
            // Both are the break/phase conversation. Hyperfocus says "you have been at this a
            // while" and Pomodoro says "the phase changed"; in each case what the user wants
            // in front of them is the timer, not a different screen.
            return AlertAction::OpenPomodoro;
    }
    return AlertAction::None;
}

// Whether a local reading is inside a quiet range. Half-open [start, end), so 22:00-07:00 is
// loud at 07:00. `start == end` is an empty range, not a full day (clearing every channel is
// how to silence all day).
inline bool minute_in_quiet_range(int minute, int start, int end) {
    if (start == end) return false;
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end;  // wraps midnight
}

// The delivery policy. `local_minute_of_day` is a parameter so quiet hours are testable without
// setting TZ. nullopt (conversion failed) fails open: a missed quiet hour is visible, a
// permanently silent app is not. Precedence: snooze, then quiet hours, then channels.
inline AlertRoute route_alert(AlertEvent event, const AlertDeliverySettings& settings,
                              std::int64_t now_wall_ms, std::optional<int> local_minute_of_day) {
    AlertRoute route;
    route.preview = settings.preview;
    route.action = alert_action_for(event);

    switch (event) {
        case AlertEvent::Snapback:
            route.channels = settings.snapback;
            break;
        case AlertEvent::Hyperfocus:
            route.channels = settings.hyperfocus;
            break;
        case AlertEvent::Pomodoro:
            route.channels = settings.pomodoro;
            break;
        // Fixed, per the note on AlertEvent. In-app matches what this nudge does today.
        case AlertEvent::UntrackedWork:
            route.channels = AlertChannels{true, false, false};
            break;
    }

    // A silenced alert also loses its destination, so a stale toast cannot act later.
    const auto silence = [&route](AlertSuppression why) {
        route.channels = AlertChannels{};
        route.action = AlertAction::None;
        route.suppressed_by = why;
    };

    if (settings.snoozed_until_wall_ms > 0 && now_wall_ms < settings.snoozed_until_wall_ms) {
        silence(AlertSuppression::Snoozed);
        return route;
    }

    if (settings.quiet_hours_enabled) {
        if (!local_minute_of_day) {
            // Delivered, but say so: the alert went out without the quiet-hours check running.
            route.suppressed_by = AlertSuppression::ConversionFailed;
            return route;
        }
        if (minute_in_quiet_range(*local_minute_of_day, settings.quiet_hours_start_min,
                                  settings.quiet_hours_end_min)) {
            silence(AlertSuppression::QuietHours);
            return route;
        }
    }

    if (!route.channels.any()) {
        route.suppressed_by = AlertSuppression::ChannelsOff;
        route.action = AlertAction::None;
    }
    return route;
}

// The route as sent to the delivery layer: explicit booleans (unlike the stored preference's
// array of names, which must tolerate unknown channels from newer builds).
inline void to_json(nlohmann::json& j, const AlertRoute& v) {
    j = nlohmann::json{{"inApp", v.channels.in_app},
                       {"overlay", v.channels.overlay},
                       {"native", v.channels.native},
                       {"preview", v.preview},
                       {"action", alert_action_as_str(v.action)}};
}

inline void from_json(const nlohmann::json& j, AlertRoute& v) {
    v.channels.in_app = j.value("inApp", false);
    v.channels.overlay = j.value("overlay", false);
    v.channels.native = j.value("native", false);
    v.preview = j.value("preview", AlertPreviewMode::Detailed);
    // Absent, or a destination a newer build knows and this one does not, both mean "no
    // destination". Degrading to a click that only raises the window is the safe direction:
    // guessing would send the user somewhere nobody chose.
    const auto action = j.value("action", std::string("none"));
    v.action = AlertAction::None;
    for (const auto candidate : {AlertAction::ReturnToWork, AlertAction::OpenSessionComposer,
                                 AlertAction::OpenPomodoro}) {
        if (action == alert_action_as_str(candidate)) v.action = candidate;
    }
}

}  // namespace snapback
