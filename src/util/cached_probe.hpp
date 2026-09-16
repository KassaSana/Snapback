// A yes/no fact that is expensive to establish and slow to change, remembered for a while.
//
// The motivating case is `check_capture_permissions` on Linux, which asks whether xdotool is
// installed by running `command -v xdotool` through std::system — a fork plus an exec of
// /bin/sh on the command worker thread. Three callers reach it (the 5-second health poll,
// recording_status, refresh_permissions), so the app was spawning a shell several times a
// minute to re-learn something that changes roughly once per install. The hot-path benchmark
// found it the hard way: 250,000 iterations became 250,000 processes.
//
// Reads go through `Clock::steady_ms()` rather than the real clock so the TTL is tested with
// a ManualClock at the scale it runs at (ROADMAP 11.4), not with sleeps.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

#include "util/clock.hpp"

namespace snapback {

class CachedProbe {
public:
    CachedProbe(std::function<bool()> probe, std::int64_t ttl_ms)
        : probe_(std::move(probe)), ttl_ms_(ttl_ms) {}

    // The cached answer if it is younger than the TTL, otherwise a fresh probe. The probe
    // runs under the lock on purpose: two pollers arriving together should cost one fork,
    // not two, and the probe is the expensive part this class exists to amortise.
    bool value(const Clock& clock) {
        std::lock_guard lock(mutex_);
        const auto now = clock.steady_ms();
        if (!cached_ || now - probed_at_ms_ >= ttl_ms_) {
            cached_ = probe_();
            probed_at_ms_ = now;
        }
        return *cached_;
    }

    // Forget the answer so the next `value()` re-probes. For user-initiated checks, where a
    // stale "not installed" after the user just installed the tool would be a lie.
    void invalidate() {
        std::lock_guard lock(mutex_);
        cached_.reset();
    }

private:
    std::mutex mutex_;
    std::function<bool()> probe_;
    std::int64_t ttl_ms_;
    std::optional<bool> cached_;
    std::int64_t probed_at_ms_{};
};

}  // namespace snapback
