// What Snapback costs while it is doing nothing. Roadmap 14.11.
//
// The item asks for "idle CPU and wakeups per second with no session", and those two have to
// be read together or neither means anything. The wakeup count on its own is close to a
// constant by construction: `kEngineTickIntervalMs` is 100, so the tick loop wakes ten times
// a second whether or not a session exists, whether or not input is arriving, and whether or
// not the last tick had anything to do. **That is the finding, not a defect this file fixes.**
// What it costs to wake that often is the number nobody had.
//
// **What this measures.** A real `AppState` with a real engine thread, a real storage
// connection, and the capture thread running -- started, left alone with no session for a
// while, then stopped. Every number printed is of the same process over the same interval.
//
// **What it deliberately leaves out**, and why the figure is a floor rather than the whole
// answer: the input hook is a silent fake, so none of the OS-level cost of a real
// `WH_KEYBOARD_LL` hook and its message pump is here. That part cannot be measured without
// the platform hook installed, which means a running app; 14.11 says so. A number from this
// file is "what the engine costs when idle", not "what the product costs when idle", and the
// two must not be quoted as though they were the same.
//
// There is no pass/fail threshold and no CI job, for the reason `bench_budgets.cpp` gives at
// length: a hosted runner is too noisy to gate on, and a disabled gate lies by omission.
//
// Build: -DSNAPBACK_BUILD_BENCHMARKS=ON, target `snapback_idle_benchmarks`.
// Env:
//   SNAPBACK_IDLE_SECONDS  how long to sit idle before reporting (default 10)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include "bench_util.hpp"

#include "app/state.hpp"
#include "capture/capture_thread.hpp"
#include "capture/input_hook.hpp"
#include "storage/storage.hpp"
#include "util/process_cpu.hpp"
#include "util/ranked_mutex.hpp"

using namespace snapback;
using namespace snapback::bench;

namespace {

// A hook that installs nothing and emits nothing. The real one blocks in an OS message loop;
// this blocks in a sleep, which is the same shape of thread from the engine's point of view
// and none of the same cost -- see the header note about what that excludes.
class SilentHook final : public InputHook {
public:
    void run(InputCallback, const std::atomic<bool>& stop_requested) override {
        while (running_.load(std::memory_order_relaxed) &&
               !stop_requested.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void stop() noexcept override { running_.store(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> running_{true};
};

std::size_t env_size(const char* name, std::size_t fallback) {
    if (const char* raw = std::getenv(name)) {
        const auto value = std::strtoull(raw, nullptr, 10);
        if (value > 0) return static_cast<std::size_t>(value);
    }
    return fallback;
}

void print_lock_rank(const char* label, LockRank rank) {
    const auto metrics = lock_metrics(rank);
    std::cout << "  " << std::left << std::setw(20) << label << std::right
              << " acquisitions=" << std::setw(8) << metrics.acquisitions
              << "  contended=" << std::setw(6) << metrics.contended
              << "  hold p50<=" << std::setw(7) << metrics.hold_p50_us << "us"
              << "  p95<=" << std::setw(7) << metrics.hold_p95_us << "us"
              << "  max=" << std::setw(8) << metrics.max_hold_us << "us"
              << "  wait p95<=" << std::setw(7) << metrics.wait_p95_us << "us"
              << "  max=" << std::setw(8) << metrics.max_wait_us << "us\n";
}

}  // namespace

int main() {
    try {
        const auto idle_seconds = env_size("SNAPBACK_IDLE_SECONDS", 10);

        const auto dir = std::filesystem::temp_directory_path() /
                         ("snapback_idle_" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);

        auto storage = Storage::open(dir);
        if (!storage) throw std::runtime_error("failed to open the benchmark database");

        std::cout << "Snapback idle cost (Roadmap 14.11)\n"
                  << "Engine + capture thread running, no session, fake input hook.\n"
                  << "The real OS hook's cost is NOT here -- see the header.\n\n"
                  << "  idle_seconds=" << idle_seconds
                  << "  engine_tick_interval_ms=" << kEngineTickIntervalMs << "\n\n";

        // Heap-allocated: AppState embeds the 64K-slot capture ring (~5 MB), which is more
        // than a default thread stack wants to hold (Roadmap 6.1).
        auto state = std::make_unique<AppState>(std::move(*storage));
        SilentHook hook;

        // Reset after construction and the first storage work, so the figures describe the
        // idle window rather than startup's migrations and retention prune.
        reset_lock_metrics();
        const auto cpu_before_ms = process_cpu_ms();
        const Timer wall;
        state->start_engine_for_test(&hook);
        std::this_thread::sleep_for(std::chrono::seconds(idle_seconds));
        const auto wakeups = state->engine_wakeups();
        state->stop_engine();
        const double wall_ms = wall.elapsed_ms();
        const auto cpu_ms = process_cpu_ms() - cpu_before_ms;

        const double wall_secs = wall_ms / 1000.0;
        std::cout << std::fixed << std::setprecision(2) << "Idle\n"
                  << "  wall                " << wall_secs << " s\n"
                  << "  cpu                 " << static_cast<double>(cpu_ms) << " ms ("
                  << (wall_secs > 0.0 ? static_cast<double>(cpu_ms) / wall_secs : 0.0)
                  << " ms per wall second, "
                  << (wall_secs > 0.0
                          ? static_cast<double>(cpu_ms) / (wall_secs * 1000.0) * 100.0
                          : 0.0)
                  << "% of one core)\n"
                  << "  engine wakeups      " << wakeups << " ("
                  << (wall_secs > 0.0 ? static_cast<double>(wakeups) / wall_secs : 0.0)
                  << " per second)\n"
                  << "  cpu per wakeup      "
                  << (wakeups > 0 ? static_cast<double>(cpu_ms) * 1000.0 /
                                        static_cast<double>(wakeups)
                                  : 0.0)
                  << " us\n"
                  << "  ring high-water     " << state->capture_ring_high_water()
                  << " of " << CaptureThread::kCapacity << " slots\n"
                  << "  capture drops       " << state->health().capture_events_dropped << '\n';
        // GetProcessTimes counts in scheduler ticks (~15.6 ms on Windows), so a
        // process that burned less than one tick over the whole window reports zero.
        // That is a real result -- the idle cost is below what the OS can resolve --
        // but it is not "free", and a bare 0.00 with nothing beside it invites that
        // reading.
        if (cpu_ms == 0) {
            std::cout << "  (cpu is under the platform timer's granularity over this"
                      << " window; raise SNAPBACK_IDLE_SECONDS to resolve it)\n";
        }
        std::cout << '\n';

        std::cout << "Locks over the same window\n"
                  << "  (p50/p95 are histogram bucket upper bounds, max is exact, so"
                  << " a p95 above max is the bound and not a contradiction)\n";
        print_lock_rank("State", LockRank::State);
        print_lock_rank("ActivityBoundary", LockRank::ActivityBoundary);
        print_lock_rank("Storage", LockRank::Storage);

        const auto busy = state->storage_busy_stats();
        std::cout << "\nSQLite busy handler\n"
                  << "  waits               " << busy.waits << '\n'
                  << "  exhausted           " << busy.exhausted << '\n'
                  << "  longest wait        " << busy.max_wait_ms << " ms\n";

        state.reset();
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bench_idle failed: " << error.what() << '\n';
        return 1;
    }
}
