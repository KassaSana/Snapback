// Pomodoro / focus-timer state machine. Roadmap 2.6.
//
// Pure logic, same discipline as IdleDetector: you feed it monotonic millisecond
// timestamps and it auto-advances Work -> break -> Work, telling you when a phase flips
// so the UI can chime and switch colors. No clock hidden inside, so tests drive it with a
// fake timeline. The engine/UI owns the wiring and the "chime" side effect.
#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

namespace snapback {

enum class PomodoroPhase { Work, ShortBreak, LongBreak };

inline const char* pomodoro_phase_as_str(PomodoroPhase phase) noexcept {
    switch (phase) {
        case PomodoroPhase::Work: return "work";
        case PomodoroPhase::ShortBreak: return "shortBreak";
        case PomodoroPhase::LongBreak: return "longBreak";
    }
    return "work";
}

struct PomodoroConfig {
    std::int64_t work_ms = 25 * 60 * 1000;         // classic 25/5/15
    std::int64_t short_break_ms = 5 * 60 * 1000;
    std::int64_t long_break_ms = 15 * 60 * 1000;
    int intervals_before_long_break = 4;           // long break after every 4th work block
};

struct PomodoroStatus {
    bool running = false;
    PomodoroPhase phase = PomodoroPhase::Work;
    int completed_work_intervals = 0;
    std::int64_t remaining_ms = 0;
};

void to_json(nlohmann::json& json, const PomodoroStatus& status);

class PomodoroTimer {
public:
    explicit PomodoroTimer(PomodoroConfig config = {}) : config_(config) {}

    // Begin a fresh Work phase at `now_ms`. Resets the completed-interval count.
    void start(std::int64_t now_ms) {
        running_ = true;
        phase_ = PomodoroPhase::Work;
        completed_work_intervals_ = 0;
        phase_start_ms_ = now_ms;
        phase_end_ms_ = now_ms + duration_of(PomodoroPhase::Work);
    }

    void stop() { running_ = false; }

    // Clear all progress at a focus-session boundary. This is distinct from stop(),
    // which intentionally leaves the current phase/count available for the UI.
    void reset() {
        running_ = false;
        phase_ = PomodoroPhase::Work;
        completed_work_intervals_ = 0;
        phase_start_ms_ = 0;
        phase_end_ms_ = 0;
    }

    // Advance time. Returns true if at least one phase boundary was crossed (the caller
    // chimes / repaints on true). Loops so a large time jump can't skip phases.
    bool poll(std::int64_t now_ms) {
        if (!running_) return false;
        bool changed = false;
        while (now_ms >= phase_end_ms_) {
            advance_from_end();
            changed = true;
        }
        return changed;
    }

    [[nodiscard]] PomodoroPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] int completed_work_intervals() const noexcept {
        return completed_work_intervals_;
    }

    // Time left in the current phase as of `now_ms` (0 when elapsed / not running).
    [[nodiscard]] std::int64_t remaining_ms(std::int64_t now_ms) const noexcept {
        if (!running_) return 0;
        const std::int64_t left = phase_end_ms_ - now_ms;
        return left > 0 ? left : 0;
    }

    [[nodiscard]] PomodoroStatus status(std::int64_t now_ms) const noexcept {
        return PomodoroStatus{
            running_, phase_, completed_work_intervals_, remaining_ms(now_ms)};
    }

private:
    std::int64_t duration_of(PomodoroPhase p) const noexcept {
        switch (p) {
            case PomodoroPhase::Work: return config_.work_ms;
            case PomodoroPhase::ShortBreak: return config_.short_break_ms;
            case PomodoroPhase::LongBreak: return config_.long_break_ms;
        }
        return config_.work_ms;
    }

    // Transition off the phase that just ended. Chain phase_start from the old end (not
    // `now`) so timing never drifts even if poll is called late.
    void advance_from_end() {
        PomodoroPhase next;
        if (phase_ == PomodoroPhase::Work) {
            ++completed_work_intervals_;
            const bool long_due = config_.intervals_before_long_break > 0 &&
                                  completed_work_intervals_ %
                                          config_.intervals_before_long_break ==
                                      0;
            next = long_due ? PomodoroPhase::LongBreak : PomodoroPhase::ShortBreak;
        } else {
            next = PomodoroPhase::Work;
        }
        phase_start_ms_ = phase_end_ms_;
        phase_end_ms_ = phase_start_ms_ + duration_of(next);
        phase_ = next;
    }

    PomodoroConfig config_;
    bool running_ = false;
    PomodoroPhase phase_ = PomodoroPhase::Work;
    int completed_work_intervals_ = 0;
    std::int64_t phase_start_ms_ = 0;
    std::int64_t phase_end_ms_ = 0;
};

}  // namespace snapback
