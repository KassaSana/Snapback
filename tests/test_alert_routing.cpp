#include "doctest_wrapper.hpp"

#include "app/alert_routing.hpp"

using namespace snapback;

namespace {

// A fixed "now" for the cases that do not care about it. Well before any snooze deadline the
// cases set, so a snooze is only in play where a case puts one there.
constexpr std::int64_t kNow = 1'700'000'000'000;

// 22:00-07:00, the default range, enabled.
AlertDeliverySettings with_quiet_hours() {
    AlertDeliverySettings s;
    s.quiet_hours_enabled = true;
    return s;
}

int at(int hour, int minute = 0) {
    return hour * 60 + minute;
}

}  // namespace

TEST_CASE("route_alert defaults deliver exactly one channel per event") {
    // 2.16's rule: one visible intervention per logical event. A snapback firing both the
    // overlay and a native toast for the same moment is the duplication the item names.
    const AlertDeliverySettings defaults;

    const auto snapback = route_alert(AlertEvent::Snapback, defaults, kNow, at(12));
    CHECK(snapback.channels.overlay == true);
    CHECK(snapback.channels.native == false);
    CHECK(snapback.channels.in_app == false);

    const auto hyperfocus = route_alert(AlertEvent::Hyperfocus, defaults, kNow, at(12));
    CHECK(hyperfocus.channels.native == true);
    CHECK(hyperfocus.channels.overlay == false);

    const auto pomodoro = route_alert(AlertEvent::Pomodoro, defaults, kNow, at(12));
    CHECK(pomodoro.channels.in_app == true);
    CHECK(pomodoro.channels.native == false);
}

TEST_CASE("route_alert honours an explicit both-channels preference") {
    // "Both" has to stay reachable, or the default is a restriction rather than a default.
    AlertDeliverySettings settings;
    settings.snapback = AlertChannels{false, true, true};

    const auto route = route_alert(AlertEvent::Snapback, settings, kNow, at(12));
    CHECK(route.channels.overlay == true);
    CHECK(route.channels.native == true);
    CHECK(route.visible());
    CHECK(route.suppressed_by == AlertSuppression::None);
}

TEST_CASE("quiet hours spanning midnight silence 23:30 and 06:59 but not 07:00") {
    // The case the item calls out by name. A range whose start is greater than its end wraps,
    // and the end is exclusive so "ends at 7" means what a person means by it.
    const auto settings = with_quiet_hours();

    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, at(23, 30)).visible());
    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, at(0, 0)).visible());
    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, at(6, 59)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(7, 0)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(21, 59)).visible());
    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, at(22, 0)).visible());
}

TEST_CASE("a quiet range inside one day silences only that range") {
    // The non-wrapping branch, which the midnight case above cannot exercise.
    auto settings = with_quiet_hours();
    settings.quiet_hours_start_min = at(13);
    settings.quiet_hours_end_min = at(14);

    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(12, 59)).visible());
    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, at(13, 0)).visible());
    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, at(13, 59)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(14, 0)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(2, 0)).visible());
}

TEST_CASE("quiet hours with equal start and end never silence anything") {
    // Empty range, not a full day. A 24-hour quiet period is expressible by clearing the
    // channels, and that spelling is visible in the UI; this one would be a silent outage
    // nobody could attribute.
    auto settings = with_quiet_hours();
    settings.quiet_hours_start_min = at(9);
    settings.quiet_hours_end_min = at(9);

    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(9, 0)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(3, 0)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(21, 0)).visible());
}

TEST_CASE("quiet hours do nothing while the toggle is off") {
    // The start/end values exist whether or not the feature is on; they must not act until it
    // is, or the defaults would silence the first night after an upgrade.
    AlertDeliverySettings settings;  // quiet_hours_enabled defaults false
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(23, 30)).visible());
}

TEST_CASE("route_alert reports why nothing fired") {
    // Four causes, and the user is entitled to know which. A suppression reason that has to be
    // reconstructed from the settings is a reason nobody will reconstruct.
    AlertDeliverySettings quiet = with_quiet_hours();
    CHECK(route_alert(AlertEvent::Snapback, quiet, kNow, at(23, 30)).suppressed_by ==
          AlertSuppression::QuietHours);

    AlertDeliverySettings off;
    off.snapback = AlertChannels{};
    CHECK(route_alert(AlertEvent::Snapback, off, kNow, at(12)).suppressed_by ==
          AlertSuppression::ChannelsOff);

    AlertDeliverySettings snoozed;
    snoozed.snoozed_until_wall_ms = kNow + 60'000;
    CHECK(route_alert(AlertEvent::Snapback, snoozed, kNow, at(12)).suppressed_by ==
          AlertSuppression::Snoozed);

    const AlertDeliverySettings defaults;
    CHECK(route_alert(AlertEvent::Snapback, defaults, kNow, at(12)).suppressed_by ==
          AlertSuppression::None);
}

TEST_CASE("snooze outranks quiet hours in the reported reason") {
    // Both apply; the snooze is the user's most recent explicit act, so it is the more useful
    // thing to be told.
    auto settings = with_quiet_hours();
    settings.snoozed_until_wall_ms = kNow + 60'000;

    const auto route = route_alert(AlertEvent::Snapback, settings, kNow, at(23, 30));
    CHECK_FALSE(route.visible());
    CHECK(route.suppressed_by == AlertSuppression::Snoozed);
}

TEST_CASE("a lapsed snooze deadline stops silencing") {
    // The deadline is the promise. Nothing rewrites the field when it passes -- a stale value
    // is inert, and writing settings.json from the tick to tidy it would be disk churn.
    AlertDeliverySettings settings;
    settings.snoozed_until_wall_ms = kNow;

    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(12)).visible());
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow + 1, at(12)).visible());
    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow - 1, at(12)).visible());
}

TEST_CASE("an unavailable local reading fails open") {
    // A machine whose locale data cannot answer must not go permanently silent. The user could
    // not tell that apart from "nothing happened"; a missed quiet hour they can see.
    const auto settings = with_quiet_hours();

    const auto route = route_alert(AlertEvent::Snapback, settings, kNow, std::nullopt);
    CHECK(route.visible());
    CHECK(route.suppressed_by == AlertSuppression::ConversionFailed);
}

TEST_CASE("a snooze silences even when the local reading is unavailable") {
    // Snooze is compared against an instant, so it does not need local time at all. If a
    // broken conversion could defeat it, the tray action would be unreliable on exactly the
    // machines that most need a way to shut the app up.
    AlertDeliverySettings settings;
    settings.snoozed_until_wall_ms = kNow + 60'000;

    CHECK_FALSE(route_alert(AlertEvent::Snapback, settings, kNow, std::nullopt).visible());
}

TEST_CASE("untracked work obeys snooze and quiet hours without its own preference") {
    // It has no settings field by design, but exempting it would make quiet hours a lie.
    const AlertDeliverySettings defaults;
    CHECK(route_alert(AlertEvent::UntrackedWork, defaults, kNow, at(12)).channels.in_app == true);

    auto quiet = with_quiet_hours();
    CHECK_FALSE(route_alert(AlertEvent::UntrackedWork, quiet, kNow, at(23, 30)).visible());

    AlertDeliverySettings snoozed;
    snoozed.snoozed_until_wall_ms = kNow + 60'000;
    CHECK_FALSE(route_alert(AlertEvent::UntrackedWork, snoozed, kNow, at(12)).visible());
}

TEST_CASE("the preview mode rides along regardless of channel") {
    // The delivery layer reads preview off the route rather than the settings, so it must be
    // populated even when the route is about to be suppressed -- otherwise a later refactor
    // that logs the route loses the distinction.
    AlertDeliverySettings settings = with_quiet_hours();
    settings.preview = AlertPreviewMode::Generic;

    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(12)).preview ==
          AlertPreviewMode::Generic);
    CHECK(route_alert(AlertEvent::Snapback, settings, kNow, at(23, 30)).preview ==
          AlertPreviewMode::Generic);
}

TEST_CASE("minute_in_quiet_range is total across the day") {
    // Every minute answers, in both the wrapping and non-wrapping directions, with no minute
    // claimed by both a range and its complement.
    for (int minute = 0; minute < kMinutesPerDay; ++minute) {
        const bool wrapping = minute_in_quiet_range(minute, at(22), at(7));
        CHECK(wrapping == (minute >= at(22) || minute < at(7)));

        const bool inside = minute_in_quiet_range(minute, at(9), at(17));
        CHECK(inside == (minute >= at(9) && minute < at(17)));
    }
}
