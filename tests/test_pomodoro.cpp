#include "doctest_wrapper.hpp"

#include <nlohmann/json.hpp>

#include "engine/pomodoro.hpp"

using namespace snapback;

namespace {
constexpr std::int64_t kWork = 25 * 60 * 1000;
constexpr std::int64_t kShort = 5 * 60 * 1000;
constexpr std::int64_t kLong = 15 * 60 * 1000;
}  // namespace

TEST_CASE("PomodoroTimer starts in Work and counts down") {
    PomodoroTimer t;
    CHECK_FALSE(t.running());
    t.start(0);
    CHECK(t.running());
    CHECK(t.phase() == PomodoroPhase::Work);
    CHECK(t.remaining_ms(0) == kWork);
    CHECK(t.remaining_ms(kWork - 1000) == 1000);
    CHECK_FALSE(t.poll(kWork - 1));  // not yet
}

TEST_CASE("PomodoroTimer advances Work -> ShortBreak -> Work") {
    PomodoroTimer t;
    t.start(0);
    CHECK(t.poll(kWork));  // work done
    CHECK(t.phase() == PomodoroPhase::ShortBreak);
    CHECK(t.completed_work_intervals() == 1);
    CHECK(t.poll(kWork + kShort));  // break done
    CHECK(t.phase() == PomodoroPhase::Work);
}

TEST_CASE("PomodoroTimer takes a long break after 4 work intervals") {
    PomodoroTimer t;
    t.start(0);
    std::int64_t clock = 0;
    // Run three Work+ShortBreak cycles.
    for (int i = 0; i < 3; ++i) {
        clock += kWork;
        t.poll(clock);
        CHECK(t.phase() == PomodoroPhase::ShortBreak);
        clock += kShort;
        t.poll(clock);
        CHECK(t.phase() == PomodoroPhase::Work);
    }
    // Fourth work block ends -> long break.
    clock += kWork;
    t.poll(clock);
    CHECK(t.completed_work_intervals() == 4);
    CHECK(t.phase() == PomodoroPhase::LongBreak);
    CHECK(t.remaining_ms(clock) == kLong);
}

TEST_CASE("PomodoroTimer poll catches up across a big time jump") {
    PomodoroTimer t;
    t.start(0);
    // Jump past Work + ShortBreak in one poll -> lands back in Work, no skipped phases.
    CHECK(t.poll(kWork + kShort + 10));
    CHECK(t.phase() == PomodoroPhase::Work);
    CHECK(t.completed_work_intervals() == 1);
}

TEST_CASE("PomodoroTimer stopped timer does nothing") {
    PomodoroTimer t;
    CHECK_FALSE(t.poll(kWork * 10));
    CHECK(t.remaining_ms(0) == 0);
}

TEST_CASE("PomodoroTimer reset clears progress for a new focus session") {
    PomodoroTimer t;
    t.start(0);
    t.poll(kWork);
    REQUIRE(t.completed_work_intervals() == 1);
    t.reset();
    CHECK_FALSE(t.running());
    CHECK(t.phase() == PomodoroPhase::Work);
    CHECK(t.completed_work_intervals() == 0);
    CHECK(t.remaining_ms(kWork) == 0);
}

TEST_CASE("PomodoroStatus serializes the stable camelCase IPC contract") {
    // Designated initializers rather than positional: this is the wire contract's test, and
    // the positional form silently reinterpreted every field when 2.13 added two flags.
    PomodoroStatus status;
    status.running = true;
    status.paused = false;
    status.awaiting_acknowledgement = true;
    status.phase = PomodoroPhase::LongBreak;
    status.completed_work_intervals = 4;
    status.remaining_ms = 1234;

    const nlohmann::json json = status;
    CHECK(json.at("running") == true);
    CHECK(json.at("paused") == false);
    CHECK(json.at("awaitingAcknowledgement") == true);
    CHECK(json.at("phase") == "longBreak");
    CHECK(json.at("completedWorkIntervals") == 4);
    CHECK(json.at("remainingMs") == 1234);
}

// --- Roadmap 2.13: pause/resume, skip, restart, and the relaunch policy ---------------------

TEST_CASE("pausing freezes the countdown and resuming costs the phase nothing") {
    PomodoroTimer t;
    t.start(0);
    t.pause(10 * 60 * 1000);
    CHECK(t.paused());
    CHECK(t.remaining_ms(10 * 60 * 1000) == kWork - 10 * 60 * 1000);

    // However long the pause lasts, the remainder is the remainder.
    CHECK(t.remaining_ms(9 * 60 * 60 * 1000) == kWork - 10 * 60 * 1000);
    CHECK_FALSE(t.poll(9 * 60 * 60 * 1000));
    CHECK(t.phase() == PomodoroPhase::Work);

    t.resume(9 * 60 * 60 * 1000);
    CHECK_FALSE(t.paused());
    CHECK(t.remaining_ms(9 * 60 * 60 * 1000) == kWork - 10 * 60 * 1000);
}

TEST_CASE("skipping work does not credit an interval nobody worked") {
    // The long-break cadence is a reward for work done. If skips counted, four clicks would
    // earn a long break.
    PomodoroTimer t;
    t.start(0);
    t.skip(1000);
    CHECK(t.phase() == PomodoroPhase::ShortBreak);
    CHECK(t.completed_work_intervals() == 0);

    t.skip(2000);
    CHECK(t.phase() == PomodoroPhase::Work);
    CHECK(t.completed_work_intervals() == 0);
    CHECK(t.remaining_ms(2000) == kWork);
}

TEST_CASE("restarting a phase replays it without losing the intervals already earned") {
    PomodoroTimer t;
    t.start(0);
    REQUIRE(t.poll(kWork));                    // one interval genuinely completed
    REQUIRE(t.completed_work_intervals() == 1);

    t.restart_phase(kWork + 60 * 1000);
    CHECK(t.phase() == PomodoroPhase::ShortBreak);
    CHECK(t.remaining_ms(kWork + 60 * 1000) == kShort);
    CHECK(t.completed_work_intervals() == 1);  // kept: restart is not a reset
}

TEST_CASE("with auto-start off a finished phase waits instead of running on") {
    PomodoroConfig config;
    config.auto_start_next_phase = false;
    PomodoroTimer t(config);
    t.start(0);

    // The boundary is still reported, once, so the UI can alert.
    CHECK(t.poll(kWork));
    CHECK(t.awaiting_acknowledgement());
    CHECK(t.remaining_ms(kWork) == 0);
    CHECK(t.pending_phase() == PomodoroPhase::ShortBreak);
    CHECK(t.completed_work_intervals() == 1);

    // And not again on every later tick, however long the user is away.
    CHECK_FALSE(t.poll(kWork + 60 * 60 * 1000));

    t.acknowledge(kWork + 60 * 60 * 1000);
    CHECK_FALSE(t.awaiting_acknowledgement());
    CHECK(t.phase() == PomodoroPhase::ShortBreak);
    CHECK(t.remaining_ms(kWork + 60 * 60 * 1000) == kShort);
}

TEST_CASE("a relaunch inside a phase resumes the time genuinely left") {
    PomodoroTimer before;
    before.start(0);
    // 10 minutes in on both clocks; wall clock is what survives.
    const auto snap = before.snapshot(1'700'000'000'000, 10 * 60 * 1000);

    PomodoroTimer after;
    // Two more minutes passed while the app was closed, and the steady clock restarted at 0.
    after.restore(snap, 1'700'000'000'000 + 2 * 60 * 1000, 0);
    CHECK(after.running());
    CHECK(after.phase() == PomodoroPhase::Work);
    CHECK(after.remaining_ms(0) == kWork - 12 * 60 * 1000);
}

TEST_CASE("a deadline that passed while the app was closed waits rather than advancing") {
    // 2.13's relaunch policy, and the reason it exists: silently chaining through the phases
    // that "would have" elapsed credits work intervals nobody worked and hands out a long
    // break for an afternoon the app spent shut.
    PomodoroTimer before;
    before.start(0);
    const auto snap = before.snapshot(1'700'000'000'000, 0);

    PomodoroTimer after;
    // Gone for eight hours -- long enough for many phases, had anyone been watching.
    after.restore(snap, 1'700'000'000'000 + 8 * 60 * 60 * 1000, 0);
    CHECK(after.running());
    CHECK(after.awaiting_acknowledgement());
    CHECK(after.remaining_ms(0) == 0);
    CHECK(after.completed_work_intervals() == 0);   // nothing was earned in absentia
    CHECK(after.pending_phase() == PomodoroPhase::ShortBreak);

    // The user comes back and says so; only then does the break begin.
    after.acknowledge(0);
    CHECK(after.phase() == PomodoroPhase::ShortBreak);
    CHECK(after.remaining_ms(0) == kShort);
}

TEST_CASE("a paused timer restores paused, with its remaining time intact") {
    PomodoroTimer before;
    before.start(0);
    before.pause(5 * 60 * 1000);
    const auto snap = before.snapshot(1'700'000'000'000, 5 * 60 * 1000);

    PomodoroTimer after;
    after.restore(snap, 1'700'000'000'000 + 24 * 60 * 60 * 1000, 0);
    CHECK(after.running());
    CHECK(after.paused());
    CHECK(after.remaining_ms(0) == kWork - 5 * 60 * 1000);
}

TEST_CASE("a stopped timer restores stopped rather than resuming a stale deadline") {
    PomodoroTimer before;
    before.start(0);
    before.stop();
    const auto snap = before.snapshot(1'700'000'000'000, 0);

    PomodoroTimer after;
    after.restore(snap, 1'700'000'000'000, 0);
    CHECK_FALSE(after.running());
    CHECK(after.remaining_ms(0) == 0);
}

TEST_CASE("an awaiting timer restores awaiting rather than starting the phase for the user") {
    PomodoroConfig config;
    config.auto_start_next_phase = false;
    PomodoroTimer before(config);
    before.start(0);
    REQUIRE(before.poll(kWork));
    REQUIRE(before.awaiting_acknowledgement());
    const auto snap = before.snapshot(1'700'000'000'000, kWork);

    PomodoroTimer after(config);
    after.restore(snap, 1'700'000'000'000 + 60 * 1000, 0);
    CHECK(after.awaiting_acknowledgement());
    CHECK(after.pending_phase() == PomodoroPhase::ShortBreak);
    CHECK(after.completed_work_intervals() == 1);
}

TEST_CASE("pause, skip and restart do nothing to a timer that was never started") {
    // Every one of these is reachable from a UI control, and a stopped timer is exactly the
    // state those controls are visible in before the first start.
    PomodoroTimer t;
    t.pause(1000);
    t.resume(2000);
    t.skip(3000);
    t.restart_phase(4000);
    t.acknowledge(5000);
    CHECK_FALSE(t.running());
    CHECK_FALSE(t.paused());
    CHECK_FALSE(t.awaiting_acknowledgement());
    CHECK(t.completed_work_intervals() == 0);
    CHECK(t.remaining_ms(5000) == 0);
}
