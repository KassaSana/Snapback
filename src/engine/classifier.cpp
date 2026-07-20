#include "engine/classifier.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "engine/app_context.hpp"
#include "engine/onnx_model.hpp"

namespace snapback {
namespace {

constexpr std::array<const char*, 4> kStateLabels = {
    "DISTRACTED", "PSEUDO_PRODUCTIVE", "PRODUCTIVE", "DEEP_FOCUS"};
constexpr std::array<double, 4> kFocusLevels = {25.0, 50.0, 75.0, 100.0};
constexpr double kThrashDistractedThreshold = 0.75;
constexpr double kDriftPseudoThreshold = 0.55;

double clamp(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

double thrash_score(const FeatureVector& f) {
    const double switches_30s = std::min(f.context_switches_30s() / 4.0, 1.0);
    const double switches_5min = std::min(f.context_switches_5min() / 10.0, 1.0);
    const double unique_apps = clamp((f.unique_apps_5min() - 1.0) / 5.0, 0.0, 1.0);
    return clamp(switches_30s * 0.45 + switches_5min * 0.25 + unique_apps * 0.30, 0.0, 1.0);
}

double drift_score(const FeatureVector& f) {
    const double title_churn = f.window_title_changed_30s() > 0.5 ? 1.0 : 0.0;
    const double keystroke_chaos = f.keystroke_count() >= 3.0
                                       ? std::min(f.keystroke_interval_std() / 1.2, 1.0)
                                       : 0.0;
    const double unsettled = std::min(f.context_switches_30s() / 3.0, 1.0);
    const double in_work_context = f.is_ide() > 0.5 || f.is_productivity() > 0.5
                                       ? 1.0
                                       : (f.is_browser() > 0.5 || f.is_communication() > 0.5 ? 0.85 : 0.4);
    return clamp((title_churn * 0.45 + keystroke_chaos * 0.30 + unsettled * 0.25) *
                     in_work_context,
                 0.0, 1.0);
}

double deep_work_score(const FeatureVector& f, double thrash, double drift) {
    const double settled = std::min(f.time_in_current_app() / 180.0, 1.0);
    const double steady_typing = f.keystroke_count() >= 4.0
                                     ? std::max(0.0, 1.0 - std::min(f.keystroke_interval_std(), 1.0))
                                     : 0.3;
    const double low_switch = std::max(0.0, 1.0 - std::min(f.context_switches_30s() / 2.0, 1.0));
    const double stability = (1.0 - thrash) * (1.0 - drift);
    const double momentum = clamp(f.focus_momentum(), 0.0, 1.0);
    return clamp(settled * 0.30 + steady_typing * 0.20 + low_switch * 0.15 +
                     stability * 0.20 + momentum * 0.15,
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
    out.distraction_risk = clamp(p[0] + thrash * 0.15, 0.0, 1.0);
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
#if defined(SNAPBACK_ONNX)
    if (OnnxModel::instance().loaded()) {
        return apply_focus_guardrails(OnnxModel::instance().run(features), 0.0, 0.0, false, mode);
    }
#endif
    return predict_heuristic(features, mode, session_goal, rules, {});
}

PredictionScores Classifier::predict(const FeatureVector& features,
                                     FocusMode mode,
                                     const std::optional<std::string>& session_goal,
                                     const std::vector<AppRuleRecord>& rules,
                                     const std::vector<GoalCategory>& categories) const {
#if defined(SNAPBACK_ONNX)
    if (OnnxModel::instance().loaded()) {
        return apply_focus_guardrails(OnnxModel::instance().run(features), 0.0, 0.0, false, mode);
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

PredictionScores Classifier::predict_heuristic(const FeatureVector& f,
                                               FocusMode mode,
                                               const std::optional<std::string>& session_goal,
                                               const std::vector<AppRuleRecord>& rules,
                                               const std::vector<GoalCategory>& categories) const {
    const auto ctx = classify_app_context(f.app_name, f.window_title, rules);
    const double alignment = snapback::goal_alignment_score(session_goal, ctx, f.window_title, categories);
    const double bias = alignment - 0.5;

    const double thrash = thrash_score(f);
    const double drift = clamp(drift_score(f) - bias * 0.25, 0.0, 1.0);
    const double deep = deep_work_score(f, thrash, drift);

    const double is_entertainment = (ctx.is_entertainment || ctx.personal_block) ? 1.0 : 0.0;
    const double is_communication = (ctx.is_communication && !ctx.personal_allow) ? 1.0 : 0.0;
    const double in_work_app = ((ctx.is_ide || ctx.is_productivity) && !ctx.personal_block) ? 1.0 : 0.0;

    double distracted = 0.0;
    distracted += thrash * 0.30;
    distracted += std::min(f.idle_time_30s() / 8.0, 1.0) * 0.15;
    distracted += (1.0 - std::min(f.keystroke_rate() / 4.0, 1.0)) * 0.10;
    distracted += (1.0 - std::min(f.time_in_current_app() / 120.0, 1.0)) * 0.10;
    distracted += is_entertainment * 0.20;
    distracted += is_communication * 0.05;
    distracted -= in_work_app * 0.10;
    distracted = clamp(distracted - bias * 0.35, 0.0, 1.0);

    const double pseudo = clamp(drift * (1.0 - distracted * 0.6), 0.0, 1.0);
    const double remaining = std::max(0.0, 1.0 - distracted - pseudo);
    const double productive = remaining * (1.0 - deep * 0.65);
    const double deep_prob = std::max(0.0, remaining * deep * 0.65);

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
        thrash >= kThrashDistractedThreshold ||
        personal_block) {
        scores.focus_state = "DISTRACTED";
    } else if (drift >= kDriftPseudoThreshold && scores.focus_state != "DEEP_FOCUS") {
        scores.focus_state = "PSEUDO_PRODUCTIVE";
    }
    return scores;
}

}  // namespace snapback
