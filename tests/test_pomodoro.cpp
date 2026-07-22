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
    const PomodoroStatus status{true, PomodoroPhase::LongBreak, 4, 1234};
    const nlohmann::json json = status;
    CHECK(json.at("running") == true);
    CHECK(json.at("phase") == "longBreak");
    CHECK(json.at("completedWorkIntervals") == 4);
    CHECK(json.at("remainingMs") == 1234);
}
