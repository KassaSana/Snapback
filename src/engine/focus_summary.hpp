// Focus summary aggregation. Roadmap 2.2 (first slice: the pure math).
//
// Turns a batch of prediction rows into the numbers a daily/weekly recap shows: average
// focus, how much time read as distracted, the peak, and the longest unbroken focus run.
// Pure over the input vector — no storage, no clock — so the recap logic is unit-testable
// independent of the DB. Storage aggregate queries + the frontend view are follow-ups.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.hpp"
#include "util/time.hpp"

namespace snapback {

struct FocusSummary {
    std::size_t sample_count = 0;
    double avg_focus_score = 0.0;      // mean focus_score across samples
    double peak_focus_score = 0.0;     // best single sample
    std::size_t distracted_samples = 0;
    double distracted_fraction = 0.0;  // distracted_samples / sample_count, in [0,1]
    // Roadmap 10.13. The **duration** of the longest unbroken focused stretch, in seconds.
    //
    // This replaced a count of consecutive non-DISTRACTED prediction *rows*, which was shown
    // under the time-like label "Focus streak". Predictions arrive when input produces a
    // reading, not on a clock, so that number was not elapsed focus by any reading of it — and
    // two people doing identical work got different values purely from typing cadence. A row
    // count is not a duration and must never be labelled as one.
    std::uint64_t longest_focus_secs = 0;
};

// Aggregate predictions in the order given (**must be chronological**, oldest first — the
// duration below is measured between neighbours, so a reversed vector would measure the same
// intervals backwards and report zero). Empty input yields a zeroed summary, so callers can
// render "no data yet" without special-casing.
inline FocusSummary summarize_predictions(const std::vector<PredictionRecord>& preds) {
    FocusSummary s;
    s.sample_count = preds.size();
    if (preds.empty()) return s;

    double sum = 0.0;
    // Roadmap 10.13. The run is measured in time, so it needs the previous sample's clock as
    // well as its verdict. `nullopt` means "no run is open": either nothing has been seen yet,
    // or the last sample broke one.
    std::int64_t run_secs = 0;
    std::optional<std::int64_t> previous_secs;
    // A stretch of focus belongs to one session. Two sessions with a gap of seconds between
    // them are still two pieces of work, and merging them would report a stretch the user
    // never had.
    std::string previous_session;
    for (const auto& p : preds) {
        sum += p.focus_score;
        if (p.focus_score > s.peak_focus_score) s.peak_focus_score = p.focus_score;

        const auto now_secs = epoch_secs_from_rfc3339(p.timestamp);
        const bool new_session = p.session_id != previous_session;
        previous_session = p.session_id;
        if (p.focus_state == "DISTRACTED") {
            ++s.distracted_samples;
            run_secs = 0;
            previous_secs.reset();
            continue;
        }
        if (new_session) {
            run_secs = 0;
            previous_secs.reset();
        }

        // An unparseable timestamp cannot be measured against its neighbours, so it ends the
        // run rather than being folded in at an invented distance.
        if (!now_secs) {
            run_secs = 0;
            previous_secs.reset();
            continue;
        }
        if (previous_secs) {
            const auto gap = *now_secs - *previous_secs;
            // A backwards clock (DST, NTP) is not a negative amount of focus, and a gap past
            // the bound is a pause in the *user*, not in the data. Both start a fresh run.
            run_secs = (gap >= 0 && gap <= kFocusRunGapSecs) ? run_secs + gap : 0;
        }
        previous_secs = now_secs;
        s.longest_focus_secs =
            std::max(s.longest_focus_secs, static_cast<std::uint64_t>(run_secs));
    }
    s.avg_focus_score = sum / static_cast<double>(preds.size());
    s.distracted_fraction =
        static_cast<double>(s.distracted_samples) / static_cast<double>(preds.size());
    return s;
}

// Header-only like the rest of this file (matches summarize_predictions above); camelCase
// keys match the frontend's existing JSON convention (see PomodoroStatus::to_json).
inline void to_json(nlohmann::json& json, const FocusSummary& s) {
    json = nlohmann::json{{"sampleCount", s.sample_count},
                          {"avgFocusScore", s.avg_focus_score},
                          {"peakFocusScore", s.peak_focus_score},
                          {"distractedSamples", s.distracted_samples},
                          {"distractedFraction", s.distracted_fraction},
                          {"longestFocusSecs", s.longest_focus_secs}};
}

}  // namespace snapback
