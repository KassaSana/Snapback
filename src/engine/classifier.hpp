// Focus-state classifier.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/features.hpp"
#include "types.hpp"

namespace snapback {

struct PredictionScores {
    double focus_score{};
    double distraction_risk{};
    double thrash_score{};
    double drift_score{};
    double goal_alignment{0.5};
    std::string focus_state;
    // Which rule decided focus_state (ADR-0004): "model", or "risk", "thrash", "block",
    // "drift".
    std::string state_source{"model"};
};

class Classifier {
public:
    PredictionScores predict(const FeatureVector& features, FocusMode mode) const;
    PredictionScores predict(const FeatureVector& features,
                             FocusMode mode,
                             const std::optional<std::string>& session_goal,
                             const std::vector<AppRuleRecord>& rules) const;
    PredictionScores predict(const FeatureVector& features,
                             FocusMode mode,
                             const std::optional<std::string>& session_goal,
                             const std::vector<AppRuleRecord>& rules,
                             const std::vector<GoalCategory>& categories) const;

    // The backend that produced the most recent prediction: "onnx" only while a model is
    // loaded *and* its last inference succeeded. See OnnxModel::last_inference_failed.
    std::string backend() const;
    std::string model_id() const;
    // True while a model is loaded but its last inference failed, so predictions are
    // coming from the heuristic. Always false without the ONNX build.
    bool inference_degraded() const;
    // Count of failed or rejected inferences since the model was loaded.
    std::uint64_t inference_failures() const;

private:
    PredictionScores predict_heuristic(const FeatureVector& features,
                                       FocusMode mode,
                                       const std::optional<std::string>& session_goal,
                                       const std::vector<AppRuleRecord>& rules,
                                       const std::vector<GoalCategory>& categories) const;
};

PredictionScores apply_focus_guardrails(PredictionScores scores,
                                        double thrash,
                                        double drift,
                                        bool personal_block,
                                        FocusMode mode);

// Builds PredictionScores from a model's 4 class probabilities
// [DISTRACTED, PSEUDO_PRODUCTIVE, PRODUCTIVE, DEEP_FOCUS].
// Used by the ONNX backend; the heuristic backend has its own internal builder.
PredictionScores scores_from_probabilities(const std::array<double, 4>& probas, double thrash,
                                           double drift, double goal_alignment);

// Signals from the user's context rather than the model (thrash/drift, Block/Allow rules, goal
// categories). Both backends apply them.
struct ContextSignals {
    double thrash{};
    double drift{};
    double goal_alignment{0.5};
    bool personal_block{};
};

ContextSignals compute_context_signals(const FeatureVector& features,
                                       const std::optional<std::string>& session_goal,
                                       const std::vector<AppRuleRecord>& rules,
                                       const std::vector<GoalCategory>& categories);

// Combines model probabilities with the user's context signals. A free function so it is
// testable without SNAPBACK_ONNX.
PredictionScores blend_model_output(const std::array<double, 4>& probas,
                                    const ContextSignals& signals,
                                    FocusMode mode);

}  // namespace snapback
