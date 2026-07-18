#include <doctest/doctest.h>

#include "engine/idle_detector.hpp"

using namespace snapback;

namespace {
constexpr std::int64_t kThreshold = 5 * 60 * 1000;  // 5 min
}

TEST_CASE("IdleDetector starts Active and seeds baseline on first poll") {
    IdleDetector d(kThreshold);
    CHECK(d.state() == IdleState::Active);
    // First poll can't know prior idle time, so it only seeds the clock.
    CHECK(d.poll(1'000'000) == IdleTransition::None);
    CHECK(d.idle_for_ms(1'000'000) == 0);
}

TEST_CASE("IdleDetector goes idle exactly once at the threshold") {
    IdleDetector d(kThreshold);
    d.on_activity(0);
    CHECK(d.poll(kThreshold - 1) == IdleTransition::None);   // just under
    CHECK(d.state() == IdleState::Active);
    CHECK(d.poll(kThreshold) == IdleTransition::WentIdle);    // crosses
    CHECK(d.state() == IdleState::Idle);
    CHECK(d.poll(kThreshold + 5000) == IdleTransition::None); // stays idle, no repeat edge
}

TEST_CASE("IdleDetector wakes on activity after going idle") {
    IdleDetector d(kThreshold);
    d.on_activity(0);
    d.poll(kThreshold);
    REQUIRE(d.state() == IdleState::Idle);

    CHECK(d.on_activity(kThreshold + 100) == IdleTransition::WokeUp);
    CHECK(d.state() == IdleState::Active);
    // Fresh window: idle clock restarts from the wake time.
    CHECK(d.poll(kThreshold + 100 + kThreshold - 1) == IdleTransition::None);
    CHECK(d.poll(kThreshold + 100 + kThreshold) == IdleTransition::WentIdle);
}

TEST_CASE("IdleDetector: activity while active does not transition") {
    IdleDetector d(kThreshold);
    d.on_activity(0);
    CHECK(d.on_activity(1000) == IdleTransition::None);
    CHECK(d.state() == IdleState::Active);
    CHECK(d.idle_for_ms(3000) == 2000);
}

TEST_CASE("IdleDetector: non-positive threshold disables idle detection") {
    IdleDetector d(0);
    d.on_activity(0);
    CHECK(d.poll(1'000'000'000) == IdleTransition::None);
    CHECK(d.state() == IdleState::Active);
}
