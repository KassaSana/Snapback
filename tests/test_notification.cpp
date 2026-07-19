#include <doctest/doctest.h>

#include "app/notification.hpp"

using namespace snapback;

TEST_CASE("build_distraction_notification names the app when known") {
    const auto n = build_distraction_notification("YouTube");
    CHECK(n.title == "Drifting off?");
    CHECK(n.body.find("YouTube") != std::string::npos);
}

TEST_CASE("build_distraction_notification falls back gracefully with no app") {
    const auto n = build_distraction_notification("");
    CHECK_FALSE(n.title.empty());
    CHECK_FALSE(n.body.empty());
    CHECK(n.body.find("wandered off") != std::string::npos);
}

TEST_CASE("build_hyperfocus_notification embeds the minute count") {
    const auto n = build_hyperfocus_notification(90);
    CHECK(n.title == "Time for a break");
    CHECK(n.body.find("90 minutes") != std::string::npos);
}

TEST_CASE("native notification delivery requires title and body") {
    CHECK(notification_payload_is_valid(build_distraction_notification("Cursor")));
    CHECK_FALSE(notification_payload_is_valid(NotificationPayload{"", "body"}));
    CHECK_FALSE(notification_payload_is_valid(NotificationPayload{"title", ""}));
}
