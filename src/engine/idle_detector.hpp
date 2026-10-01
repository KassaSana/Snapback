// Idle/AFK state machine. Pure: fed monotonic ms timestamps (`now` is always passed in), it
// reports Active -> Idle and wake transitions. The engine owns the wiring.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace snapback {

// Default AFK threshold: 5 minutes of no input pauses the session.
inline constexpr std::int64_t kDefaultIdleThresholdMs = 5 * 60 * 1000;

enum class IdleState { Active, Idle };

// What the last call did to the state. Callers act on the edges (pause/resume),
// not the level, so we report the transition rather than making them diff state().
enum class IdleTransition { None, WentIdle, WokeUp };

class IdleDetector {
public:
    // A non-positive threshold disables idle detection entirely (the detector stays
    // Active forever) — same "non-positive = off" convention as storage retention.
    explicit IdleDetector(std::int64_t threshold_ms = kDefaultIdleThresholdMs)
        : threshold_ms_(threshold_ms) {}

    // Record real user input observed at `now_ms`. Wakes the detector if it was idle.
    IdleTransition on_activity(std::int64_t now_ms) {
        const std::int64_t gap_ms = std::max<std::int64_t>(0, now_ms - last_activity_ms_);
        last_activity_ms_ = now_ms;
        seen_activity_ = true;
        if (state_ == IdleState::Idle) {
            state_ = IdleState::Active;
            last_idle_duration_ms_ = gap_ms;
            return IdleTransition::WokeUp;
        }
        return IdleTransition::None;
    }

    // Length of the stretch that ended with the latest WokeUp, from the last input before it
    // (threshold included). 0 until the first wake.
    [[nodiscard]] std::int64_t last_idle_duration_ms() const noexcept {
        return last_idle_duration_ms_;
    }

    // Advance time with no new input. Returns WentIdle exactly once, on the tick that
    // crosses the threshold. The first poll just seeds the baseline (we can't know how
    // long the user has been idle before we started watching).
    IdleTransition poll(std::int64_t now_ms) {
        if (!seen_activity_) {
            last_activity_ms_ = now_ms;
            seen_activity_ = true;
            return IdleTransition::None;
        }
        if (threshold_ms_ <= 0) return IdleTransition::None;
        if (state_ == IdleState::Active && (now_ms - last_activity_ms_) >= threshold_ms_) {
            state_ = IdleState::Idle;
            return IdleTransition::WentIdle;
        }
        return IdleTransition::None;
    }

    std::optional<std::int64_t> next_deadline_ms() const noexcept {
        if (!seen_activity_ || threshold_ms_ <= 0 || state_ == IdleState::Idle) return std::nullopt;
        return last_activity_ms_ + threshold_ms_;
    }

    [[nodiscard]] IdleState state() const noexcept { return state_; }
    [[nodiscard]] std::int64_t threshold_ms() const noexcept { return threshold_ms_; }

    // Change the threshold on a running detector. The activity baseline is kept, so a lower
    // threshold can conclude "already idle" on the next poll; only real input wakes it.
    void set_threshold_ms(std::int64_t threshold_ms) noexcept { threshold_ms_ = threshold_ms; }

    // How long since the last input as of `now_ms` (0 before any activity is seen).
    [[nodiscard]] std::int64_t idle_for_ms(std::int64_t now_ms) const noexcept {
        if (!seen_activity_) return 0;
        return std::max<std::int64_t>(0, now_ms - last_activity_ms_);
    }

private:
    std::int64_t threshold_ms_;
    std::int64_t last_activity_ms_ = 0;
    std::int64_t last_idle_duration_ms_ = 0;
    IdleState state_ = IdleState::Active;
    bool seen_activity_ = false;
};

}  // namespace snapback
