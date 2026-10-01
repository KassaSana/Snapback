// Coalesced producer notifications and clock-independent deadline selection.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <semaphore>
#if defined(_WIN32) && defined(__GLIBCXX__)
#include <windows.h>
#include <stdexcept>
#endif

namespace snapback {
class EngineWakeSignal {
public:
#if defined(_WIN32) && defined(__GLIBCXX__)
    EngineWakeSignal() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        if (!event_) throw std::runtime_error("could not create engine wake event");
    }
    ~EngineWakeSignal() { CloseHandle(event_); }
#endif
    void notify() noexcept {
        if (!pending_.exchange(true, std::memory_order_acq_rel)) {
            permit_.release();
#if defined(_WIN32) && defined(__GLIBCXX__)
            SetEvent(event_);
#endif
        }
    }
    bool wait(std::optional<std::int64_t> delay_ms) {
        bool acquired = true;
#if defined(_WIN32) && defined(__GLIBCXX__)
        // MinGW libstdc++ 16 timed atomic waits spin on this host. The semaphore
        // remains the coalesced token; the OS event supplies a blocking timeout.
        acquired = permit_.try_acquire();
        if (acquired) ResetEvent(event_);
        else {
            const auto timeout = delay_ms
                ? static_cast<DWORD>(std::min<std::int64_t>(*delay_ms, MAXDWORD - 1))
                : INFINITE;
            WaitForSingleObject(event_, timeout);
            acquired = permit_.try_acquire();
        }
#else
        if (delay_ms) acquired = permit_.try_acquire_for(std::chrono::milliseconds(*delay_ms));
        else permit_.acquire();
#endif
        // Clear before the next authoritative ring/state check. A notification coalesced
        // during acquire is covered by that check; one after clear leaves a new permit.
        // Acquire the latest coalesced notification, including relaxed stop flags.
        if (acquired) pending_.exchange(false, std::memory_order_acq_rel);
        return acquired;
    }
private:
#if defined(_WIN32) && defined(__GLIBCXX__)
    HANDLE event_;
#endif
    std::binary_semaphore permit_{0};
    std::atomic<bool> pending_{false};
};
struct WakeOnExit {
    EngineWakeSignal& signal;
    ~WakeOnExit() { signal.notify(); }
};
class EngineDeadlines {
public:
    EngineDeadlines(std::int64_t steady_ms, std::int64_t wall_ms)
        : steady_(steady_ms), wall_(wall_ms) {}
    void monotonic(std::int64_t deadline) { include(deadline - steady_); }
    void wall(std::int64_t deadline) {
        include(std::min<std::int64_t>(30000, deadline - wall_));
    }
    std::optional<std::int64_t> delay_ms() const { return delay_; }
private:
    void include(std::int64_t delay) {
        delay = std::max<std::int64_t>(0, delay);
        if (!delay_ || delay < *delay_) delay_ = delay;
    }
    std::int64_t steady_, wall_;
    std::optional<std::int64_t> delay_;
};
}  // namespace snapback
