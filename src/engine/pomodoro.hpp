// Pomodoro / focus-timer state machine. Roadmap 2.6.
//
// Pure logic, same discipline as IdleDetector: you feed it monotonic millisecond
// timestamps and it auto-advances Work -> break -> Work, telling you when a phase flips
// so the UI can chime and switch colors. No clock hidden inside, so tests drive it with a
// fake timeline. The engine/UI owns the wiring and the "chime" side effect.
#pragma once

#include <cstdint>
#include <string_view>

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
    // Roadmap 2.13 asked for this to be decided rather than left implicit, so: **on by
    // default**, because that is what the timer has always done and a silent change of rhythm
    // for existing users would be the worse answer. Turned off, a phase that reaches its end
    // stops there and waits — `poll()` still reports the boundary once so the UI can alert,
    // but nothing begins until `acknowledge()`. That is the setting for people who want the
    // break to start when they actually stand up, not when the clock says so.
    bool auto_start_next_phase = true;
};

// What the timer looks like right now. `paused` and `awaiting_acknowledgement` are reported
// separately from `running` because they are different states with the same countdown reading:
// paused is the user's choice and resumes where it stopped, awaiting means a phase ended and
// the next one is deliberately waiting to be started.
struct PomodoroStatus {
    bool running = false;
    bool paused = false;
    bool awaiting_acknowledgement = false;
    PomodoroPhase phase = PomodoroPhase::Work;
    int completed_work_intervals = 0;
    std::int64_t remaining_ms = 0;
};

// The timer as it survives a relaunch. Roadmap 2.13.
//
// The deadline is **wall clock**, not the monotonic timeline the timer runs on: a monotonic
// clock restarts at process launch, so a monotonic deadline written before a restart means
// nothing after it. Wall clock is the only representation that still refers to the same
// instant in the next process.
struct PomodoroSnapshot {
    bool running = false;
    bool paused = false;
    bool awaiting_acknowledgement = false;
    PomodoroPhase phase = PomodoroPhase::Work;
    int completed_work_intervals = 0;
    // Unix ms at which the current phase ends. Meaningless while paused or awaiting.
    std::int64_t deadline_wall_ms = 0;
    // What was left when the user paused, so resuming does not depend on any clock having
    // kept running in between.
    std::int64_t paused_remaining_ms = 0;
};

// Parses what pomodoro_phase_as_str wrote. Unknown text becomes Work rather than throwing:
// this reads a settings file a user can hand-edit, and refusing to start over a typo in a
// timer phase would be a worse answer than beginning the cycle at the start.
inline PomodoroPhase pomodoro_phase_from_string(std::string_view text) noexcept {
    if (text == "shortBreak") return PomodoroPhase::ShortBreak;
    if (text == "longBreak") return PomodoroPhase::LongBreak;
    return PomodoroPhase::Work;
}

void to_json(nlohmann::json& json, const PomodoroStatus& status);
void to_json(nlohmann::json& json, const PomodoroConfig& config);
void from_json(const nlohmann::json& json, PomodoroConfig& config);
void to_json(nlohmann::json& json, const PomodoroSnapshot& snapshot);
void from_json(const nlohmann::json& json, PomodoroSnapshot& snapshot);

class PomodoroTimer {
public:
    explicit PomodoroTimer(PomodoroConfig config = {}) : config_(config) {}

    // Begin a fresh Work phase at `now_ms`. Resets the completed-interval count.
    void start(std::int64_t now_ms) {
        running_ = true;
        completed_work_intervals_ = 0;
        begin_phase(PomodoroPhase::Work, now_ms);
    }

    void stop() {
        running_ = false;
        paused_ = false;
        awaiting_ = false;
    }

    // Freeze the countdown where it is. Idempotent, and a no-op unless a phase is actually
    // running: pausing an already-ended phase that is waiting for acknowledgement would
    // otherwise leave two "stopped" states to tell apart on resume.
    void pause(std::int64_t now_ms) {
        if (!running_ || paused_ || awaiting_) return;
        paused_remaining_ms_ = remaining_ms(now_ms);
        paused_ = true;
    }

    // Continue with exactly the time that was left. The deadline is re-anchored to `now`
    // rather than kept, so a pause costs the phase nothing however long it lasted.
    void resume(std::int64_t now_ms) {
        if (!running_ || !paused_) return;
        paused_ = false;
        phase_start_ms_ = now_ms;
        phase_end_ms_ = now_ms + paused_remaining_ms_;
        paused_remaining_ms_ = 0;
    }

    // End the current phase now and move to the next one.
    //
    // A skipped Work phase is **not** credited as a completed interval: the long-break cadence
    // is a reward for work actually done, and counting skips would let someone reach a long
    // break without working. Skipping a break simply gets back to work early.
    void skip(std::int64_t now_ms) {
        if (!running_) return;
        const bool was_work = phase_ == PomodoroPhase::Work;
        begin_phase(was_work ? next_break_phase(completed_work_intervals_)
                             : PomodoroPhase::Work,
                    now_ms);
    }

    // Start the current phase over from its full duration, keeping the completed-interval
    // count. Distinct from start(), which begins the whole cycle again from zero.
    void restart_phase(std::int64_t now_ms) {
        if (!running_) return;
        begin_phase(phase_, now_ms);
    }

    // Begin the phase that has been waiting since the last boundary. No-op unless the timer
    // is actually awaiting one, so a stray click cannot skip a phase.
    void acknowledge(std::int64_t now_ms) {
        if (!running_ || !awaiting_) return;
        awaiting_ = false;
        begin_phase(pending_phase_, now_ms);
    }

    // Clear all progress at a focus-session boundary. This is distinct from stop(),
    // which intentionally leaves the current phase/count available for the UI.
    void reset() {
        running_ = false;
        paused_ = false;
        awaiting_ = false;
        phase_ = PomodoroPhase::Work;
        completed_work_intervals_ = 0;
        phase_start_ms_ = 0;
        phase_end_ms_ = 0;
        paused_remaining_ms_ = 0;
    }

    // Advance time. Returns true if at least one phase boundary was crossed (the caller
    // chimes / repaints on true). Loops so a large time jump can't skip phases.
    //
    // A paused timer does not advance, and one already waiting for acknowledgement does not
    // report the same boundary twice — otherwise a UI that alerts on `true` would alert on
    // every tick until the user came back.
    bool poll(std::int64_t now_ms) {
        if (!running_ || paused_ || awaiting_) return false;
        bool changed = false;
        while (now_ms >= phase_end_ms_) {
            advance_from_end();
            changed = true;
            if (awaiting_) break;
        }
        return changed;
    }

    // Roadmap 2.13. The timer as it should be written down, using a wall clock the next
    // process can still make sense of.
    [[nodiscard]] PomodoroSnapshot snapshot(std::int64_t now_wall_ms,
                                            std::int64_t now_steady_ms) const noexcept {
        PomodoroSnapshot snap;
        snap.running = running_;
        snap.paused = paused_;
        snap.awaiting_acknowledgement = awaiting_;
        snap.phase = awaiting_ ? pending_phase_ : phase_;
        snap.completed_work_intervals = completed_work_intervals_;
        snap.paused_remaining_ms = paused_remaining_ms_;
        // Convert the monotonic deadline into the same instant on the wall clock.
        snap.deadline_wall_ms = now_wall_ms + (phase_end_ms_ - now_steady_ms);
        return snap;
    }

    // Restore a snapshot taken by a previous run. Roadmap 2.13's relaunch policy, in full:
    //
    //   * a deadline still in the future resumes, with the time that is genuinely left;
    //   * a deadline that passed while the app was gone restores **at zero, awaiting
    //     acknowledgement** — never advanced, never chained through the phases that would
    //     have elapsed. The app was not there to alert anybody, so claiming those phases
    //     happened would credit work intervals nobody worked and hand out a long break for
    //     an afternoon the app spent closed. This holds whether or not auto-start is on:
    //     auto-start is about a boundary the user was present for.
    //   * a paused timer restores paused, with its remaining time intact.
    void restore(const PomodoroSnapshot& snap, std::int64_t now_wall_ms,
                 std::int64_t now_steady_ms) {
        reset();
        if (!snap.running) return;
        running_ = true;
        phase_ = snap.phase;
        completed_work_intervals_ = snap.completed_work_intervals;

        if (snap.paused) {
            paused_ = true;
            paused_remaining_ms_ = snap.paused_remaining_ms;
            phase_start_ms_ = now_steady_ms;
            phase_end_ms_ = now_steady_ms + paused_remaining_ms_;
            return;
        }
        if (snap.awaiting_acknowledgement) {
            awaiting_ = true;
            pending_phase_ = snap.phase;
            phase_end_ms_ = now_steady_ms;
            return;
        }

        const std::int64_t left = snap.deadline_wall_ms - now_wall_ms;
        if (left > 0) {
            phase_start_ms_ = now_steady_ms;
            phase_end_ms_ = now_steady_ms + left;
            return;
        }
        // Elapsed while the app was not running.
        awaiting_ = true;
        pending_phase_ = next_phase_after(phase_, completed_work_intervals_);
        phase_end_ms_ = now_steady_ms;
    }

    // The phase that acknowledgement will begin. Meaningful only while awaiting.
    // Change the rhythm without disturbing the phase already under way: the new lengths apply
    // to the phases that follow. Restarting the countdown someone is currently inside is not
    // what editing a preference should do.
    void set_config(const PomodoroConfig& config) noexcept { config_ = config; }
    [[nodiscard]] const PomodoroConfig& config() const noexcept { return config_; }

    [[nodiscard]] PomodoroPhase pending_phase() const noexcept { return pending_phase_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] bool awaiting_acknowledgement() const noexcept { return awaiting_; }

    [[nodiscard]] PomodoroPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] int completed_work_intervals() const noexcept {
        return completed_work_intervals_;
    }

    // Time left in the current phase as of `now_ms` (0 when elapsed / not running).
    [[nodiscard]] std::int64_t remaining_ms(std::int64_t now_ms) const noexcept {
        if (!running_) return 0;
        // A paused phase reads its frozen remainder rather than a deadline that is still
        // marching away from `now`.
        if (paused_) return paused_remaining_ms_;
        if (awaiting_) return 0;
        const std::int64_t left = phase_end_ms_ - now_ms;
        return left > 0 ? left : 0;
    }

    [[nodiscard]] PomodoroStatus status(std::int64_t now_ms) const noexcept {
        return PomodoroStatus{running_,
                              paused_,
                              awaiting_,
                              awaiting_ ? pending_phase_ : phase_,
                              completed_work_intervals_,
                              remaining_ms(now_ms)};
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

    // Which break follows a work block, given how many are already done. Pure, so both the
    // natural boundary and skip()/restore() agree on the cadence.
    [[nodiscard]] PomodoroPhase next_break_phase(int completed) const noexcept {
        const bool long_due = config_.intervals_before_long_break > 0 &&
                              completed > 0 &&
                              completed % config_.intervals_before_long_break == 0;
        return long_due ? PomodoroPhase::LongBreak : PomodoroPhase::ShortBreak;
    }

    // What comes after `phase`, assuming it ran to completion. Used by restore() to name the
    // phase that is waiting, without actually entering it.
    [[nodiscard]] PomodoroPhase next_phase_after(PomodoroPhase phase,
                                                 int completed) const noexcept {
        return phase == PomodoroPhase::Work ? next_break_phase(completed + 1)
                                            : PomodoroPhase::Work;
    }

    // Enter `next` starting at `now_ms`, at its full duration.
    void begin_phase(PomodoroPhase next, std::int64_t now_ms) {
        phase_ = next;
        paused_ = false;
        awaiting_ = false;
        paused_remaining_ms_ = 0;
        phase_start_ms_ = now_ms;
        phase_end_ms_ = now_ms + duration_of(next);
    }

    // Transition off the phase that just ended. Chain phase_start from the old end (not
    // `now`) so timing never drifts even if poll is called late.
    void advance_from_end() {
        PomodoroPhase next;
        if (phase_ == PomodoroPhase::Work) {
            ++completed_work_intervals_;
            next = next_break_phase(completed_work_intervals_);
        } else {
            next = PomodoroPhase::Work;
        }
        // With auto-start off the boundary is still crossed and still reported — the next
        // phase simply does not begin until the user says so.
        // The countdown stops at the deadline it just reached: remaining_ms() reports 0 while
        // awaiting, so phase_end_ms_ is deliberately left where it is.
        if (!config_.auto_start_next_phase) {
            awaiting_ = true;
            pending_phase_ = next;
            return;
        }
        phase_start_ms_ = phase_end_ms_;
        phase_end_ms_ = phase_start_ms_ + duration_of(next);
        phase_ = next;
    }

    PomodoroConfig config_;
    bool running_ = false;
    bool paused_ = false;
    bool awaiting_ = false;
    PomodoroPhase phase_ = PomodoroPhase::Work;
    PomodoroPhase pending_phase_ = PomodoroPhase::Work;
    int completed_work_intervals_ = 0;
    std::int64_t phase_start_ms_ = 0;
    std::int64_t phase_end_ms_ = 0;
    std::int64_t paused_remaining_ms_ = 0;
};

}  // namespace snapback
