// The heuristic classifier's tunable constants, named and sorted by what they are.
//
// ROADMAP 7.15. `classifier.cpp` carried roughly twenty bare literals — `0.45`, `4.0`,
// `180.0`, `0.65` — with only two of them named. That is not merely untidy. Two open items
// cannot be answered while the numbers are anonymous:
//
//   * **1.2** asks what a user-facing "distraction sensitivity" control should tune. That is
//     unanswerable until someone can say which numbers are *policy* (a judgement call a user
//     might reasonably want to move) and which are *structural* (change them and the score
//     stops meaning what it meant).
//   * **2.3** replaces some of these with a trained model and keeps others as the blend layer
//     around it. Which is which was tribal knowledge living in one commit message.
//
// So the organising principle here is **role, not location**. Each constant below is one of:
//
//   `Policy`      — a decision about what counts as distracted. Survives 2.3: a model
//                   predicts a focus class, it does not decide that thrashing means the user
//                   is distracted. This group is 1.2's answer space.
//   `Scale`       — a normaliser turning a raw count or duration into 0..1. Encodes a claim
//                   about human behaviour ("four app switches in 30s is as thrashy as it
//                   gets"). Survives 2.3 only as feature engineering.
//   `Weight`      — the coefficients of a linear blend. **This is what 2.3 replaces.** A
//                   trained model learns these; nothing else in this file is learnable.
//   `Shape`       — how the four class scores are carved out of one another, and how goal
//                   alignment bends them. Structural: these are not independent dials, and
//                   moving one silently changes what the others mean.
//
// **On provenance: there is none, and that is the finding.** Every value here arrived in the
// port commit (`1cffcb9 refactor: C++`) with no derivation, no fitting procedure, and no
// recorded rationale — git history goes no further back for this file. They are hand-tuned
// numbers that have never been validated against labelled data. The comments below say what
// each value *does*; where a comment would have to invent why the value is what it is, it
// says so instead. Naming them does not make them right, it makes them arguable.
#pragma once

#include <array>

namespace snapback::tuning {

// Exact floating-point comparison is unusable for a sum of literals, so the convex-combination
// checks below allow a small epsilon. constexpr because these are static_asserts.
constexpr double abs_diff(double a, double b) { return a > b ? a - b : b - a; }
constexpr bool sums_to_one(double sum) { return abs_diff(sum, 1.0) < 1e-9; }

// --- Policy: what counts as distracted ------------------------------------------------
//
// The mode-dependent risk thresholds are deliberately NOT here — `risk_threshold()` lives in
// `types.hpp` because it is part of the FocusMode contract rather than the classifier's
// internals, and storage.cpp reasons about it too. 1.2 will need to consider both together.
namespace policy {

// Thrash at or above this forces DISTRACTED regardless of the model's opinion. Note 0.3 is
// the ceiling thrash can reach without context-switch counts, which is what made two focus
// states unreachable in 0.3 — the threshold was never the problem, its inputs were.
constexpr double kThrashDistracted = 0.75;

// Drift at or above this downgrades to PSEUDO_PRODUCTIVE, unless the state is DEEP_FOCUS.
constexpr double kDriftPseudo = 0.55;

}  // namespace policy

// --- Scale: raw units -> 0..1 -----------------------------------------------------------
//
// Each of these is the value at which a signal is treated as fully saturated. They are
// claims about behaviour, and every one is a guess.
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

// Seconds in the current app before it counts as fully "settled" / no longer "just arrived".
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
// These are the learnable parameters. Each group that must be a convex combination is
// static_asserted to sum to 1, which is the property that keeps its score in 0..1 without
// relying on the clamp — and which a future edit to a single weight would otherwise break
// silently, rescaling the score rather than reweighting it.
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

// The distracted accumulator. Deliberately NOT a convex combination: it sums to 0.80 with a
// negative term, then gets clamped. So a maximally-distracted-looking sample cannot reach
// 1.0 from behaviour alone — entertainment context and goal misalignment carry the rest.
// Whether that ceiling is intentional is unknown; it is preserved here because changing it
// changes every stored prediction's meaning.
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

// Goal alignment enters as a bias around the 0.5 midpoint: aligned work pushes drift and
// distraction down, misaligned work pushes them up. The two multipliers differ and nothing
// records why distraction should be more goal-sensitive than drift.
constexpr double kGoalBiasMidpoint = 0.5;
constexpr double kGoalBiasOnDrift = 0.25;
constexpr double kGoalBiasOnDistracted = 0.35;

// How much thrash adds to distraction_risk on top of the model's DISTRACTED probability.
// This is the one place a context signal edits a *score* rather than a state, which is part
// of what 7.7 is about.
constexpr double kThrashRiskBump = 0.15;

// Pseudo-productive is drift that distraction has not already claimed.
constexpr double kPseudoDistractedDiscount = 0.6;

// Whatever probability mass is left after distracted and pseudo splits between PRODUCTIVE
// and DEEP_FOCUS by the deep-work score. The same constant on both sides is what makes it a
// split rather than two independent scores: deep takes 0.65 * deep_work of the remainder and
// productive takes the rest.
constexpr double kDeepShareOfRemaining = 0.65;

}  // namespace shape

// --- Output mapping ---------------------------------------------------------------------

// Class order everywhere: DISTRACTED, PSEUDO_PRODUCTIVE, PRODUCTIVE, DEEP_FOCUS. This order
// is a contract with the model and with the stored `focus_state` strings.
inline constexpr std::array<const char*, 4> kStateLabels = {"DISTRACTED", "PSEUDO_PRODUCTIVE",
                                                            "PRODUCTIVE", "DEEP_FOCUS"};

// focus_score is the probability-weighted average of these per-class levels, which is why a
// confidently-DEEP_FOCUS row can carry a high score while guardrails have overridden the
// state to DISTRACTED. That contradiction is 7.7, and it is a consequence of this mapping.
inline constexpr std::array<double, 4> kFocusLevels = {25.0, 50.0, 75.0, 100.0};

}  // namespace snapback::tuning
