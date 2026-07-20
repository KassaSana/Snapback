// Focus-state classifier. Rust: engine/classifier.rs.
#pragma once

#include <array>
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

    std::string backend() const;

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
// [DISTRACTED, PSEUDO_PRODUCTIVE, PRODUCTIVE, DEEP_FOCUS]. Rust: build_prediction_scores.
// Used by the ONNX backend; the heuristic backend has its own internal builder.
PredictionScores scores_from_probabilities(const std::array<double, 4>& probas, double thrash,
                                           double drift, double goal_alignment);

}  // namespace snapback
