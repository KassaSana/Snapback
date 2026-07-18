// Confidence calibration for distraction nags. Roadmap 2.4.
//
// The classifier emits a distraction_risk in [0,100]. Firing a "you're distracted" nudge
// the instant risk crosses a hard line makes the app twitchy near the boundary, where the
// model is least sure. This turns raw risk into a [0,1] confidence and only nags when the
// call is both over-threshold AND confident, so borderline blips stay quiet.
//
// Pure functions, no state: trivially testable and safe to call on the hot path.
#pragma once

#include <algorithm>

namespace snapback {

struct ConfidenceConfig {
    double nag_threshold = 60.0;    // below this risk we never nag
    double min_confidence = 0.25;   // and above it, only once we're this sure
};

// Confidence that a nag is warranted, in [0,1]: 0 at the threshold, ramping to 1 at max
// risk. Below the threshold it's 0 (no case to make). Linear is deliberate — it's a
// monotonic, explainable mapping, not a calibrated probability.
inline double distraction_confidence(double distraction_risk,
                                     const ConfidenceConfig& cfg = {}) {
    const double span = 100.0 - cfg.nag_threshold;
    if (span <= 0.0) return distraction_risk >= cfg.nag_threshold ? 1.0 : 0.0;
    const double over = (distraction_risk - cfg.nag_threshold) / span;
    return std::clamp(over, 0.0, 1.0);
}

// True only when risk clears the threshold AND we're confident enough — the gate that
// suppresses low-confidence nags.
inline bool should_nag(double distraction_risk, const ConfidenceConfig& cfg = {}) {
    return distraction_risk >= cfg.nag_threshold &&
           distraction_confidence(distraction_risk, cfg) >= cfg.min_confidence;
}

}  // namespace snapback
