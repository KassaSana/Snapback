// The whole "may this interruption reach the user, and how" decision, in one pure function.
//
// Roadmap 2.16. Same shape and the same reason as notification.hpp next door: that header
// separates *what a notification says* from *how the OS shows it*, and this one separates
// *whether it is shown at all* from both. Delivery policy scattered as `if` checks across the
// tick, main.cpp, and the tray is policy nobody can read in one sitting and nobody can test
// without a running app.
//
// Nothing here reads a clock, loads settings, or touches the filesystem. Every input arrives
// as an argument, which is what makes the across-midnight and clock-change cases testable at
// all.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "types.hpp"

namespace snapback {

// The interruptions this policy governs.
//
// UntrackedWork is here even though 2.16 names only the first three. It is a fourth thing that
// interrupts the user (state.cpp emits it as its own event), so exempting it from quiet hours
// would make quiet hours a lie -- but giving it a fourth settings field would be sprawl for a
// nudge nobody has asked to reconfigure. It is therefore subject to snooze and quiet hours
// with a fixed channel set, and gains a preference the day someone wants one.
enum class AlertEvent { Snapback, Hyperfocus, Pomodoro, UntrackedWork };

// How many there are, and how to index a per-event table by one.
//
// Declared here beside the enum rather than wherever a table happens to live, so adding a
// fifth event is one edit instead of a silent out-of-bounds write in a file nobody thought to
// look at.
inline constexpr std::size_t kAlertEventCount = 4;

inline constexpr std::size_t alert_event_index(AlertEvent event) noexcept {
    return static_cast<std::size_t>(event);
}

// Why nothing fired.
//
// Carried rather than inferred, because "no alert appeared" has four causes and the user is
// entitled to know which one. A log line saying "suppressed" without saying why is the reason
// this kind of feature generates bug reports that cannot be answered.
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

// Where a click on the alert goes.
//
// 9.15's activation channel landed on 2026-08-27, which is what makes a click worth handling:
// before it, a handler could act on the user's request and still leave the window behind
// another app, which is worse than no handler at all.
//
// Chosen here rather than at each delivery site, for the same reason the channels are. There
// are four delivery sites across two native surfaces and a webview; a site that picks its own
// destination is a site that can pick the wrong one, and only on one platform.
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
//
// `action` is the field this struct was shaped to receive: the comment that used to sit here
// said the destination was being kept out so 2.16's action-routing half would "add a field
// rather than reinterpret one". This is that field.
struct AlertRoute {
    AlertChannels channels;
    AlertPreviewMode preview{AlertPreviewMode::Detailed};
    AlertSuppression suppressed_by{AlertSuppression::None};
    AlertAction action{AlertAction::None};

    bool visible() const { return channels.any(); }
};

// The destination each event opens, as a fact about the event rather than a preference.
//
// Not configurable, and deliberately: the channel is a question about how much a person wants
// to be interrupted, while this is a question about what the interruption *is*. A snapback
// that opened the Pomodoro timer would not be a preference, it would be a bug someone
// configured.
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

// Whether a local reading falls inside a quiet range.
//
// Half-open [start, end): 22:00-07:00 is quiet at 06:59 and loud at 07:00 exactly, so two
// adjacent ranges cannot both claim the same minute and "ends at 7" means what a person means
// by it.
//
// `start == end` is an empty range, not a full day. The two mistakes are not symmetric: "I set
// both to 09:00 and got no alerts for a week" is a silent outage that is hard to attribute,
// while "I set both to 09:00 and quiet hours did nothing" is visible the first evening. A
// genuine all-day quiet period is already expressible by clearing every channel, which is also
// the more honest way to say it.
inline bool minute_in_quiet_range(int minute, int start, int end) {
    if (start == end) return false;
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end;  // wraps midnight
}

// The delivery policy, as a function of facts.
//
// `local_minute_of_day` is a parameter rather than a reading taken inside, for a reason that is
// mechanical rather than stylistic: forcing the caller to do the conversion is what lets every
// quiet-hours case be tested without setting TZ. TZ is a process global, it does not behave the
// same across the four toolchains CI builds on, and two cases setting it in parallel would
// interfere with each other.
//
// nullopt for that parameter means the platform could not answer, and it **fails open** -- the
// alert is delivered. Failing closed would silence the product on a machine whose locale data
// is broken, and the user would have no way to tell that apart from "nothing happened". A
// missed quiet hour is a small annoyance the user can see; a permanently silent app is not.
//
// Precedence is snooze, then quiet hours, then channels. Snooze first because it is the user's
// most recent explicit act: when a snooze and a quiet hour both apply, "snoozed" is the more
// useful thing to be told.
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

    // A silenced alert loses its destination as well as its channels. An alert that never
    // appeared has nothing to be clicked -- and leaving a live destination on it would mean a
    // toast still sitting in notification history from before a quiet hour began could act
    // when the user finally noticed it.
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

// The route as it crosses to the delivery layer.
//
// Explicit booleans here, unlike the array-of-names an AlertChannels *preference* uses in
// settings.json. The two are different boundaries: a stored preference has to survive an
// unknown channel from a newer build, while this is a computed instruction consumed
// immediately by main.cpp and the frontend listener, both of which want to ask "is this
// channel on" and nothing else. A consumer that had to scan an array to answer that would be
// a consumer that gets it subtly wrong once.
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
