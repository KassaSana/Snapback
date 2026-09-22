// Measured budgets for the Review surfaces and the storage layer. Roadmap 14.11.
//
// **Why a third benchmark instead of a flag on the existing two.** `bench_snapback.cpp` and
// `bench_hotpaths.cpp` both run `Storage::open_memory()` with a fixed timestamp, and
// `bench_snapback` writes a prediction on every other capture event. That answers "how fast is
// the code" and it is the right question for those files. It cannot answer the question 14.1,
// 14.5, 14.7 and 9.10 all open with -- *how much does a real install actually hold, and what
// does reading it cost* -- because the two things that make the answer are exactly what those
// harnesses leave out: the on-disk WAL, and the real write cadence.
//
// The cadence is the fact everything else follows from. `AppState::compute_event` persists at
// most one prediction per second, and only while a session is attended, not idle, and
// receiving input; every persisted prediction carries exactly one `feature_snapshots` row. So
// the per-attended-hour ceiling is 3,600 + 3,600 rows, and the realistic figure is lower by
// however much of the hour had no input. This file generates a database at that cadence,
// across the full 90-day retention window, on disk, and then reads it the way the app does.
//
// **What it deliberately does not do.** There is no pass/fail threshold here and no CI job.
// Hosted runners are too noisy to carry a performance gate; a flaky gate gets disabled within
// a month and then lies by omission for as long as it stays disabled. This prints numbers for
// a human to paste into `docs/testing_strategy.md` with the host named. A number without a
// host named is not a measurement.
//
// Build: -DSNAPBACK_BUILD_BENCHMARKS=ON, target `snapback_budget_benchmarks`.
// Env:
//   SNAPBACK_BUDGET_DAYS           days of history to generate      (default 90 = retention)
//   SNAPBACK_BUDGET_ATTENDED_HOURS attended hours per day           (default 6)
//   SNAPBACK_BUDGET_DUTY_PCT       percent of attended seconds that saw input (default 100)
//   SNAPBACK_BUDGET_QUERY_REPS     timed repetitions per query      (default 25)
//   SNAPBACK_BUDGET_KEEP           1 to leave the generated database on disk
//
// The default is the **ceiling**: every attended second writes. Run it again with
// SNAPBACK_BUDGET_DUTY_PCT=60 for a figure closer to a real day. Publishing both is the point
// -- the ceiling bounds the worst case and the duty-cycled run says what to expect.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "bench_util.hpp"

#include "storage/storage.hpp"
#include "types.hpp"

using namespace snapback;
using namespace snapback::bench;

namespace {

std::size_t env_size(const char* name, std::size_t fallback) {
    if (const char* raw = std::getenv(name)) {
        const auto value = std::strtoull(raw, nullptr, 10);
        if (value > 0) return static_cast<std::size_t>(value);
    }
    return fallback;
}

bool env_flag(const char* name) {
    const char* raw = std::getenv(name);
    return raw && raw[0] == '1';
}

struct Footprint {
    std::uintmax_t db_bytes{};
    std::uintmax_t wal_bytes{};
    std::uintmax_t total_bytes() const { return db_bytes + wal_bytes; }
};

Footprint measure_footprint(const std::filesystem::path& db_path) {
    Footprint out;
    std::error_code error;
    out.db_bytes = std::filesystem::file_size(db_path, error);
    if (error) out.db_bytes = 0;
    const auto wal = db_path.string() + "-wal";
    out.wal_bytes = std::filesystem::file_size(wal, error);
    if (error) out.wal_bytes = 0;
    return out;
}

std::string human_bytes(double bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 3) {
        bytes /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(bytes < 10.0 ? 2 : 1) << bytes << ' ' << units[unit];
    return out.str();
}

struct Generated {
    std::size_t predictions{};
    std::size_t feature_snapshots{};
    std::size_t sessions{};
    std::size_t attended_seconds{};
    double generate_ms{};
};

// One day of a plausible working install: a session per attended block, a prediction (and its
// feature row) for each attended second that saw input, and a focus score that moves so the
// gaps-and-islands run arithmetic has real islands to find rather than one unbroken block.
Generated generate(Storage& storage, std::size_t days, std::size_t attended_hours,
                   std::size_t duty_pct, std::int64_t now_ms) {
    Generated out;
    std::mt19937 rng(20260922);
    std::uniform_int_distribution<int> score(20, 99);
    std::uniform_int_distribution<int> roll(1, 100);

    constexpr std::int64_t kDayMs = 24 * 60 * 60 * 1000;
    FeatureVector features;  // one row's worth; the shape is what costs bytes, not the values

    Timer total;
    for (std::size_t day = 0; day < days; ++day) {
        // Oldest first, and strictly inside the retention window so `Storage::open`'s prune
        // does not delete the fixture out from under the measurement.
        const std::int64_t day_start = now_ms - static_cast<std::int64_t>(days - day) * kDayMs;

        Storage::Transaction txn(storage);
        // Two sessions a day, so session-history reads have something to page and the run
        // arithmetic has a session boundary to break on.
        for (int block = 0; block < 2; ++block) {
            const auto session =
                storage.create_session("Ship the measured budgets", FocusMode::Normal);
            ++out.sessions;
            const std::int64_t block_start =
                day_start + (block == 0 ? 9 : 14) * 60 * 60 * 1000;
            const std::size_t block_seconds = attended_hours * 3600 / 2;

            storage.begin_session_span(session.session_id, block_start);
            for (std::size_t second = 0; second < block_seconds; ++second) {
                ++out.attended_seconds;
                // "A second with no input writes nothing" -- the duty cycle is the whole
                // difference between the ceiling and a real day.
                if (duty_pct < 100 && static_cast<std::size_t>(roll(rng)) > duty_pct) continue;

                PredictionRecord record;
                record.session_id = session.session_id;
                record.focus_score = static_cast<double>(score(rng));
                record.distraction_risk = 1.0 - record.focus_score / 100.0;
                record.focus_state = record.focus_score < 40.0 ? "DISTRACTED" : "PRODUCTIVE";
                record.thrash_score = 0.1;
                record.drift_score = 0.2;
                record.goal_alignment = 0.7;
                record.timestamp_ms = block_start + static_cast<std::int64_t>(second) * 1000;
                record.model_id = "heuristic:bench";
                storage.insert_prediction(record);
                storage.insert_feature_snapshot(session.session_id, features);
                ++out.predictions;
                ++out.feature_snapshots;
            }
            storage.close_session_span(session.session_id,
                                       block_start +
                                           static_cast<std::int64_t>(block_seconds) * 1000);
            storage.end_session(session.session_id);
        }
        txn.commit();

        if ((day + 1) % 10 == 0) {
            std::cout << "  generated " << (day + 1) << '/' << days << " days ("
                      << out.predictions << " predictions)\n"
                      << std::flush;
        }
    }
    out.generate_ms = total.elapsed_ms();
    return out;
}

// Times one read the way the UI issues it: cold-ish, repeated, p50/p95 reported. The return
// value is consumed so the optimiser cannot delete the call.
std::uint64_t g_sink = 0;

template <typename Fn>
Stats time_query(std::size_t reps, Fn&& body) {
    std::vector<double> samples;
    samples.reserve(reps);
    Timer total;
    for (std::size_t i = 0; i < reps; ++i) {
        Timer one;
        body();
        samples.push_back(one.elapsed_ms() * 1000.0);
    }
    return summarize(std::move(samples), total.elapsed_ms());
}

}  // namespace

int main() {
    try {
        const auto days = env_size("SNAPBACK_BUDGET_DAYS", 90);
        const auto attended_hours = env_size("SNAPBACK_BUDGET_ATTENDED_HOURS", 6);
        const auto duty_pct = std::min<std::size_t>(100, env_size("SNAPBACK_BUDGET_DUTY_PCT", 100));
        const auto reps = env_size("SNAPBACK_BUDGET_QUERY_REPS", 25);

        const auto dir = std::filesystem::temp_directory_path() /
                         ("snapback_budget_" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);

        std::cout << "Snapback measured budgets (Roadmap 14.11)\n"
                  << "On-disk database, real persistence cadence: at most one prediction per\n"
                  << "attended second, one feature_snapshots row per prediction.\n\n"
                  << "  days=" << days << "  attended_hours/day=" << attended_hours
                  << "  duty=" << duty_pct << "%  query_reps=" << reps << '\n'
                  << "  database: " << dir.string() << "\n\n";

        const std::int64_t now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();

        Generated generated;
        Footprint footprint;
        std::filesystem::path db_path;
        {
            auto storage = Storage::open(dir);
            if (!storage) throw std::runtime_error("failed to open the benchmark database");
            db_path = storage->database_path();

            std::cout << "Generating...\n";
            generated = generate(*storage, days, attended_hours, duty_pct, now_ms);
            std::cout << "  done in " << std::fixed << std::setprecision(1)
                      << generated.generate_ms / 1000.0 << " s\n\n";

            footprint = measure_footprint(db_path);

            // --- Reads, against the database just written ------------------------------
            constexpr std::int64_t kDayMs = 24 * 60 * 60 * 1000;
            struct Window {
                const char* name;
                std::int64_t cutoff_ms;
            };
            const Window windows[] = {
                {"day", now_ms - kDayMs},
                {"7d", now_ms - 7 * kDayMs},
                {"30d", now_ms - 30 * kDayMs},
                {"90d", now_ms - 90 * kDayMs},
            };

            std::cout << "Read latency (p50/p95 over " << reps << " runs each)\n";
            for (const auto& window : windows) {
                const std::optional<std::int64_t> cutoff{window.cutoff_ms};
                print_stats(std::string("prediction_stats ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->prediction_stats(cutoff).sample_count;
                            }));
                print_stats(std::string("hourly_focus_buckets ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->hourly_focus_buckets(cutoff).size();
                            }));
                print_stats(std::string("session_summaries ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->recent_session_summaries(500, cutoff).size();
                            }));
                print_stats(std::string("daily_summary ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->daily_summary(now_ms, window.cutoff_ms).size();
                            }));
                std::cout << '\n';
            }
        }

        // --- Footprint, after the connection closed and the WAL checkpointed -----------
        const auto settled = measure_footprint(db_path);
        const double day_count = static_cast<double>(days);
        const double attended_hour_count = day_count * static_cast<double>(attended_hours);

        std::cout << "Footprint\n"
                  << "  sessions            " << generated.sessions << '\n'
                  << "  attended seconds    " << generated.attended_seconds << '\n'
                  << "  predictions         " << generated.predictions << '\n'
                  << "  feature_snapshots   " << generated.feature_snapshots << '\n'
                  << "  rows/attended hour  " << std::setprecision(0)
                  << static_cast<double>(generated.predictions + generated.feature_snapshots) /
                         attended_hour_count
                  << "  (ceiling is 7200)\n"
                  << "  db + wal, open      " << human_bytes(static_cast<double>(footprint.total_bytes()))
                  << "  (db " << human_bytes(static_cast<double>(footprint.db_bytes))
                  << ", wal " << human_bytes(static_cast<double>(footprint.wal_bytes)) << ")\n"
                  << "  db, closed          " << human_bytes(static_cast<double>(settled.total_bytes()))
                  << '\n'
                  << "  bytes/day           "
                  << human_bytes(static_cast<double>(settled.total_bytes()) / day_count) << '\n'
                  << "  bytes/attended hour "
                  << human_bytes(static_cast<double>(settled.total_bytes()) / attended_hour_count)
                  << '\n'
                  << "  bytes/prediction    "
                  << std::setprecision(1)
                  << static_cast<double>(settled.total_bytes()) /
                         static_cast<double>(std::max<std::size_t>(1, generated.predictions))
                  << " (prediction + its feature row)\n\n";

        if (env_flag("SNAPBACK_BUDGET_KEEP")) {
            std::cout << "Kept: " << dir.string() << '\n';
        } else {
            std::error_code error;
            std::filesystem::remove_all(dir, error);
        }
        std::cout << "checksum " << g_sink << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "budget benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
