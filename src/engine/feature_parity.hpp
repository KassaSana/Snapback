// Shared feature-parity replay helpers.
//
// These power the C++ side of the Rust-vs-C++ parity harness. They intentionally
// mirror the Rust `engine/parity.rs` helper: load scenario JSON, replay synthetic
// capture events through FeatureExtractor, and expose the model-training columns.
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/features.hpp"
#include "types.hpp"

namespace snapback {

struct FeatureParityResult {
    std::string name;
    std::unordered_map<std::string, double> features;
};

std::unordered_map<std::string, double> training_column_values(const FeatureVector& f);
FeatureVector replay_feature_parity_scenario(const nlohmann::json& scenario,
                                             const std::vector<AppRuleRecord>& rules);
std::vector<FeatureParityResult> replay_feature_parity_file(
    const std::filesystem::path& path,
    const std::vector<AppRuleRecord>& rules);
nlohmann::json feature_parity_results_to_json(const std::vector<FeatureParityResult>& results);

}  // namespace snapback
