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
//   SNAPBACK_BUDGET_CONCURRENT_SECS seconds per concurrency phase   (default 10)
//   SNAPBACK_BUDGET_READERS        threads in the hot-loop phase    (default 2)
//   SNAPBACK_BUDGET_REVIEW_SECS    seconds of the realistic phase   (default 60)
//   SNAPBACK_BUDGET_REVIEW_IDLE_MS think time between Review loads  (default 5000)
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
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "bench_util.hpp"

#include "storage/storage.hpp"
#include "types.hpp"
#include "util/ranked_mutex.hpp"
#include "util/writer_priority.hpp"

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
    std::size_t context_snapshots{};
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
            // Title history, at the rate the engine writes it: a row on every foreground
            // change plus a 30-second checkpoint. One row every 24 s models a checkpoint and
            // a switch every two minutes -- thousands of rows a week, as the roadmap's FTS5
            // entry states -- so `context_app_counts` reads a table rather than an empty one.
            constexpr const char* kApps[] = {"Code.exe", "chrome.exe", "Slack.exe",
                                             "WindowsTerminal.exe", "Figma.exe", "Notion.exe"};
            ContextSnapshotDto snap;
            for (std::size_t second = 0; second < block_seconds; second += 24) {
                snap.app_name = kApps[static_cast<std::size_t>(roll(rng)) % std::size(kApps)];
                snap.window_title = "measured budgets - " + snap.app_name;
                snap.timestamp_ms = block_start + static_cast<std::int64_t>(second) * 1000;
                storage.save_context_snapshot(session.session_id, snap);
                ++out.context_snapshots;
            }
            const std::int64_t block_end =
                block_start + static_cast<std::int64_t>(block_seconds) * 1000;
            storage.close_session_span(session.session_id, block_end);
            storage.end_session(session.session_id);
            // `create_session` and `end_session` stamp the wall clock, which put all 180
            // sessions inside the last few seconds. Every `sessions`-windowed read then
            // matched the whole history whatever window it was asked for, and read as
            // "flat across windows" -- a property of the fixture, not of the query. The
            // session has to sit where its predictions and spans already do.
            storage.execute_for_test("UPDATE sessions SET started_at = " +
                                     std::to_string(block_start) +
                                     ", ended_at = " + std::to_string(block_end) +
                                     " WHERE session_id = '" + session.session_id + "'");
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

// --- Roadmap 14.1: what a report costs the writer ------------------------------------------
//
// 14.1 proposes a separate SQLite read lane and then forbids building it from structure
// alone: measure the largest reads running *concurrently with persistence* first, and close
// the item with the numbers if the result is immaterial. This is that measurement.
//
// The shape mirrors `AppState` exactly, because a benchmark of a different shape would answer
// a different question: one `Storage`, one `RankedMutex` at `LockRank::Storage` around every
// use of it, a writer persisting at the engine's real cadence, and readers issuing the
// heaviest Review queries. WAL already allows a concurrent reader and writer at the SQLite
// level -- the serialization being measured here is ours, not SQLite's, which is precisely
// what a read lane would remove.
//
// The figure that matters is the **comparison**: the same writer, measured alone and then
// under load. An absolute persist latency says nothing about contention on its own.
//
// **Two kinds of load, and the difference between them is the point.** The first draft ran
// only reader threads looping with no think time, which produced a 29 s worst-case wait and
// no way to tell how much of it was the benchmark. A hot loop is a ceiling: it is the right
// number for "what is the worst this lock can do to a persist" and the wrong one for "what
// does opening Review cost". So both run here -- one realistic reader doing what a person
// does, and the hot loop kept beside it as the bound -- and they are reported side by side,
// the same way SNAPBACK_BUDGET_DUTY_PCT publishes a ceiling and an expected day.
struct WriterResult {
    // Time blocked on the storage lock, which is the quantity 14.1 names: "measure writer
    // delay". Kept apart from the write itself, because the two answer different questions --
    // a slow write is SQLite's cost and a long wait is ours, and only the second is what a
    // separate read lane would remove.
    Stats wait;
    Stats write;
    std::size_t writes{};
    double wall_ms{};
};

// One tick's worth of persistence: a prediction and its feature row, which is what
// `AppState::compute_event` writes at most once per attended second.
void persist_one(Storage& storage, const std::string& session_id, const FeatureVector& features,
                 std::int64_t timestamp_ms) {
    PredictionRecord record;
    record.session_id = session_id;
    record.focus_score = 72.0;
    record.distraction_risk = 0.28;
    record.focus_state = "PRODUCTIVE";
    record.thrash_score = 0.1;
    record.drift_score = 0.2;
    record.goal_alignment = 0.7;
    record.timestamp_ms = timestamp_ms;
    record.model_id = "heuristic:bench";
    storage.insert_prediction(record);
    storage.insert_feature_snapshot(session_id, features);
}

// One Review load, issued the way the product issues it (Roadmap 14.1).
//
// `useReviewWorkflow.ts:loadReview` fires five commands inside one `Promise.all`, which reads
// as concurrent and is not. `CommandRegistry::add` registers a synchronous handler and the
// bridge runs it inline on the webview's own thread; only `add_async` commands reach the
// worker, and none of these five is one. A Review load is therefore five reads in a row on a
// single thread, and modelling the reader as N parallel threads measures a shape the product
// cannot produce.
//
// The lock granularity is `state.cpp`'s and not a simplification of it: one acquisition per
// command, with `analytics` and `summary_report` each holding theirs across every storage
// call they make. That granularity is the quantity being measured, so it cannot be rounded.
// The five, in the order `loadReview` lists them. Named so a hold can be attributed to a
// command rather than to "one of the reads".
constexpr std::size_t kReviewCommandCount = 5;
constexpr const char* kReviewCommands[kReviewCommandCount] = {
    "get_analytics", "get_summary_report", "get_focus_summary", "get_session_history",
    "get_daily_summary"};

struct ReviewLoadTiming {
    // How long each command *held* the lock, timed from acquisition to release so the
    // reader's own wait is not counted as a hold.
    double held_us[kReviewCommandCount]{};
    double total_us{};
    double longest_hold_us() const {
        return *std::max_element(std::begin(held_us), std::end(held_us));
    }
};

ReviewLoadTiming review_load(Storage& storage, RankedMutex& storage_lock, std::int64_t now_ms,
                             std::int64_t cutoff_ms, const WriterPriority* gate = nullptr) {
    const std::optional<std::int64_t> cutoff{cutoff_ms};
    ReviewLoadTiming timing;
    Timer whole_load;
    // Each command: acquire, time the held section, release. The gap between two of these is
    // exactly the opening a fair lock would hand to a waiting writer, which is why the holds
    // are timed individually -- a wait longer than the longest hold cannot be one command's.
    const auto held = [&](std::size_t index, auto&& body) {
        // With a gate, exactly what the five `AppState` commands do before their lock.
        if (gate != nullptr) gate->yield_to_writers();
        std::unique_lock<RankedMutex> lock(storage_lock);
        Timer holding;
        body();
        timing.held_us[index] = holding.elapsed_us();
    };

    held(0, [&] {  // AppState::analytics: four reads under one lock.
        g_sink += storage.prediction_stats(cutoff).sample_count;
        g_sink += storage.hourly_focus_buckets(cutoff).size();
        g_sink += storage.context_app_counts(200, 200, cutoff).size();
        g_sink += storage.productive_session_streak(200, 70.0, cutoff);
    });
    // AppState::summary_report: four reads. The attended total takes the 30d/custom branch
    // (`attended_secs_since`); today and 7d call a calendar helper instead, which is the same
    // shape of query over a smaller span.
    held(1, [&] {
        g_sink += storage.prediction_stats(cutoff).sample_count;
        g_sink += storage.session_window_totals(500, cutoff_ms).session_count;
        g_sink += storage.context_app_counts(500, 200, cutoff).size();
        g_sink += storage.attended_secs_since(now_ms, cutoff);
    });
    held(2, [&] {  // AppState::focus_summary_for_window: one aggregate (7.33).
        g_sink += storage.prediction_stats(cutoff).sample_count;
    });
    held(3, [&] {  // AppState::session_history_for_window.
        g_sink += storage.recent_session_summaries(500, cutoff).size();
    });
    held(4, [&] {  // AppState::daily_summary.
        g_sink += storage.daily_summary(now_ms, cutoff_ms).size();
    });
    timing.total_us = whole_load.elapsed_us();
    return timing;
}

// Runs a paced writer, taking the shared lock per persist exactly as the engine does. Called
// once per phase: alone, against one realistic Review reader, and against the hot loop.
//
// **Paced, not flat out**, and the first draft of this file got that wrong in a way worth
// recording. A writer with no pacing does tens of thousands of persists a second when it is
// alone and a few dozen when a reader holds the lock, so the two phases take different
// numbers of samples and the comparison degenerates into a throughput ratio that says more
// about the benchmark than about the engine. The engine persists at most once per second and
// spends the rest of its time asleep; what it wants to know is how long *one* persist waits
// when a report is in flight. Fixed attempts at a fixed interval measure that.
WriterResult run_writer(Storage& storage, RankedMutex& storage_lock, const std::string& session_id,
                        std::int64_t base_ms, std::size_t attempts, std::int64_t interval_ms,
                        WriterPriority* gate = nullptr) {
    FeatureVector features;
    std::vector<double> waits_us;
    std::vector<double> writes_us;
    waits_us.reserve(attempts);
    writes_us.reserve(attempts);

    WriterResult out;
    const auto started = Clock::now();
    Timer total;
    for (std::size_t i = 0; i < attempts; ++i) {
        // sleep_until, not sleep_for: after a persist that waited four seconds the next
        // attempt is due immediately, and a relative sleep would let one stall push the
        // whole schedule back and quietly shorten the phase.
        std::this_thread::sleep_until(started + std::chrono::milliseconds(
                                                    static_cast<std::int64_t>(i) * interval_ms));
        Timer waiting;
        // With a gate, exactly what `AppState::engine_tick` does around its persist lock.
        std::optional<WriterPriority::Announce> announce;
        if (gate != nullptr) announce.emplace(*gate);
        std::unique_lock<RankedMutex> lock(storage_lock);
        if (announce) announce->release();
        waits_us.push_back(waiting.elapsed_us());
        Timer writing;
        persist_one(storage, session_id, features,
                    base_ms + static_cast<std::int64_t>(i) * 1000);
        writes_us.push_back(writing.elapsed_us());
        lock.unlock();
        ++out.writes;
    }
    out.wall_ms = total.elapsed_ms();
    out.wait = summarize(std::move(waits_us), out.wall_ms);
    out.write = summarize(std::move(writes_us), out.wall_ms);
    return out;
}

int main() {
    try {
        const auto days = env_size("SNAPBACK_BUDGET_DAYS", 90);
        const auto attended_hours = env_size("SNAPBACK_BUDGET_ATTENDED_HOURS", 6);
        const auto duty_pct = std::min<std::size_t>(100, env_size("SNAPBACK_BUDGET_DUTY_PCT", 100));
        const auto reps = env_size("SNAPBACK_BUDGET_QUERY_REPS", 25);
        const auto concurrent_secs = env_size("SNAPBACK_BUDGET_CONCURRENT_SECS", 10);
        const auto reader_count = env_size("SNAPBACK_BUDGET_READERS", 2);
        // Longer than the hot-loop phase on purpose. One Review load against the 90-day
        // ceiling fixture is seconds of work, so a ten-second phase would sample one of them
        // and call it a distribution.
        const auto review_secs = env_size("SNAPBACK_BUDGET_REVIEW_SECS", 60);
        const auto review_idle_ms = env_size("SNAPBACK_BUDGET_REVIEW_IDLE_MS", 5000);
        // Ten a second: faster than the engine's real cadence of at most one, so a ten-second
        // phase takes a hundred samples instead of ten, and still slow enough that the writer
        // is idle between attempts the way the engine is.
        constexpr std::int64_t kWriterIntervalMs = 100;

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
                // The other four reads a Review load makes, with the arguments `review_load`
                // passes. Without them `get_analytics`' hold cannot be attributed to a query,
                // and 14.14 asks for exactly these two `sessions`-window sites to be measured
                // before either is touched.
                print_stats(std::string("context_app_counts ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->context_app_counts(200, 200, cutoff).size();
                            }));
                print_stats(std::string("session_streak ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->productive_session_streak(200, 70.0, cutoff);
                            }));
                print_stats(std::string("session_window_totals ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->session_window_totals(500, window.cutoff_ms)
                                              .session_count;
                            }));
                print_stats(std::string("attended_secs_since ") + window.name, reps,
                            time_query(reps, [&] {
                                g_sink += storage->attended_secs_since(now_ms, cutoff);
                            }));
                std::cout << '\n';
            }

            // --- Concurrency: the writer alone, then under report load (14.1) ----------
            std::cout << "Writer delay under concurrent reads (Roadmap 14.1)" "\n"
                      << "  one Storage behind one LockRank::Storage mutex, exactly as" "\n"
                      << "  AppState holds it. Two loads: one realistic Review reader," "\n"
                      << "  and the hot loop kept beside it as the ceiling." "\n\n";

            RankedMutex storage_lock{LockRank::Storage};
            const auto writer_session =
                storage->create_session("Concurrency measurement", FocusMode::Normal);
            const std::int64_t writer_base_ms = now_ms - 60 * 1000;
            const auto attempts = concurrent_secs * 1000 / kWriterIntervalMs;
            const auto review_attempts = review_secs * 1000 / kWriterIntervalMs;
            // The range the Review page opens on.
            const std::int64_t review_cutoff_ms = now_ms - 7 * kDayMs;

            reset_lock_metrics();
            const auto solo = run_writer(*storage, storage_lock, writer_session.session_id,
                                         writer_base_ms, attempts, kWriterIntervalMs);
            const auto solo_metrics = lock_metrics(LockRank::Storage);

            // --- Phase 2: one person with Review open ----------------------------------
            // One thread, because the five commands are synchronous and share the webview's
            // thread; a load and then think time, because a person reads the page they
            // opened rather than reloading it as fast as it will answer.
            //
            // Run twice: as the lock behaves by itself, and with `WriterPriority` wired the
            // way `AppState` wires it. Same database, same host, same run, so the difference
            // between the two is the gate and nothing else.
            struct ReviewPhase {
                WriterResult writer;
                std::uint64_t loads{};
                // How long one load takes end to end. Without it the writer's worst wait is
                // a number with nothing to compare it to; with it, "the writer waited out a
                // whole load" and "the writer waited out one query" are distinguishable.
                Stats load;
                double worst_hold_us[kReviewCommandCount]{};
                // Every hold, not only the worst: a maximum over a handful of loads cannot
                // say whether a command is slow or one load was unlucky.
                std::vector<double> hold_samples_us[kReviewCommandCount];
                LockMetricsSnapshot metrics;
                std::size_t worst_command() const {
                    return static_cast<std::size_t>(
                        std::max_element(std::begin(worst_hold_us), std::end(worst_hold_us)) -
                        std::begin(worst_hold_us));
                }
            };
            const auto review_phase = [&](WriterPriority* gate, std::int64_t base_ms) {
                ReviewPhase out;
                std::atomic<bool> stop{false};
                std::vector<double> load_us;
                reset_lock_metrics();
                std::thread reader([&] {
                    while (!stop.load(std::memory_order_relaxed)) {
                        const auto timing =
                            review_load(*storage, storage_lock, now_ms, review_cutoff_ms, gate);
                        load_us.push_back(timing.total_us);
                        for (std::size_t i = 0; i < kReviewCommandCount; ++i) {
                            out.worst_hold_us[i] = std::max(out.worst_hold_us[i], timing.held_us[i]);
                            out.hold_samples_us[i].push_back(timing.held_us[i]);
                        }
                        ++out.loads;
                        // Slept in slices so the phase can end during think time instead of
                        // holding the run open for one last full idle period.
                        for (std::size_t waited = 0;
                             waited < review_idle_ms && !stop.load(std::memory_order_relaxed);
                             waited += 50) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }
                });
                out.writer = run_writer(*storage, storage_lock, writer_session.session_id, base_ms,
                                        review_attempts, kWriterIntervalMs, gate);
                stop.store(true, std::memory_order_relaxed);
                reader.join();
                out.metrics = lock_metrics(LockRank::Storage);
                // Safe to read unsynchronized: the only writer was the thread just joined.
                out.load = summarize(std::move(load_us), out.writer.wall_ms);
                return out;
            };
            auto unfair = review_phase(nullptr, writer_base_ms + 10 * 60 * 1000);
            WriterPriority gate;
            auto gated = review_phase(&gate, writer_base_ms + 20 * 60 * 1000);

            const auto& reviewed = unfair.writer;
            const auto review_loads = unfair.loads;
            const auto& review_metrics = unfair.metrics;
            const auto& review_load_stats = unfair.load;
            const auto& worst_hold_us = unfair.worst_hold_us;
            auto& hold_samples_us = unfair.hold_samples_us;
            const auto worst_command = unfair.worst_command();

            // --- Phase 3: the ceiling, readers looping with no think time --------------
            std::atomic<bool> readers_stop{false};
            std::atomic<std::uint64_t> reads_done{0};
            std::vector<std::thread> readers;
            reset_lock_metrics();
            for (std::size_t i = 0; i < reader_count; ++i) {
                readers.emplace_back([&, i] {
                    // Staggered windows, so the readers are not all served the same page
                    // cache and one of them is always doing the expensive thing.
                    const std::optional<std::int64_t> cutoff{
                        now_ms - static_cast<std::int64_t>(1 + (i % 3) * 6) * 24 * 60 * 60 * 1000};
                    while (!readers_stop.load(std::memory_order_relaxed)) {
                        {
                            std::lock_guard lock(storage_lock);
                            g_sink += storage->prediction_stats(cutoff).sample_count;
                        }
                        {
                            std::lock_guard lock(storage_lock);
                            g_sink += storage->daily_summary(now_ms, *cutoff).size();
                        }
                        {
                            std::lock_guard lock(storage_lock);
                            g_sink += storage->recent_session_summaries(500, cutoff).size();
                        }
                        reads_done.fetch_add(3, std::memory_order_relaxed);
                    }
                });
            }
            const auto loaded =
                run_writer(*storage, storage_lock, writer_session.session_id,
                           writer_base_ms + 30 * 60 * 1000, attempts, kWriterIntervalMs);
            readers_stop.store(true, std::memory_order_relaxed);
            for (auto& reader : readers) reader.join();
            const auto loaded_metrics = lock_metrics(LockRank::Storage);

            const auto ms = [](double microseconds) { return microseconds / 1000.0; };
            // Three columns, and the middle one is the answer to 14.1's question. The right
            // one bounds it. Persist counts differ per phase, which matters for a maximum --
            // the more samples a phase takes, the more chances it has to find a bad one --
            // so the count is printed rather than left to be inferred from the env defaults.
            const auto row = [&](const char* label, double a, double b, double c) {
                std::cout << "    " << std::left << std::setw(6) << label << std::right
                          << std::setw(12) << a << " ms" << std::setw(12) << b << " ms"
                          << std::setw(12) << c << " ms" "\n";
            };
            std::cout << std::fixed << std::setprecision(2)
                      << "  the Review reader   " << review_loads
                      << " load(s) of five commands, " << review_idle_ms
                      << " ms think time between" "\n"
                      << "                      one load takes p50 "
                      << ms(review_load_stats.p50_us) << " ms, max "
                      << ms(review_load_stats.max_us) << " ms" "\n"
                      << "                      longest single command hold "
                      << ms(worst_hold_us[worst_command]) << " ms ("
                      << kReviewCommands[worst_command] << ")" "\n";
            for (std::size_t i = 0; i < kReviewCommandCount; ++i) {
                const auto hold = summarize(std::move(hold_samples_us[i]), reviewed.wall_ms);
                std::cout << "                        " << std::left << std::setw(20)
                          << kReviewCommands[i] << std::right << " holds p50 " << std::setw(9)
                          << ms(hold.p50_us) << " ms, max " << std::setw(9) << ms(hold.max_us)
                          << " ms" "\n";
            }
            std::cout << "  the hot loop        " << reader_count << " thread(s), "
                      << reads_done.load() << " queries, no think time" "\n"
                      << "  the writer          one persist every " << kWriterIntervalMs
                      << " ms throughout" "\n\n"
                      << "  writer's wait for the lock" "\n"
                      << "          " << std::setw(15) << "solo" << std::setw(15)
                      << "Review reader" << std::setw(15)
                      << (std::to_string(reader_count) + " hot readers") << "\n";
            std::cout << "    " << std::left << std::setw(6) << "n" << std::right
                      << std::setw(12) << solo.writes << "   " << std::setw(12)
                      << reviewed.writes << "   " << std::setw(12) << loaded.writes << "\n";
            row("p50", ms(solo.wait.p50_us), ms(reviewed.wait.p50_us), ms(loaded.wait.p50_us));
            row("p95", ms(solo.wait.p95_us), ms(reviewed.wait.p95_us), ms(loaded.wait.p95_us));
            row("max", ms(solo.wait.max_us), ms(reviewed.wait.max_us), ms(loaded.wait.max_us));
            std::cout << "\n";
            row("write", ms(solo.write.p95_us), ms(reviewed.write.p95_us),
                ms(loaded.write.p95_us));
            std::cout << "    (write is p95 of the persist itself: SQLite's cost, not ours)" "\n\n"
                      << "  storage lock, solo    acquisitions=" << solo_metrics.acquisitions
                      << " contended=" << solo_metrics.contended
                      << " wait max=" << solo_metrics.max_wait_us << "us" "\n"
                      << "  storage lock, review  acquisitions=" << review_metrics.acquisitions
                      << " contended=" << review_metrics.contended
                      << " wait max=" << review_metrics.max_wait_us << "us" "\n"
                      << "  storage lock, hot     acquisitions=" << loaded_metrics.acquisitions
                      << " contended=" << loaded_metrics.contended
                      << " wait max=" << loaded_metrics.max_wait_us << "us" "\n"
                      << "  (the same instrument the app reports through get_diagnostics, so" "\n"
                      << "   a figure here and one from a support bundle are comparable)" "\n\n"
                      // The one comparison 14.1 turns on. The reader drops the lock between
                      // each of the five commands, so a fair lock would hand a waiting writer
                      // one of those four openings. A wait longer than the longest single
                      // hold means it was passed over at a boundary -- that is `std::mutex`
                      // being unfair, not one connection being shared, and the two have
                      // different fixes. Below 1.0x the wait fits inside one command and the
                      // shared connection is the whole story.
                      << "  starved across commands?  worst wait "
                      << (ms(reviewed.wait.max_us) /
                          std::max(1.0, ms(worst_hold_us[worst_command])))
                      << "x the longest single hold" "\n"
                      << "                            (>1 means the writer was passed over at"
                      << " a command boundary)" "\n\n"
                      // 14.1 asks for dropped-event risk, and the ring is what converts a
                      // stall into a loss: the capture thread keeps pushing while the engine
                      // is blocked, and CaptureThread::kCapacity is 65,536 events. Quoted
                      // from the realistic phase, because the risk worth stating is the one a
                      // person can actually cause by opening Review.
                      << "  dropped-event risk  a Review load stalls a persist for at most "
                      << ms(reviewed.wait.max_us) / 1000.0 << " s;" "\n"
                      << "                      at 50 events/s that is "
                      << (ms(reviewed.wait.max_us) / 1000.0 * 50.0) << " of 65536 ring slots"
                      << " (hot-loop ceiling: "
                      << (ms(loaded.wait.max_us) / 1000.0 * 50.0) << ")" "\n\n";

            // The same Review phase with the gate `AppState` now uses (14.1). The claim to
            // check is the bound: a persist waits for the command in progress, not the load,
            // so its worst wait should come out at or under the longest single hold.
            const auto gated_worst = gated.worst_command();
            std::cout << "  with writer priority (util/writer_priority.hpp, as AppState wires it)" "\n"
                      << "    Review reader     " << gated.loads << " load(s); one load takes p50 "
                      << ms(gated.load.p50_us) << " ms, max " << ms(gated.load.max_us) << " ms" "\n"
                      << "                      longest single command hold "
                      << ms(gated.worst_hold_us[gated_worst]) << " ms ("
                      << kReviewCommands[gated_worst] << ")" "\n"
                      << "    writer's wait     n=" << gated.writer.writes << "  p50 "
                      << ms(gated.writer.wait.p50_us) << " ms  p95 " << ms(gated.writer.wait.p95_us)
                      << " ms  max " << ms(gated.writer.wait.max_us) << " ms" "\n"
                      << "    worst wait / longest hold  "
                      << (ms(gated.writer.wait.max_us) /
                          std::max(1.0, ms(gated.worst_hold_us[gated_worst])))
                      << "x  (<=1 means the gate held: no persist sat out a whole load)" "\n"
                      << "    storage lock      acquisitions=" << gated.metrics.acquisitions
                      << " contended=" << gated.metrics.contended
                      << " wait max=" << gated.metrics.max_wait_us << "us" "\n\n";
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
                  << "  context_snapshots   " << generated.context_snapshots << '\n'
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
                  << " (prediction + its feature row, title history amortized)\n\n";

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
