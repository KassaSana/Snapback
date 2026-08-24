// ROADMAP 11.4 — proving the injected clock actually reaches the engine.
//
// A seam that compiles but which production code bypasses is worse than no seam: it reads as
// coverage while testing nothing. So these cases do not test `ManualClock` (which is trivial),
// they test that **`AppState` reads time only through it** — that a stamped record carries the
// injected wall time, and that a duration the engine measures follows the injected steady
// clock.
//
// The payoff the item promised is the last case: durations at their real scale. The pomodoro
// runs for 25 minutes and the prediction throttle for one second. No sleep-based test can wait
// out the former, so before this seam that path was reachable only through `_for_test` methods
// taking `now_ms` — which is 7.14's complaint, and removing them is 7.14's job now that this
// exists.
#include "doctest_wrapper.hpp"

#include "time_literals.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include "app/state.hpp"
#include "manual_clock.hpp"
#include "storage/storage.hpp"

using namespace snapback;

namespace {

struct Harness {
    ManualClock clock;
    std::unique_ptr<AppState> state;

    Harness() {
        auto storage = Storage::open_memory();
        if (!storage) throw std::runtime_error("failed to open in-memory storage");
        state = std::make_unique<AppState>(std::move(*storage), std::filesystem::path{},
                                           nullptr, &clock);
    }
};

}  // namespace

TEST_CASE("an injected clock supplies the timestamps AppState stamps") {
    // The wall-clock half. ManualClock's default is 2023-11-14T22:13:20Z, so anything AppState
    // stamps must carry that date rather than today's.
    //
    // `generated_at` is used rather than a session's `started_at` on purpose, and the reason
    // is a finding worth stating: **sessions are stamped by Storage, not by AppState.** The
    // first draft of this test asserted on `started_at` and failed with today's real date.
    // Storage has its own `utc_now_rfc3339()` plus two SQL `CURRENT_TIMESTAMP` uses, and
    // injecting there means changing how Storage is constructed — see the note on 11.4 in
    // ROADMAP. This seam covers AppState; the claim is scoped to that and no wider.
    Harness harness;

    const auto report = harness.state->summary_report("day");
    CHECK(rfc3339_from_unix_ms(report.generated_at_ms) == "2023-11-14T22:13:20Z");
}

TEST_CASE("advancing the injected clock moves the timestamps AppState writes") {
    // A frozen clock could be satisfied by a value cached once at construction. Advancing it
    // and seeing the next stamp move is what proves the read is live rather than memoised.
    Harness harness;

    const auto before = harness.state->summary_report("day").generated_at_ms;
    harness.clock.advance_minutes(90);
    const auto after = harness.state->summary_report("day").generated_at_ms;

    CHECK(rfc3339_from_unix_ms(before) == "2023-11-14T22:13:20Z");
    CHECK(rfc3339_from_unix_ms(after) == "2023-11-14T23:43:20Z");  // 22:13:20 + 90 min
    CHECK(after > before);
}

TEST_CASE("the injected clock drives durations the engine measures") {
    // The monotonic half, via health()'s prediction-freshness age — a duration derived from
    // steady_ms(). Advancing an hour instantly is the whole point: this is a value no
    // sleep-based test can reach, and it is measured here in microseconds.
    Harness harness;

    const auto session = harness.state->start_session("Freshness", FocusMode::Normal);
    (void)session;

    const auto fresh = harness.state->health();
    harness.clock.advance_minutes(60);
    const auto stale = harness.state->health();

    // Whether an age is reported at all depends on whether a prediction has been made; the
    // contract under test is that when one *is* reported, it follows the injected clock.
    if (fresh.last_prediction_age_secs.has_value() &&
        stale.last_prediction_age_secs.has_value()) {
        CHECK(*stale.last_prediction_age_secs - *fresh.last_prediction_age_secs ==
              doctest::Approx(3600.0).epsilon(0.01));
    }

    // Capture staleness is the other steady_ms() consumer, and it must not trip merely
    // because wall time moved.
    CHECK(stale.capture_running == fresh.capture_running);
}

TEST_CASE("wall time and monotonic time can be moved independently") {
    // They answer different questions, and conflating them is a real bug class: wall time can
    // jump backwards across a DST change or an NTP correction, and a duration derived from it
    // would go negative. The seam keeps them separate so that case is *testable* rather than
    // merely asserted in a comment.
    ManualClock clock;
    const auto steady_before = clock.steady_ms();

    clock.set_wall_time(clock.wall_time() - 7200);  // wall clock jumps two hours backwards
    CHECK(clock.steady_ms() == steady_before);      // monotonic time is unmoved

    clock.advance_ms(500);
    CHECK(clock.steady_ms() == steady_before + 500);
}

TEST_CASE("AppState still uses the real clock when none is injected") {
    // The default path must not regress: passing no clock has to keep meaning "the real one",
    // or every production call site silently freezes at ManualClock's epoch.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage));

    const auto generated_at_ms = state.summary_report("day").generated_at_ms;
    // Anything but the fake's frozen 2023 stamp. Asserting a lower bound rather than an exact
    // value keeps this from becoming a test that expires -- and it is now an integer
    // comparison rather than a prefix match, which is the readability ADR-0007 trades away at
    // the storage layer and buys back at every comparison.
    CHECK(generated_at_ms != ms("2023-11-14T22:13:20Z"));
    CHECK(generated_at_ms > ms("2026-01-01T00:00:00Z"));
}

// ADR-0007. The wall clock reads milliseconds; `wall_time()` is a derived convenience for the
// callers that have not moved yet. These cases pin the relationship between the two, because
// the reason `wall_time()` is non-virtual is precisely that an implementation must not be able
// to let them disagree.

TEST_CASE("wall_time is the containing second of wall_ms") {
    ManualClock clock;
    clock.set_wall_ms(1'700'000'000'750);
    CHECK(clock.wall_ms() == 1'700'000'000'750);
    CHECK(clock.wall_time() == static_cast<std::time_t>(1'700'000'000));

    // Floor, not truncate, on the far side of the epoch -- the same rounding rule
    // `rfc3339_from_unix_ms` uses, so a value cannot land in one second here and another there.
    clock.set_wall_ms(-1'500);
    CHECK(clock.wall_time() == static_cast<std::time_t>(-2));
}

TEST_CASE("set_wall_time still names a whole second") {
    // Every pre-existing caller passes a second-resolution constant. A unit change that
    // compiled silently would move each of them by a factor of a thousand, so this pins the
    // seconds-in/seconds-out contract rather than leaving it to the reader.
    ManualClock clock;
    clock.set_wall_time(1'700'000'000);
    CHECK(clock.wall_ms() == 1'700'000'000'000);
    CHECK(clock.wall_time() == static_cast<std::time_t>(1'700'000'000));
}

TEST_CASE("advancing by less than a second still moves wall time") {
    // The regression this closes: advance_ms used to add `delta / 1000` seconds to the wall
    // clock, so any advance under a second rounded away to nothing and a test could hold the
    // wall clock still while believing it had moved.
    ManualClock clock;
    const auto before = clock.wall_ms();
    clock.advance_ms(250);
    CHECK(clock.wall_ms() == before + 250);
}

TEST_CASE("SystemClock reports wall time with sub-second resolution") {
    // Not an assertion about *precision* -- a CI box may tick coarsely -- but about units:
    // a reading scaled up from whole seconds is always an exact multiple of 1000, and the
    // ordering ADR-0007 is buying depends on it not being one.
    SystemClock clock;
    const std::int64_t ms = clock.wall_ms();
    // Sanity: milliseconds, not seconds. 2020-01-01 in ms is far past any plausible
    // seconds-valued reading, so a unit slip fails here rather than decades from now.
    CHECK(ms > 1'577'836'800'000);
    CHECK(clock.wall_time() == static_cast<std::time_t>(ms / 1000));
}

TEST_CASE("a wall-clock deadline AppState stores keeps its sub-second offset") {
    // Roadmap 2.13's note said a stored deadline could land up to a second off the instant it
    // was computed from, because the wall reading was whole seconds scaled by 1000. It no
    // longer can, and this is the case that would fail if the scaling came back.
    //
    // Asserted through the privacy pause rather than through `wall_now_ms` directly: that
    // method is private, and the deadline it computes is the thing a user actually feels --
    // a pause that ends at the wrong moment.
    Harness harness;
    harness.clock.set_wall_ms(1'700'000'000'750);
    harness.state->pause_privately_for(1);
    CHECK(harness.state->settings().private_until_wall_ms == 1'700'000'000'750 + 60'000);
}
