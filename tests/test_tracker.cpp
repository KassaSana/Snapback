#include <doctest/doctest.h>

#include "snapback/tracker.hpp"

using namespace snapback;

namespace {

std::vector<AppRuleRecord> no_rules() {
    return {};
}

}  // namespace

TEST_CASE("ContextTracker snapshots meaningful on-task window changes") {
    ContextTracker tracker;
    tracker.set_prediction_feedback("PRODUCTIVE", std::nullopt);

    auto snapshot = tracker.observe_window_change("Cursor", "main.rs - Snapback",
                                                  no_rules(), 10.0, "2026-07-12T10:00:00Z");

    REQUIRE(snapshot.has_value());
    CHECK(snapshot->app_name == "Cursor");
    CHECK(snapshot->file_hint == "main.rs");
    CHECK(snapshot->summary == "Editing main.rs");
}

TEST_CASE("ContextTracker does not persist off-task window changes") {
    ContextTracker tracker;
    tracker.set_prediction_feedback("PRODUCTIVE", std::nullopt);

    REQUIRE(tracker.observe_window_change("Cursor", "main.rs", no_rules(), 10.0,
                                          "2026-07-12T10:00:00Z")
                .has_value());

    auto snapshot = tracker.observe_window_change("Google Chrome", "YouTube", no_rules(), 11.0,
                                                  "2026-07-12T10:00:01Z");

    CHECK(snapshot == std::nullopt);
    CHECK(tracker.state() == DistractionState::Distracted);
}

TEST_CASE("ContextTracker checkpoints focused on-task context after interval") {
    ContextTracker tracker;
    tracker.set_snapshot_interval_secs(30.0);
    tracker.set_prediction_feedback("DEEP_FOCUS", std::nullopt);
    tracker.observe_window_change("Cursor", "tracker.cpp - Snapback", no_rules(), 10.0,
                                  "2026-07-12T10:00:00Z");

    CHECK(tracker.maybe_checkpoint_snapshot(no_rules(), 39.0,
                                            "2026-07-12T10:00:29Z") == std::nullopt);
    auto checkpoint = tracker.maybe_checkpoint_snapshot(no_rules(), 40.0,
                                                        "2026-07-12T10:00:30Z");

    REQUIRE(checkpoint.has_value());
    CHECK(checkpoint->window_title == "tracker.cpp - Snapback");
    CHECK(checkpoint->timestamp == "2026-07-12T10:00:30Z");
}

TEST_CASE("ContextTracker emits snapback only after a long distraction") {
    ContextTracker tracker;
    tracker.set_min_distraction_secs(30.0);
    tracker.set_prediction_feedback("PRODUCTIVE", std::optional<std::string>("implement api"));
    tracker.observe_window_change("Cursor", "api.cpp - Snapback", no_rules(), 10.0,
                                  "2026-07-12T10:00:00Z");
    tracker.observe_window_change("Google Chrome", "YouTube", no_rules(), 20.0,
                                  "2026-07-12T10:00:10Z");

    tracker.set_prediction_feedback("PRODUCTIVE", std::optional<std::string>("implement api"));
    tracker.observe_window_change("Cursor", "api.cpp - Snapback", no_rules(), 55.0,
                                  "2026-07-12T10:00:45Z");

    auto snapback = tracker.take_pending_snapback();
    REQUIRE(snapback.has_value());
    CHECK(snapback->app_name == "Cursor");
    CHECK(snapback->file_hint == "api.cpp");
    CHECK(snapback->distraction_duration_secs == 35);
    CHECK(tracker.take_pending_snapback() == std::nullopt);
}

TEST_CASE("ContextTracker stays distracted when classifier says return app is distracted") {
    ContextTracker tracker;
    tracker.set_min_distraction_secs(0.0);
    tracker.set_prediction_feedback("PRODUCTIVE", std::optional<std::string>("implement api"));
    tracker.observe_window_change("Cursor", "lib.cpp", no_rules(), 10.0,
                                  "2026-07-12T10:00:00Z");
    tracker.observe_window_change("Google Chrome", "YouTube", no_rules(), 11.0,
                                  "2026-07-12T10:00:01Z");

    tracker.set_prediction_feedback("DISTRACTED", std::optional<std::string>("implement api"));
    tracker.observe_window_change("Slack", "#random", no_rules(), 12.0,
                                  "2026-07-12T10:00:02Z");

    CHECK(tracker.state() == DistractionState::Distracted);
    CHECK(tracker.take_pending_snapback() == std::nullopt);
}
