// A yes/no fact that is expensive to establish and slow to change, cached for a TTL (e.g. "is
// xdotool installed", which costs a shell spawn). Uses Clock::steady_ms() so tests drive the
// TTL with a ManualClock.
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
