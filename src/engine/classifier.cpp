#include "engine/classifier.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "engine/app_context.hpp"
#include "engine/classifier_tuning.hpp"
#include "engine/onnx_model.hpp"

namespace snapback {
namespace {

// ROADMAP 7.15: every literal that used to sit inline in this file now has a name and a
// role in engine/classifier_tuning.hpp. Read that file to understand the model; read this
// one to understand the arithmetic. The extraction changed no value — the feature-parity
// golden fixture is what proves that.
using namespace snapback::tuning;

double clamp(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

double thrash_score(const FeatureVector& f) {
    const double switches_30s =
        std::min(f.context_switches_30s() / scale::kSwitches30sFull, 1.0);
    const double switches_5min =
        std::min(f.context_switches_5min() / scale::kSwitches5minFull, 1.0);
    const double unique_apps =
        clamp((f.unique_apps_5min() - 1.0) / scale::kUniqueAppsSpan, 0.0, 1.0);
    return clamp(switches_30s * weight::kThrashSwitches30s +
                     switches_5min * weight::kThrashSwitches5min +
                     unique_apps * weight::kThrashUniqueApps,
                 0.0, 1.0);
}

double drift_score(const FeatureVector& f) {
    const double title_churn = f.window_title_changed_30s() > 0.5 ? 1.0 : 0.0;
    const double keystroke_chaos =
        f.keystroke_count() >= scale::kMinKeystrokesForChaos
            ? std::min(f.keystroke_interval_std() / scale::kKeystrokeChaosFull, 1.0)
            : 0.0;
    const double unsettled =
        std::min(f.context_switches_30s() / scale::kSwitchesUnsettledFull, 1.0);
    const double in_work_context =
        f.is_ide() > 0.5 || f.is_productivity() > 0.5
            ? shape::kDriftContextWork
            : (f.is_browser() > 0.5 || f.is_communication() > 0.5
                   ? shape::kDriftContextBrowserOrComms
                   : shape::kDriftContextUnknown);
    return clamp((title_churn * weight::kDriftTitleChurn +
                  keystroke_chaos * weight::kDriftKeystrokeChaos +
                  unsettled * weight::kDriftUnsettled) *
                     in_work_context,
                 0.0, 1.0);
}

double deep_work_score(const FeatureVector& f, double thrash, double drift) {
    const double settled = std::min(f.time_in_current_app() / scale::kSettledSeconds, 1.0);
    const double steady_typing =
        f.keystroke_count() >= scale::kMinKeystrokesForSteady
            ? std::max(0.0, 1.0 - std::min(f.keystroke_interval_std(), 1.0))
            : scale::kSteadyTypingUnknown;
    const double low_switch =
        std::max(0.0, 1.0 - std::min(f.context_switches_30s() / scale::kSwitchesLowFull, 1.0));
    const double stability = (1.0 - thrash) * (1.0 - drift);
    const double momentum = clamp(f.focus_momentum(), 0.0, 1.0);
    return clamp(settled * weight::kDeepSettled + steady_typing * weight::kDeepSteadyTyping +
                     low_switch * weight::kDeepLowSwitch + stability * weight::kDeepStability +
                     momentum * weight::kDeepMomentum,
                 0.0, 1.0);
}

PredictionScores scores_from_probas(const std::array<double, 4>& raw,
                                    double thrash,
                                    double drift,
                                    double goal_alignment) {
    const double total = raw[0] + raw[1] + raw[2] + raw[3];
    std::array<double, 4> p = raw;
    if (total <= 0.0) {
        p = {0.25, 0.25, 0.25, 0.25};
    } else {
        for (double& v : p) v /= total;
    }

    PredictionScores out;
    out.distraction_risk = clamp(p[0] + thrash * shape::kThrashRiskBump, 0.0, 1.0);
    for (std::size_t i = 0; i < p.size(); ++i) out.focus_score += kFocusLevels[i] * p[i];
    const auto best = std::max_element(p.begin(), p.end());
    out.focus_state = kStateLabels[static_cast<std::size_t>(std::distance(p.begin(), best))];
    out.thrash_score = thrash;
    out.drift_score = drift;
    out.goal_alignment = goal_alignment;
    return out;
}

}  // namespace

PredictionScores Classifier::predict(const FeatureVector& features, FocusMode mode) const {
    return predict(features, mode, std::nullopt, {});
}

PredictionScores Classifier::predict(const FeatureVector& features,
                                     FocusMode mode,
                                     const std::optional<std::string>& session_goal,
                                     const std::vector<AppRuleRecord>& rules) const {
    // Delegate rather than duplicate the backend selection — keeping two copies is how the
    // ONNX branch drifted out of sync with the heuristic one in the first place.
    return predict(features, mode, session_goal, rules, {});
}

PredictionScores Classifier::predict(const FeatureVector& features,
                                     FocusMode mode,
                                     const std::optional<std::string>& session_goal,
                                     const std::vector<AppRuleRecord>& rules,
                                     const std::vector<GoalCategory>& categories) const {
#if defined(SNAPBACK_ONNX)
    // The model supplies class probabilities; the user's Block/Allow rules, goal alignment,
    // and thrash/drift come from here and apply to both backends. Previously this branch
    // passed zeros and `false`, so a deployed model silently ignored user configuration.
    if (auto probas = OnnxModel::instance().infer_probabilities(features)) {
        return blend_model_output(
            *probas, compute_context_signals(features, session_goal, rules, categories), mode);
    }
#endif
    return predict_heuristic(features, mode, session_goal, rules, categories);
}

std::string Classifier::backend() const {
#if defined(SNAPBACK_ONNX)
    if (OnnxModel::instance().loaded()) return "onnx";
#endif
    return "heuristic";
}

std::string Classifier::model_id() const {
#if defined(SNAPBACK_ONNX)
    if (const auto& identity = OnnxModel::instance().model_id()) return *identity;
#endif
    return "heuristic:" + std::string(kFeatureContractId);
}

ContextSignals compute_context_signals(const FeatureVector& f,
                                       const std::optional<std::string>& session_goal,
                                       const std::vector<AppRuleRecord>& rules,
                                       const std::vector<GoalCategory>& categories) {
    const auto ctx = classify_app_context(f.app_name, f.window_title, rules);
    const double alignment =
        snapback::goal_alignment_score(session_goal, ctx, f.window_title, categories);
    const double bias = alignment - shape::kGoalBiasMidpoint;
    ContextSignals out;
    out.thrash = thrash_score(f);
    out.drift = clamp(drift_score(f) - bias * shape::kGoalBiasOnDrift, 0.0, 1.0);
    out.goal_alignment = alignment;
    out.personal_block = ctx.personal_block;
    return out;
}

PredictionScores blend_model_output(const std::array<double, 4>& probas,
                                    const ContextSignals& signals,
                                    FocusMode mode) {
    auto scores = scores_from_probabilities(probas, signals.thrash, signals.drift,
                                            signals.goal_alignment);
    return apply_focus_guardrails(scores, signals.thrash, signals.drift, signals.personal_block,
                                  mode);
}

PredictionScores Classifier::predict_heuristic(const FeatureVector& f,
                                               FocusMode mode,
                                               const std::optional<std::string>& session_goal,
                                               const std::vector<AppRuleRecord>& rules,
                                               const std::vector<GoalCategory>& categories) const {
    const auto ctx = classify_app_context(f.app_name, f.window_title, rules);
    const double alignment = snapback::goal_alignment_score(session_goal, ctx, f.window_title, categories);
    const double bias = alignment - shape::kGoalBiasMidpoint;

    const double thrash = thrash_score(f);
    const double drift = clamp(drift_score(f) - bias * shape::kGoalBiasOnDrift, 0.0, 1.0);
    const double deep = deep_work_score(f, thrash, drift);

    const double is_entertainment = (ctx.is_entertainment || ctx.personal_block) ? 1.0 : 0.0;
    const double is_communication = (ctx.is_communication && !ctx.personal_allow) ? 1.0 : 0.0;
    const double in_work_app = ((ctx.is_ide || ctx.is_productivity) && !ctx.personal_block) ? 1.0 : 0.0;

    double distracted = 0.0;
    distracted += thrash * weight::kDistractedThrash;
    distracted += std::min(f.idle_time_30s() / scale::kIdleFull, 1.0) * weight::kDistractedIdle;
    distracted += (1.0 - std::min(f.keystroke_rate() / scale::kKeystrokeRateFull, 1.0)) *
                  weight::kDistractedLowKeystrokes;
    distracted += (1.0 - std::min(f.time_in_current_app() / scale::kJustArrivedSeconds, 1.0)) *
                  weight::kDistractedJustArrived;
    distracted += is_entertainment * weight::kDistractedEntertainment;
    distracted += is_communication * weight::kDistractedCommunication;
    distracted -= in_work_app * weight::kDistractedWorkAppCredit;
    distracted = clamp(distracted - bias * shape::kGoalBiasOnDistracted, 0.0, 1.0);

    const double pseudo =
        clamp(drift * (1.0 - distracted * shape::kPseudoDistractedDiscount), 0.0, 1.0);
    const double remaining = std::max(0.0, 1.0 - distracted - pseudo);
    const double productive = remaining * (1.0 - deep * shape::kDeepShareOfRemaining);
    const double deep_prob = std::max(0.0, remaining * deep * shape::kDeepShareOfRemaining);

    auto scores = scores_from_probas({distracted, pseudo, productive, deep_prob},
                                     thrash, drift, alignment);
    return apply_focus_guardrails(scores, thrash, drift, ctx.personal_block, mode);
}

PredictionScores scores_from_probabilities(const std::array<double, 4>& probas, double thrash,
                                           double drift, double goal_alignment) {
    return scores_from_probas(probas, thrash, drift, goal_alignment);
}

PredictionScores apply_focus_guardrails(PredictionScores scores,
                                        double thrash,
                                        double drift,
                                        bool personal_block,
                                        FocusMode mode) {
    if (scores.distraction_risk >= risk_threshold(mode) ||
        thrash >= tuning::policy::kThrashDistracted ||
        personal_block) {
        scores.focus_state = "DISTRACTED";
    } else if (drift >= tuning::policy::kDriftPseudo && scores.focus_state != "DEEP_FOCUS") {
        scores.focus_state = "PSEUDO_PRODUCTIVE";
    }
    return scores;
}

}  // namespace snapback
