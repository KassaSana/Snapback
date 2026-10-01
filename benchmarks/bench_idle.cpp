// Quiet-engine and paced-input measurements using a real AppState and storage.
// The fake capture producer sleeps every 5 ms; process CPU includes that harness cost.
// OS hooks, GUI work, and shipped desktop CPU are outside this measurement.
//
// Build: -DSNAPBACK_BUILD_BENCHMARKS=ON, target `snapback_idle_benchmarks`. Env:
//   SNAPBACK_IDLE_SECONDS  how long to measure (default 10)
//   SNAPBACK_IDLE_INPUT_MS optional paced input interval (absent = silent)

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
#include <vector>

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
    explicit SilentHook(std::size_t interval_ms = 0) : interval_ms_(interval_ms) {}
    std::int64_t last_input_us() const { return last_input_us_.load(std::memory_order_acquire); }
    void run(InputCallback emit, const std::atomic<bool>& stop_requested) override {
        auto next = std::chrono::steady_clock::now();
        while (running_.load(std::memory_order_relaxed) &&
               !stop_requested.load(std::memory_order_relaxed)) {
            const auto now = std::chrono::steady_clock::now();
            if (interval_ms_ > 0 && now >= next) {
                const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                last_input_us_.store(stamp, std::memory_order_release);
                CaptureEvent event;
                event.event_type = EventType::KeyPress;
                event.timestamp_secs = static_cast<double>(stamp) / 1e6;
                event.wall_clock_secs = wall_clock_secs_now();
                event.app_name = "Editor";
                event.window_title = "benchmark";
                emit(std::move(event));
                next = now + std::chrono::milliseconds(interval_ms_);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void stop() noexcept override { running_.store(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> running_{true};
    std::size_t interval_ms_;
    std::atomic<std::int64_t> last_input_us_{0};
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
                  << "  fixed_poll_interval_ms=" << 0 /* capture/deadline driven */ << "\n\n";

        // Heap-allocated: AppState embeds the 64K-slot capture ring (~5 MB), which is more
        // than a default thread stack wants to hold.
        auto state = std::make_unique<AppState>(std::move(*storage));
        const auto input_ms = std::getenv("SNAPBACK_IDLE_INPUT_MS") ? env_size("SNAPBACK_IDLE_INPUT_MS", 1100) : 0;
        SilentHook hook(input_ms);
        std::vector<double> latency_us;
        state->set_emit_hook([&](const char* name, const std::string&, AppState::ActivityEpoch) {
            if (std::string(name) == "prediction" && hook.last_input_us() > 0) {
                const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                latency_us.push_back(static_cast<double>(now - hook.last_input_us()));
            }
        });

        // Reset after construction and the first storage work, so the figures describe the
        // idle window rather than startup's migrations and retention prune.
        reset_lock_metrics();
        const auto cpu_before_ms = process_cpu_ms();
        const Timer wall;
        state->start_engine_for_test(&hook);
        std::this_thread::sleep_for(std::chrono::seconds(idle_seconds));
        const auto wakeups = state->engine_wakeups();
        state->stop_engine();
        state->set_emit_hook(nullptr);
        const double wall_ms = wall.elapsed_ms();
        const auto cpu_ms = process_cpu_ms() - cpu_before_ms;

        const double wall_secs = wall_ms / 1000.0;
        if (!latency_us.empty()) print_stats("Capture to prediction emit", latency_us.size(), summarize(latency_us, wall_ms));
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
