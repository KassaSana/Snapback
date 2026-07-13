// Targeted hot-path micro-benchmarks for Snapback's critical components, isolating each
// from the others (unlike bench_snapback.cpp, which times a full session replay):
//
//   1. Producer latency   — RingBuffer<CaptureEvent,65536>::push() (wait-free? spikes?)
//   2. Consumer drain      — drain 5,000 events -> feature extract -> heuristic classify
//   3. Lock contention     — UI-style reads on AppState while a writer holds mutex_
//   4. SQLite persistence  — per-tick insert (prediction + feature snapshot) on disk
//
// Zero-dependency <chrono> timing (see bench_util.hpp). Runs on whatever machine builds
// it; numbers in the report are from the dev box, not fabricated.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/state.hpp"
#include "bench_util.hpp"
#include "capture/active_window.hpp"
#include "capture/ring_buffer.hpp"
#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "storage/storage.hpp"
#include "types.hpp"

using namespace snapback;
using snapback::bench::print_stats;
using snapback::bench::Stats;
using snapback::bench::summarize;
using snapback::bench::Timer;

namespace {

// Sinks to keep the optimizer from deleting the work we're timing.
volatile double g_double_sink = 0.0;
volatile std::uint64_t g_int_sink = 0;

CaptureEvent make_event(EventType type, double ts, std::string app, std::string title) {
    CaptureEvent e;
    e.event_type = type;
    e.timestamp_secs = ts;
    e.app_name = std::move(app);
    e.window_title = std::move(title);
    return e;
}

// A realistic title well past the ~15-char small-string-optimization threshold, so the
// std::string genuinely lives on the heap (relevant to the "allocation-free" claim).
const std::string kLongTitle = "bench_hotpaths.cpp - snapbackCplusplus - Visual Studio Code";
const std::string kApp = "Code.exe";

// --- 1. Producer: RingBuffer::push() -------------------------------------------------
void bench_producer_push() {
    constexpr std::size_t kCount = 60000;  // < capacity (65536), so no drain mid-run
    auto buffer = std::make_unique<RingBuffer<CaptureEvent, 65536>>();

    // (a) Pure push: pre-build events and MOVE them in. This is the true ring cost — a
    //     move-assign into the slot plus a release store. No allocation at the push site.
    std::vector<CaptureEvent> pool;
    pool.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
        pool.push_back(make_event(EventType::KeyPress, static_cast<double>(i), kApp, kLongTitle));

    std::vector<double> move_samples;
    move_samples.reserve(kCount);
    Timer move_total;
    for (std::size_t i = 0; i < kCount; ++i) {
        Timer t;
        const bool ok = buffer->push(std::move(pool[i]));
        move_samples.push_back(t.elapsed_us());
        g_int_sink += ok ? 1u : 0u;
    }
    print_stats("producer push (move)", kCount, summarize(std::move(move_samples), move_total.elapsed_ms()),
                /*nanoseconds=*/true);

    // drain (untimed) to reset the buffer
    while (buffer->pop()) {
    }

    // (b) push + event construction: build a fresh long-title event INSIDE the timed
    //     region. This exposes the upstream allocation the OS hook actually pays.
    std::vector<double> ctor_samples;
    ctor_samples.reserve(kCount);
    Timer ctor_total;
    for (std::size_t i = 0; i < kCount; ++i) {
        Timer t;
        CaptureEvent e = make_event(EventType::KeyPress, static_cast<double>(i), kApp, kLongTitle);
        const bool ok = buffer->push(std::move(e));
        ctor_samples.push_back(t.elapsed_us());
        g_int_sink += ok ? 1u : 0u;
    }
    print_stats("producer push (+construct)", kCount,
                summarize(std::move(ctor_samples), ctor_total.elapsed_ms()), true);
}

// --- 2. Consumer: drain 5,000 events -> features -> heuristic classify ---------------
void bench_consumer_drain() {
    constexpr std::size_t kBatch = 5000;
    auto buffer = std::make_unique<RingBuffer<CaptureEvent, 65536>>();
    for (std::size_t i = 0; i < kBatch; ++i) {
        const auto type = i % 13 == 0 ? EventType::WindowFocusChange : EventType::KeyPress;
        buffer->push(make_event(type, static_cast<double>(i) * 0.05, kApp, kLongTitle));
    }

    FeatureExtractor features;
    Classifier classifier;  // heuristic (no ONNX model loaded)
    const std::vector<AppRuleRecord> no_rules;

    std::vector<double> samples;
    samples.reserve(kBatch);
    Timer total;
    while (auto ev = buffer->pop()) {
        Timer t;
        const auto vector = features.update(*ev, no_rules);
        const auto scores = classifier.predict(vector, FocusMode::Normal);
        features.update_focus_score(scores.focus_score / 100.0, 0.2);
        g_double_sink += scores.focus_score;
        samples.push_back(t.elapsed_us());
    }
    // Capture the count before summarize() moves the vector (argument evaluation order
    // is unspecified, so don't read samples.size() in the same call).
    const std::size_t processed = samples.size();
    const auto stats = summarize(std::move(samples), total.elapsed_ms());
    print_stats("consumer drain+classify", processed, stats);
}

// --- 3. Lock contention: UI reads vs a writer holding mutex_ -------------------------
Stats measure_reader(AppState& state, std::atomic<bool>& stop, std::size_t max_samples) {
    std::vector<double> samples;
    samples.reserve(max_samples);
    Timer total;
    while (!stop.load(std::memory_order_relaxed) && samples.size() < max_samples) {
        Timer t;
        const auto health = state.health();  // acquires mutex_
        samples.push_back(t.elapsed_us());
        g_int_sink += health.capture_events_dropped;
    }
    return summarize(std::move(samples), total.elapsed_ms());
}

void bench_lock_contention() {
    constexpr std::size_t kReadSamples = 200000;

    // Baseline: reader alone, no writer holding the lock.
    {
        auto state = std::make_unique<AppState>(*Storage::open_memory());
        state->start_session("lock baseline", FocusMode::Normal);
        std::atomic<bool> stop{false};
        auto stats = measure_reader(*state, stop, 50000);
        print_stats("lock read (uncontended)", 50000, stats);
    }

    // Contended: a writer thread hammers process_event_for_test (feature+classify+SQLite
    // insert, all under mutex_) while the reader times health() calls.
    {
        auto state = std::make_unique<AppState>(*Storage::open_memory());
        state->start_session("lock contended", FocusMode::Normal);

        std::atomic<bool> stop{false};
        std::thread writer([&] {
            double ts = 0.0;
            while (!stop.load(std::memory_order_relaxed)) {
                // +2s per event so the 1 Hz prediction throttle never skips: every event
                // runs the full classify + insert under the lock (worst-case hold).
                ts += 2.0;
                state->process_event_for_test(
                    make_event(EventType::KeyPress, ts, kApp, kLongTitle));
            }
        });

        auto stats = measure_reader(*state, stop, kReadSamples);
        stop.store(true, std::memory_order_relaxed);
        writer.join();
        print_stats("lock read (contended)", kReadSamples, stats);
    }
}

// --- 4. SQLite persistence: per-tick insert (prediction + feature snapshot) ----------
Stats persist_ticks(Storage& storage, std::size_t ticks) {
    auto session = storage.create_session("persistence bench", FocusMode::Normal);
    FeatureVector features;  // 31 zeros is fine for the write-path timing
    std::vector<double> samples;
    samples.reserve(ticks);
    Timer total;
    for (std::size_t i = 0; i < ticks; ++i) {
        PredictionRecord record;
        record.session_id = session.session_id;
        record.focus_score = 72.0;
        record.distraction_risk = 0.2;
        record.focus_state = "PRODUCTIVE";
        record.goal_alignment = 0.5;
        record.timestamp = "2026-07-12T00:00:00Z";
        Timer t;
        // One transaction per tick, matching AppState::process_event_unlocked.
        Storage::Transaction txn(storage);
        storage.insert_prediction(record);
        storage.insert_feature_snapshot(session.session_id, features);
        txn.commit();
        samples.push_back(t.elapsed_us());
    }
    return summarize(std::move(samples), total.elapsed_ms());
}

void bench_sqlite_persistence() {
    constexpr std::size_t kTicks = 2000;

    // On-disk DB: the real engine path (autocommit -> a durability sync per statement).
    const auto dir = std::filesystem::temp_directory_path() /
                     ("snapback_bench_db_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        auto storage = Storage::open(dir);
        if (storage) print_stats("sqlite persist tick (disk)", kTicks, persist_ticks(*storage, kTicks));
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    // In-memory reference: isolates SQL/statement cost from filesystem durability.
    {
        auto storage = Storage::open_memory();
        if (storage) print_stats("sqlite persist tick (memory)", kTicks, persist_ticks(*storage, kTicks));
    }
}

}  // namespace

int main() {
    std::cout << "Snapback hot-path micro-benchmarks\n";
    std::cout << "(steady_clock, heuristic backend; latencies are per-op)\n\n";

    std::cout << "-- 1. Producer (RingBuffer::push) --\n";
    bench_producer_push();
    std::cout << "\n-- 2. Consumer (drain 5k -> features -> classify) --\n";
    bench_consumer_drain();
    std::cout << "\n-- 3. Lock contention (AppState reads vs writer) --\n";
    bench_lock_contention();
    std::cout << "\n-- 4. SQLite persistence (per tick) --\n";
    bench_sqlite_persistence();

    // Touch the sinks so they can't be optimized away.
    if (g_double_sink < 0.0 && g_int_sink == 0) std::cout << "unreachable\n";
    return 0;
}
