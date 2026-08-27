#include "doctest_wrapper.hpp"

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

TEST_CASE("build_snapback_notification mirrors the overlay's summary line") {
    SnapbackPayload payload;
    payload.summary = "Return to auth.ts";
    payload.app_name = "Cursor";
    payload.distraction_duration_secs = 90;

    const auto n = build_snapback_notification(payload);
    CHECK(n.title == "Welcome back");
    CHECK(n.body == "Return to auth.ts");
    CHECK(notification_payload_is_valid(n));
}

TEST_CASE("build_snapback_notification falls back gracefully with no summary") {
    SnapbackPayload payload;  // summary left empty
    const auto n = build_snapback_notification(payload);
    CHECK_FALSE(n.body.empty());
    CHECK(notification_payload_is_valid(n));
}

// Roadmap 2.16. Generic preview copy — what a lock screen is allowed to say.

TEST_CASE("a generic preview names no app, title, file, or summary") {
    // Sentinels rather than realistic strings: a partial leak of "auth.ts" could coincidentally
    // look clean against a body that happened to mention a file, while ZZFILEZZ cannot.
    SnapbackPayload payload;
    payload.summary = "ZZSUMMARYZZ";
    payload.app_name = "ZZAPPZZ";
    payload.window_title = "ZZTITLEZZ";
    payload.file_hint = "ZZFILEZZ";
    payload.distraction_duration_secs = 4242;

    const auto note = build_snapback_notification(payload, AlertPreviewMode::Generic);
    const std::string rendered = note.title + " " + note.body;

    CHECK(rendered.find("ZZSUMMARYZZ") == std::string::npos);
    CHECK(rendered.find("ZZAPPZZ") == std::string::npos);
    CHECK(rendered.find("ZZTITLEZZ") == std::string::npos);
    CHECK(rendered.find("ZZFILEZZ") == std::string::npos);
    CHECK(rendered.find("4242") == std::string::npos);
    CHECK(notification_payload_is_valid(note));
}

TEST_CASE("a generic preview is a fixed string, not a formatted one") {
    // Pinned by equality so a later refactor that reintroduces interpolation fails here, and
    // not only in the leak case above — which a cleverly-worded format string could pass.
    SnapbackPayload first;
    first.summary = "Return to auth.ts";
    first.app_name = "Cursor";
    SnapbackPayload second;
    second.summary = "Return to billing.rs";
    second.app_name = "Zed";

    const auto a = build_snapback_notification(first, AlertPreviewMode::Generic);
    const auto b = build_snapback_notification(second, AlertPreviewMode::Generic);
    CHECK(a.title == b.title);
    CHECK(a.body == b.body);
    CHECK(a.body == "You're back on task.");
}

TEST_CASE("a generic preview carries no duration either") {
    // The minute count is derived from what the user was doing. A lock screen reading "locked
    // in for 214 minutes" says how long someone has been at their desk.
    const auto hyper = build_hyperfocus_notification(214, AlertPreviewMode::Generic);
    CHECK(hyper.body.find("214") == std::string::npos);
    CHECK(notification_payload_is_valid(hyper));

    const auto untracked = build_untracked_work_notification(97, AlertPreviewMode::Generic);
    CHECK(untracked.body.find("97") == std::string::npos);
    CHECK(notification_payload_is_valid(untracked));
}

TEST_CASE("a detailed preview is the copy this app already sent") {
    // The default path must be untouched by 2.16 — the mode selects, it does not rewrite.
    SnapbackPayload payload;
    payload.summary = "Return to auth.ts";

    CHECK(build_snapback_notification(payload, AlertPreviewMode::Detailed).body ==
          build_snapback_notification(payload).body);
    CHECK(build_hyperfocus_notification(90, AlertPreviewMode::Detailed).body ==
          build_hyperfocus_notification(90).body);
    CHECK(build_untracked_work_notification(20, AlertPreviewMode::Detailed).body ==
          build_untracked_work_notification(20).body);
}

TEST_CASE("the close-to-tray notice names nothing about the user's work") {
    // Roadmap 9.15. It rides the same OS notification history and lock screen as everything
    // else here, so it holds to 2.16's rule even though it has no preview mode: the whole
    // content is that Snapback is still running. It also has to say where to stop it, or the
    // user who wanted to quit is left with a process and no instruction.
    const auto notice = build_close_to_tray_notification();

    CHECK(notification_payload_is_valid(notice));
    CHECK(notice.body.find("tray") != std::string::npos);
    CHECK(notice.body.find("Quit") != std::string::npos);
    // Fixed copy, nothing interpolated: there is no payload to leak from and no count to
    // disclose, which is what keeps it safe to show on a locked screen.
    CHECK(notice.body == build_close_to_tray_notification().body);
}
