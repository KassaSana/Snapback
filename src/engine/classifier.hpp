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

// The signals that come from the user's context rather than the model: rolling-window
// behavior (thrash/drift) and user configuration (Block/Allow rules, goal categories).
// Both backends must apply these — a model predicts a focus class, it does not know that
// the user marked this app as blocked.
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

// Combines raw model probabilities with the user's context signals into final scores.
//
// This exists as a free function so it is testable without SNAPBACK_ONNX compiled in. The
// bug it replaces was invisible precisely because it lived inside the ONNX-only branch:
// that branch passed thrash=0, drift=0, personal_block=false and dropped the goal/rules
// entirely, so **deploying a trained model silently disabled the user's Block rules** and
// pinned goal_alignment to 0.5.
PredictionScores blend_model_output(const std::array<double, 4>& probas,
                                    const ContextSignals& signals,
                                    FocusMode mode);

}  // namespace snapback
