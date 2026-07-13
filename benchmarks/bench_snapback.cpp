#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/state.hpp"
#include "bench_util.hpp"
#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "snapback/tracker.hpp"
#include "storage/storage.hpp"

using namespace snapback;

namespace {

double g_score_sink = 0.0;
std::size_t g_snapshot_sink = 0;

using snapback::bench::print_stats;
using snapback::bench::Stats;
using snapback::bench::summarize;
using snapback::bench::Timer;

CaptureEvent event(EventType type, double ts, std::string app, std::string title) {
    CaptureEvent ev;
    ev.event_type = type;
    ev.timestamp_secs = ts;
    ev.app_name = std::move(app);
    ev.window_title = std::move(title);
    return ev;
}

std::vector<CaptureEvent> build_trace(std::size_t minutes) {
    std::vector<CaptureEvent> events;
    events.reserve(minutes * 90);

    double ts = 1000.0;
    for (std::size_t minute = 0; minute < minutes; ++minute) {
        const bool distraction_block = minute % 17 == 8;
        const bool meeting_block = minute % 23 == 12;

        if (distraction_block) {
            events.push_back(event(EventType::WindowFocusChange, ts, "Google Chrome",
                                   "YouTube - Recommended"));
            ts += 0.8;
            for (int i = 0; i < 28; ++i) {
                events.push_back(event(i % 4 == 0 ? EventType::MouseClick : EventType::MouseMove,
                                       ts, "Google Chrome", "YouTube - Recommended"));
                ts += 0.45;
            }
            events.push_back(event(EventType::WindowFocusChange, ts, "Discord", "general"));
            ts += 1.2;
            continue;
        }

        if (meeting_block) {
            events.push_back(event(EventType::WindowFocusChange, ts, "Zoom", "Sprint planning"));
            ts += 1.0;
            for (int i = 0; i < 45; ++i) {
                events.push_back(event(EventType::MouseMove, ts, "Zoom", "Sprint planning"));
                ts += 0.9;
            }
            continue;
        }

        const std::string title = minute % 5 == 0 ? "storage.cpp - Snapback"
                                : minute % 5 == 1 ? "state.cpp - Snapback"
                                : minute % 5 == 2 ? "README.md - Snapback"
                                : minute % 5 == 3 ? "CMakeLists.txt - Snapback"
                                                   : "bench_snapback.cpp - Snapback";
        events.push_back(event(EventType::WindowFocusChange, ts, "Cursor", title));
        ts += 0.6;
        for (int i = 0; i < 52; ++i) {
            const auto type = i % 13 == 0 ? EventType::MouseMove : EventType::KeyPress;
            events.push_back(event(type, ts, "Cursor", title));
            ts += 0.72;
        }
    }

    return events;
}

Stats time_app_state_replay(const std::vector<CaptureEvent>& trace) {
    auto storage = Storage::open_memory();
    if (!storage) throw std::runtime_error("failed to open benchmark storage");
    // Heap-allocate: AppState embeds the 64K-slot capture ring buffer (~5 MB), which
    // overflows the default 1 MB stack if placed as a local (as main.cpp/tests do).
    auto state = std::make_unique<AppState>(std::move(*storage));
    state->start_session("Implement Snapback Windows demo", FocusMode::Normal);

    std::vector<double> samples;
    samples.reserve(trace.size());
    Timer total;
    for (const auto& ev : trace) {
        Timer per_event;
        state->process_event_for_test(ev);
        samples.push_back(per_event.elapsed_ms() * 1000.0);
    }
    return summarize(std::move(samples), total.elapsed_ms());
}

Stats time_feature_classifier_only(const std::vector<CaptureEvent>& trace) {
    FeatureExtractor features;
    Classifier classifier;
    std::vector<double> samples;
    samples.reserve(trace.size());
    double score_sum = 0.0;

    Timer total;
    for (const auto& ev : trace) {
        Timer per_event;
        const auto vector = features.update(ev);
        const auto scores = classifier.predict(vector, FocusMode::Normal);
        features.update_focus_score(scores.focus_score / 100.0, 0.2);
        score_sum += scores.focus_score;
        samples.push_back(per_event.elapsed_ms() * 1000.0);
    }
    g_score_sink += score_sum;
    return summarize(std::move(samples), total.elapsed_ms());
}

Stats time_context_tracker(const std::vector<CaptureEvent>& trace) {
    ContextTracker tracker;
    tracker.set_prediction_feedback(std::string("PRODUCTIVE"),
                                    std::string("Implement Snapback Windows demo"));
    std::vector<AppRuleRecord> rules;
    std::vector<double> samples;
    samples.reserve(trace.size());
    std::size_t snapshots = 0;

    Timer total;
    for (const auto& ev : trace) {
        Timer per_event;
        std::optional<ContextSnapshotDto> snap;
        if (ev.event_type == EventType::WindowFocusChange ||
            ev.event_type == EventType::WindowTitleChange) {
            snap = tracker.observe_window_change(ev.app_name, ev.window_title, rules,
                                                 ev.timestamp_secs, "2026-07-12T00:00:00Z");
        } else {
            snap = tracker.maybe_checkpoint_snapshot(
                rules, ev.timestamp_secs, [] { return std::string("2026-07-12T00:00:00Z"); });
        }
        if (snap) ++snapshots;
        samples.push_back(per_event.elapsed_ms() * 1000.0);
    }
    g_snapshot_sink += snapshots;
    return summarize(std::move(samples), total.elapsed_ms());
}

Stats time_storage_write_read(const std::vector<CaptureEvent>& trace) {
    auto storage = Storage::open_memory();
    if (!storage) throw std::runtime_error("failed to open benchmark storage");
    auto session = storage->create_session("Implement Snapback Windows demo", FocusMode::Normal);

    FeatureExtractor features;
    Classifier classifier;
    std::vector<double> samples;
    samples.reserve(trace.size());

    Timer total;
    for (const auto& ev : trace) {
        Timer per_event;
        const auto vector = features.update(ev);
        if (static_cast<int>(ev.timestamp_secs) % 2 == 0) {
            const auto scores = classifier.predict(vector, FocusMode::Normal);
            PredictionRecord record;
            record.session_id = session.session_id;
            record.focus_score = scores.focus_score;
            record.distraction_risk = scores.distraction_risk;
            record.focus_state = scores.focus_state;
            record.thrash_score = scores.thrash_score;
            record.drift_score = scores.drift_score;
            record.goal_alignment = scores.goal_alignment;
            record.timestamp = "2026-07-12T00:00:00Z";
            storage->insert_prediction(record);
            storage->insert_feature_snapshot(session.session_id, vector);
        }
        samples.push_back(per_event.elapsed_ms() * 1000.0);
    }
    (void)storage->recent_predictions(100);
    (void)storage->recap(session.session_id);
    return summarize(std::move(samples), total.elapsed_ms());
}

std::size_t trace_minutes_from_env() {
    if (const char* raw = std::getenv("SNAPBACK_BENCH_MINUTES")) {
        const auto minutes = std::strtoull(raw, nullptr, 10);
        if (minutes > 0) return static_cast<std::size_t>(minutes);
    }
    return 180;
}

}  // namespace

int main() {
    try {
        const auto minutes = trace_minutes_from_env();
        const auto trace = build_trace(minutes);

        std::cout << "Snapback benchmark replay\n";
        std::cout << "Trace: " << minutes << " simulated minutes, " << trace.size()
                  << " capture events, heuristic backend, in-memory SQLite\n\n";

        print_stats("feature + classifier", trace.size(), time_feature_classifier_only(trace));
        print_stats("context tracker", trace.size(), time_context_tracker(trace));
        print_stats("storage writes + reads", trace.size(), time_storage_write_read(trace));
        print_stats("AppState replay", trace.size(), time_app_state_replay(trace));
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "benchmark failed: " << err.what() << '\n';
        return 1;
    }
}
