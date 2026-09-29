// The heuristic classifier's tunable constants, grouped by role:
//
//   `Policy`  what counts as distracted.
//   `Scale`   normalisers from raw counts/durations to 0..1 (claims about behaviour).
//   `Weight`  linear-blend coefficients -- the part a trained model replaces.
//   `Shape`   how the class scores are carved from one another; not independent dials.
//
// The values are hand-tuned and have never been validated against labelled data.
#pragma once

#include <array>

namespace snapback::tuning {

// Exact floating-point comparison is unusable for a sum of literals, so the convex-combination
// checks below allow a small epsilon. constexpr because these are static_asserts.
constexpr double abs_diff(double a, double b) { return a > b ? a - b : b - a; }
constexpr bool sums_to_one(double sum) { return abs_diff(sum, 1.0) < 1e-9; }

// --- Policy: what counts as distracted ------------------------------------------------
//
// The mode-dependent risk thresholds live in types.hpp (risk_threshold()), as part of the
// FocusMode contract.
namespace policy {

// Thrash at or above this forces DISTRACTED regardless of the model.
constexpr double kThrashDistracted = 0.75;

// Drift at or above this demotes PRODUCTIVE (only) to PSEUDO_PRODUCTIVE (ADR-0004).
constexpr double kDriftPseudo = 0.55;

}  // namespace policy

// --- Scale: raw units -> 0..1 -----------------------------------------------------------
//
// The value at which each signal saturates.
namespace scale {

// Context switches. Three different windows saturate at three different counts, and nothing
// records why they differ.
constexpr double kSwitches30sFull = 4.0;    // thrash_score
constexpr double kSwitches5minFull = 10.0;  // thrash_score
constexpr double kSwitchesUnsettledFull = 3.0;  // drift_score, a tighter bar than thrash's
constexpr double kSwitchesLowFull = 2.0;    // deep_work_score, tighter still

// Distinct apps in 5 minutes, minus one (being in a single app is not variety).
constexpr double kUniqueAppsSpan = 5.0;

// Keystroke interval standard deviation, in seconds, at which typing reads as chaotic.
constexpr double kKeystrokeChaosFull = 1.2;

// Seconds in the current app before it counts as fully settled.
constexpr double kSettledSeconds = 180.0;   // deep_work_score
constexpr double kJustArrivedSeconds = 120.0;  // distracted accumulator

// Idle seconds within a 30s window at which idleness reads as fully distracting.
constexpr double kIdleFull = 8.0;

// Keystrokes per second at which input reads as fully active.
constexpr double kKeystrokeRateFull = 4.0;

// Minimum keystrokes before interval statistics mean anything. Below these counts the
// standard deviation is noise, so the score falls back to a fixed value rather than trusting
// a sample of two.
constexpr double kMinKeystrokesForChaos = 3.0;   // drift_score -> contributes 0.0
constexpr double kMinKeystrokesForSteady = 4.0;  // deep_work_score -> kSteadyTypingUnknown

// What "steady typing" scores when there is not enough input to judge. Below the midpoint,
// so insufficient evidence leans against claiming deep work.
constexpr double kSteadyTypingUnknown = 0.3;

}  // namespace scale

// --- Weight: the linear blends ----------------------------------------------------------
//
// Groups that must be convex combinations are static_asserted to sum to 1.
namespace weight {

// thrash_score: how much of "thrashing" is short-window switching vs long-window vs variety.
constexpr double kThrashSwitches30s = 0.45;
constexpr double kThrashSwitches5min = 0.25;
constexpr double kThrashUniqueApps = 0.30;
static_assert(sums_to_one(kThrashSwitches30s + kThrashSwitches5min + kThrashUniqueApps),
              "thrash_score weights must be a convex combination");

// drift_score: title churn dominates, because a changing title inside one app is the
// signature of browsing rather than working.
constexpr double kDriftTitleChurn = 0.45;
constexpr double kDriftKeystrokeChaos = 0.30;
constexpr double kDriftUnsettled = 0.25;
static_assert(sums_to_one(kDriftTitleChurn + kDriftKeystrokeChaos + kDriftUnsettled),
              "drift_score weights must be a convex combination");

// deep_work_score.
constexpr double kDeepSettled = 0.30;
constexpr double kDeepSteadyTyping = 0.20;
constexpr double kDeepLowSwitch = 0.15;
constexpr double kDeepStability = 0.20;
constexpr double kDeepMomentum = 0.15;
static_assert(sums_to_one(kDeepSettled + kDeepSteadyTyping + kDeepLowSwitch + kDeepStability +
                          kDeepMomentum),
              "deep_work_score weights must be a convex combination");

// The distracted accumulator: not convex (sums to 0.80 with a negative term, then clamped), so
// behaviour alone cannot reach 1.0. Preserved because changing it changes every stored
// prediction's meaning.
constexpr double kDistractedThrash = 0.30;
constexpr double kDistractedIdle = 0.15;
constexpr double kDistractedLowKeystrokes = 0.10;
constexpr double kDistractedJustArrived = 0.10;
constexpr double kDistractedEntertainment = 0.20;
constexpr double kDistractedCommunication = 0.05;
constexpr double kDistractedWorkAppCredit = 0.10;  // subtracted

}  // namespace weight

// --- Shape: how the four classes are carved out of each other ---------------------------
namespace shape {

// drift_score multiplier by app context. Being in an IDE or a productivity app does not
// exempt you from drift; being somewhere uncategorised discounts it heavily, because we have
// no idea what the app is.
constexpr double kDriftContextWork = 1.0;           // IDE or productivity
constexpr double kDriftContextBrowserOrComms = 0.85;
constexpr double kDriftContextUnknown = 0.4;

// Goal alignment biases drift and distraction around the 0.5 midpoint.
constexpr double kGoalBiasMidpoint = 0.5;
constexpr double kGoalBiasOnDrift = 0.25;
constexpr double kGoalBiasOnDistracted = 0.35;

// How much thrash adds to distraction_risk on top of the model's DISTRACTED probability --
// evidence feeding the estimate, not a policy override (ADR-0004).
constexpr double kThrashRiskBump = 0.15;

// Pseudo-productive is drift that distraction has not already claimed.
constexpr double kPseudoDistractedDiscount = 0.6;

// Mass left after distracted and pseudo splits between PRODUCTIVE and DEEP_FOCUS by the
// deep-work score (deep takes 0.65 * deep_work of it).
constexpr double kDeepShareOfRemaining = 0.65;

}  // namespace shape

// --- Output mapping ---------------------------------------------------------------------

// Class order everywhere: DISTRACTED, PSEUDO_PRODUCTIVE, PRODUCTIVE, DEEP_FOCUS. This order
// is a contract with the model and with the stored `focus_state` strings.
inline constexpr std::array<const char*, 4> kStateLabels = {"DISTRACTED", "PSEUDO_PRODUCTIVE",
                                                            "PRODUCTIVE", "DEEP_FOCUS"};

// focus_score is the probability-weighted average of these levels, so it can be high while a
// guardrail overrides the state to DISTRACTED: the score is the model's opinion, the state the
// policy verdict, and `state_source` records which rule decided (ADR-0004).
inline constexpr std::array<double, 4> kFocusLevels = {25.0, 50.0, 75.0, 100.0};

}  // namespace snapback::tuning
