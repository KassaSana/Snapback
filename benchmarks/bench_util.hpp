// Shared timing + summary helpers for the Snapback benchmarks (zero-dependency,
// <chrono>-based — Google Benchmark isn't vendored and we want a single portable binary).
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace snapback::bench {

using Clock = std::chrono::steady_clock;

struct Timer {
    Clock::time_point start{Clock::now()};
    double elapsed_ns() const {
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    }
    double elapsed_us() const {
        return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    }
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
};

struct Stats {
    double total_ms{};
    double mean_us{};
    double p50_us{};
    double p95_us{};
    double p99_us{};
    double max_us{};
};

// `samples_us` are per-op latencies in microseconds; `total_ms` is the wall time of the
// whole loop (used for ops/sec, which excludes the per-sample timer overhead).
inline Stats summarize(std::vector<double> samples_us, double total_ms) {
    Stats stats;
    stats.total_ms = total_ms;
    if (samples_us.empty()) return stats;
    std::sort(samples_us.begin(), samples_us.end());
    const double n = static_cast<double>(samples_us.size());
    const auto pct = [&](double p) {
        const auto idx = static_cast<std::size_t>(std::min(n - 1.0, std::ceil((p / 100.0) * n) - 1.0));
        return samples_us[idx];
    };
    stats.mean_us = std::accumulate(samples_us.begin(), samples_us.end(), 0.0) / n;
    stats.p50_us = pct(50.0);
    stats.p95_us = pct(95.0);
    stats.p99_us = pct(99.0);
    stats.max_us = samples_us.back();
    return stats;
}

// Prints per-op latency in microseconds (default) or nanoseconds (for sub-µs hot paths).
inline void print_stats(const std::string& name, std::size_t operations, const Stats& stats,
                        bool nanoseconds = false) {
    const double ops_per_sec =
        stats.total_ms > 0.0 ? static_cast<double>(operations) / (stats.total_ms / 1000.0) : 0.0;
    const double scale = nanoseconds ? 1000.0 : 1.0;
    const char* unit = nanoseconds ? "ns" : "us";
    std::cout << std::left << std::setw(30) << name << std::right
              << " ops=" << std::setw(8) << operations
              << " total_ms=" << std::setw(9) << std::fixed << std::setprecision(2) << stats.total_ms
              << " ops/s=" << std::setw(11) << std::setprecision(0) << ops_per_sec
              << std::setprecision(2)
              << "  mean=" << std::setw(8) << stats.mean_us * scale << unit
              << " p50=" << std::setw(8) << stats.p50_us * scale << unit
              << " p95=" << std::setw(8) << stats.p95_us * scale << unit
              << " p99=" << std::setw(8) << stats.p99_us * scale << unit
              << " max=" << std::setw(9) << stats.max_us * scale << unit << '\n';
}

}  // namespace snapback::bench
