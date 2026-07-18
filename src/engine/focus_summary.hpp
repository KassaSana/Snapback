// Focus summary aggregation. Roadmap 2.2 (first slice: the pure math).
//
// Turns a batch of prediction rows into the numbers a daily/weekly recap shows: average
// focus, how much time read as distracted, the peak, and the longest unbroken focus run.
// Pure over the input vector — no storage, no clock — so the recap logic is unit-testable
// independent of the DB. Storage aggregate queries + the frontend view are follow-ups.
#pragma once

#include <cstddef>
#include <vector>

#include "types.hpp"

namespace snapback {

struct FocusSummary {
    std::size_t sample_count = 0;
    double avg_focus_score = 0.0;      // mean focus_score across samples
    double peak_focus_score = 0.0;     // best single sample
    std::size_t distracted_samples = 0;
    double distracted_fraction = 0.0;  // distracted_samples / sample_count, in [0,1]
    std::size_t longest_focus_streak = 0;  // longest run of non-DISTRACTED samples
};

// Aggregate predictions in the order given (assumed chronological). Empty input yields a
// zeroed summary — callers can render "no data yet" without special-casing.
inline FocusSummary summarize_predictions(const std::vector<PredictionRecord>& preds) {
    FocusSummary s;
    s.sample_count = preds.size();
    if (preds.empty()) return s;

    double sum = 0.0;
    std::size_t current_streak = 0;
    for (const auto& p : preds) {
        sum += p.focus_score;
        if (p.focus_score > s.peak_focus_score) s.peak_focus_score = p.focus_score;
        if (p.focus_state == "DISTRACTED") {
            ++s.distracted_samples;
            current_streak = 0;
        } else {
            ++current_streak;
            if (current_streak > s.longest_focus_streak) s.longest_focus_streak = current_streak;
        }
    }
    s.avg_focus_score = sum / static_cast<double>(preds.size());
    s.distracted_fraction =
        static_cast<double>(s.distracted_samples) / static_cast<double>(preds.size());
    return s;
}

}  // namespace snapback
