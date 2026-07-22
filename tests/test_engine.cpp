#include "doctest_wrapper.hpp"

#include "capture/ring_buffer.hpp"
#include "engine/app_context.hpp"
#include "engine/classifier.hpp"
#include "engine/features.hpp"

using namespace snapback;

namespace {

CaptureEvent event(EventType type, double ts, const char* app, const char* title,
                   std::uint32_t mouse_speed = 0, std::uint32_t idle_ms = 0) {
    CaptureEvent ev;
    ev.event_type = type;
    ev.timestamp_secs = ts;
    ev.app_name = app;
    ev.window_title = title;
    ev.mouse_speed = mouse_speed;
    ev.idle_duration_ms = idle_ms;
    return ev;
}

FeatureVector stable_features() {
    FeatureVector f;
    f.app_name = "Cursor";
    f.window_title = "classifier.cpp - Snapback";
    f.keystroke_count() = 8.0;
    f.keystroke_rate() = 3.0;
    f.keystroke_interval_std() = 0.15;
    f.context_switches_30s() = 0.0;
    f.context_switches_5min() = 1.0;
    f.unique_apps_5min() = 1.0;
    f.time_in_current_app() = 200.0;
    f.is_ide() = 1.0;
    return f;
}

AppRuleRecord rule(const char* pattern, AppRuleKind kind) {
    AppRuleRecord r;
    r.pattern = pattern;
    r.rule_type = kind;
    return r;
}

}  // namespace

TEST_CASE("app context detects defaults and lets block rules win") {
    auto browser = classify_app_context("Google Chrome", "Rust documentation");
    CHECK(browser.is_browser);
    CHECK_FALSE(browser.title_is_distracting);

    auto youtube = classify_app_context("Google Chrome", "Video - YouTube");
    CHECK(youtube.is_browser);
    CHECK(youtube.title_is_distracting);
    CHECK(is_clearly_off_task(youtube));

    std::vector<AppRuleRecord> rules = {
        rule("slack", AppRuleKind::Allow),
        rule("slack", AppRuleKind::Block),
    };
    auto slack = classify_app_context("Slack", "#general", rules);
    CHECK(slack.personal_block);
    CHECK_FALSE(slack.personal_allow);
}

TEST_CASE("app context on-task parity matches personal rule semantics") {
    CHECK(snapback_on_task(classify_app_context("Cursor", "classifier.cpp - Snapback"),
                           "classifier.cpp - Snapback"));

    auto youtube = classify_app_context("Google Chrome", "Rick Astley - YouTube");
    CHECK(youtube.title_is_distracting);
    CHECK_FALSE(snapback_on_task(youtube, "Rick Astley - YouTube", "PRODUCTIVE"));

    auto discord = classify_app_context("Discord", "Study group",
                                        {rule("discord", AppRuleKind::Allow)});
    CHECK(discord.personal_allow);
    CHECK(snapback_on_task(discord, "Study group"));

    auto notion = classify_app_context("Notion", "Weekly plan",
                                       {rule("notion", AppRuleKind::Block)});
    CHECK(notion.personal_block);
    CHECK_FALSE(snapback_on_task(notion, "Weekly plan", "PRODUCTIVE"));

    auto slack = classify_app_context("Slack", "#general");
    CHECK(snapback_on_task(slack, "#general", "PRODUCTIVE"));
    CHECK_FALSE(snapback_on_task(slack, "#general", "DISTRACTED"));

    auto new_tab = classify_app_context("Google Chrome", "New Tab");
    CHECK_FALSE(snapback_on_task(new_tab, "New Tab"));
    CHECK(snapback_on_task(new_tab, "New Tab", "PRODUCTIVE"));
}

TEST_CASE("goal alignment accepts user-defined categories") {
    const auto ctx = classify_app_context("Cursor", "brand-guide.md");
    const std::vector<GoalCategory> categories = {{"design", {"brand", "visual", "ui"}}};
    CHECK(goal_alignment_score(std::optional<std::string>("Finish the brand guide"), ctx,
                               "brand-guide.md", categories) == doctest::Approx(0.95));
    CHECK(snapback_on_task(ctx, "brand-guide.md", std::nullopt,
                           std::optional<std::string>("Finish the brand guide"), categories));
}

TEST_CASE("feature extractor computes rolling keyboard mouse and context features") {
    FeatureExtractor extractor;
    extractor.reset_for_session(100.0);

    extractor.update(event(EventType::WindowFocusChange, 100.0, "Cursor", "main.cpp"));
    extractor.update(event(EventType::KeyPress, 101.0, "Cursor", "main.cpp"));
    extractor.update(event(EventType::KeyPress, 103.0, "Cursor", "main.cpp"));
    extractor.update(event(EventType::MouseMove, 104.0, "Cursor", "main.cpp", 10));
    extractor.update(event(EventType::MouseMove, 105.0, "Cursor", "main.cpp", 20));
    auto features = extractor.update(event(EventType::MouseClick, 106.0, "Cursor", "main.cpp"));

    CHECK(features.seconds_since_session_start() == doctest::Approx(6.0));
    CHECK(features.keystroke_count() == doctest::Approx(2.0));
    CHECK(features.keystroke_interval_mean() == doctest::Approx(2.0));
    CHECK(features.mouse_move_count() == doctest::Approx(2.0));
    CHECK(features.mouse_speed_mean() == doctest::Approx(15.0));
    CHECK(features.mouse_distance_pixels() == doctest::Approx(20.0));
    CHECK(features.mouse_click_count() == doctest::Approx(1.0));
    CHECK(features.context_switches_30s() == doctest::Approx(1.0));
    CHECK(features.is_ide() == doctest::Approx(1.0));
}

TEST_CASE("begin_session seeds the session origin from the first event") {
    // The production path: AppState can't supply a start timestamp (wall-clock vs the
    // monotonic event clock), so the origin comes from the first event instead. Before
    // this existed, start_session passed nullopt and feature[0] was pinned to 0.0 forever.
    FeatureExtractor extractor;
    extractor.begin_session();

    extractor.update(event(EventType::WindowFocusChange, 500.0, "Cursor", "main.cpp"));
    auto features = extractor.update(event(EventType::KeyPress, 512.0, "Cursor", "main.cpp"));

    CHECK(features.seconds_since_session_start() == doctest::Approx(12.0));
}

TEST_CASE("no active session leaves the session-start feature at zero") {
    // reset_for_session(nullopt) is the stop path and must NOT lazily seed — with no
    // session there is nothing to measure elapsed time against.
    FeatureExtractor extractor;
    extractor.reset_for_session(std::nullopt);

    auto features = extractor.update(event(EventType::KeyPress, 900.0, "Cursor", "main.cpp"));

    CHECK(features.seconds_since_session_start() == doctest::Approx(0.0));
}

TEST_CASE("feature extractor interning preserves unique_apps_5min count") {
    FeatureExtractor extractor;
    extractor.update(event(EventType::WindowFocusChange, 0.0, "Cursor", "a.cpp"));
    extractor.update(event(EventType::KeyPress, 1.0, "Cursor", "a.cpp"));
    extractor.update(event(EventType::WindowFocusChange, 2.0, "Chrome", "docs"));
    extractor.update(event(EventType::WindowFocusChange, 3.0, "Discord", "chat"));
    auto features = extractor.update(event(EventType::KeyPress, 4.0, "Cursor", "b.cpp"));

    CHECK(features.unique_apps_5min() == doctest::Approx(3.0));
}

TEST_CASE("feature extractor trims 30s window while keeping 5min history") {
    FeatureExtractor extractor;
    extractor.update(event(EventType::WindowFocusChange, 0.0, "A", "A"));
    extractor.update(event(EventType::WindowFocusChange, 1.0, "B", "B"));
    auto features = extractor.update(event(EventType::WindowFocusChange, 40.0, "C", "C"));

    CHECK(features.context_switches_30s() == doctest::Approx(1.0));
    CHECK(features.context_switches_5min() == doctest::Approx(3.0));
}

TEST_CASE("feature extractor resets break timer after long idle") {
    FeatureExtractor extractor;
    extractor.update(event(EventType::WindowFocusChange, 0.0, "Cursor", "main.cpp"));
    extractor.update(event(EventType::IdleEnd, 400.0, "Cursor", "main.cpp", 0, 300000));
    auto features = extractor.update(event(EventType::KeyPress, 401.0, "Cursor", "main.cpp"));

    CHECK(features.idle_event_count_5min() == doctest::Approx(1.0));
    CHECK(features.idle_time_30s() == doctest::Approx(300.0));
    CHECK(features.minutes_since_last_break() == doctest::Approx(0.0));
}

TEST_CASE("classifier identifies stable work and high-thrash distraction") {
    Classifier classifier;

    auto stable = classifier.predict(stable_features(), FocusMode::Normal);
    CHECK(stable.thrash_score < 0.35);
    CHECK(stable.drift_score < 0.35);
    CHECK(stable.distraction_risk < 0.5);

    auto chaotic = stable_features();
    chaotic.context_switches_30s() = 5.0;
    chaotic.context_switches_5min() = 12.0;
    chaotic.unique_apps_5min() = 6.0;
    auto distracted = classifier.predict(chaotic, FocusMode::Normal);
    CHECK(distracted.thrash_score >= 0.75);
    CHECK(distracted.focus_state == "DISTRACTED");
}

TEST_CASE("classifier applies personal block and recovery threshold guardrails") {
    Classifier classifier;
    auto notion = stable_features();
    notion.app_name = "Notion";
    notion.window_title = "Weekly plan";
    notion.is_ide() = 0.0;
    notion.is_productivity() = 1.0;

    auto blocked = classifier.predict(notion, FocusMode::Normal, std::nullopt,
                                      {rule("notion", AppRuleKind::Block)});
    CHECK(blocked.focus_state == "DISTRACTED");

    PredictionScores borderline;
    borderline.focus_state = "PRODUCTIVE";
    borderline.distraction_risk = 0.72;
    CHECK(apply_focus_guardrails(borderline, 0.1, 0.1, false, FocusMode::Normal).focus_state ==
          "DISTRACTED");
    CHECK(apply_focus_guardrails(borderline, 0.1, 0.1, false, FocusMode::Recovery).focus_state ==
          "PRODUCTIVE");
}

TEST_CASE("ring buffer preserves SPSC order and reports full state") {
    RingBuffer<int, 4> buffer;

    CHECK(buffer.push(1));
    CHECK(buffer.push(2));
    CHECK(buffer.push(3));
    CHECK_FALSE(buffer.push(4));

    CHECK(buffer.pop() == 1);
    CHECK(buffer.pop() == 2);
    CHECK(buffer.push(4));
    CHECK(buffer.pop() == 3);
    CHECK(buffer.pop() == 4);
    CHECK(buffer.pop() == std::nullopt);
}
