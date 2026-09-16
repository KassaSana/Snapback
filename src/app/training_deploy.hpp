#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.hpp"
#include "util/subprocess.hpp"

namespace snapback::training_deploy {

std::filesystem::path export_dir(const std::filesystem::path& app_data_dir);

struct ModelDeploymentRecoveryOutcome {
    bool ok{};
    std::string message;
    std::vector<std::string> preserved_paths;
    bool retry_cleanup_available{};
    bool rollback_available{};
};

// Complete or roll back an interrupted model deployment before resolving the live model.
// Throws on failure — for user-initiated paths that must observe a hard error.
void recover_model_deployment(const std::filesystem::path& app_data_dir);

// Roadmap 13.8. Startup-safe recovery: never throws, preserves live models, and reports what
// the user can retry or roll back.
ModelDeploymentRecoveryOutcome recover_model_deployment_for_startup(
    const std::filesystem::path& app_data_dir);
ModelDeploymentRecoveryOutcome retry_model_deployment_cleanup(
    const std::filesystem::path& app_data_dir);
ModelDeploymentHealth to_model_deployment_health(
    const ModelDeploymentRecoveryOutcome& outcome);
bool rollback_available(const std::filesystem::path& app_data_dir);
// Restore the previous deployed model and its quality metadata. The swap keeps the current
// model as the next rollback target, so a user can undo an undo.
nlohmann::json rollback_model(const std::filesystem::path& app_data_dir);
bool is_training_repo(const std::filesystem::path& path);
std::optional<std::filesystem::path> read_training_repo_path(
    const std::filesystem::path& app_data_dir);
void write_training_repo_path(const std::filesystem::path& app_data_dir,
                              const std::filesystem::path& repo_path);

struct ModelQualityDecision {
    bool accepted{};
    std::string metric;
    double candidate_score{};
    double threshold{};
    std::string reason;
};

// Promote a candidate model and its accepted quality baseline as one recoverable pair.
// Both artifacts are staged before the live deployment is changed.
bool deploy_model_candidate(const std::filesystem::path& app_data_dir,
                            const std::filesystem::path& candidate_model,
                            const ModelQualityDecision& quality);

// Gate a candidate model before it can replace the deployed model. The candidate metrics must
// contain a held-out/validation/CV accuracy; in-sample-only metrics are deliberately rejected.
ModelQualityDecision evaluate_model_quality(
    const nlohmann::json& candidate_metrics,
    const std::optional<nlohmann::json>& deployed_quality = std::nullopt);

nlohmann::json training_deploy_status(const std::filesystem::path& app_data_dir);

// Run the training pipeline to completion, or until `should_cancel` answers true, and report
// the outcome. Blocks for the length of the run: callers put it on a worker, never the UI
// thread. A cancelled run reports `cancelled: true` and deploys nothing. With no predicate
// the run cannot be interrupted.
nlohmann::json train_from_export(const std::filesystem::path& app_data_dir,
                                 const subprocess::CancelPredicate& should_cancel = {});
std::string build_pipeline_command(const std::filesystem::path& output_dir);

namespace detail {

// Quote a single argument for the platform shell so its contents can never be interpreted
// as syntax. This guards `pipelineCommand`, the copy-and-paste string the user runs in their
// own shell. The app's own training run does not go through a shell at all (see
// training_spawn_request), so this is a display concern, not the execution boundary.
std::string shell_quote(const std::string& value);

// The process the app starts for a training run, as an argv array rather than a command
// line. Exposed so a test can pin the property that made std::system unsafe here: the repo
// path is one argv element, whatever characters it contains.
subprocess::SpawnRequest training_spawn_request(const std::filesystem::path& repo_path,
                                                const std::vector<std::string>& python_argv,
                                                const std::filesystem::path& output_dir,
                                                const std::filesystem::path& log_path);

}  // namespace detail

}  // namespace snapback::training_deploy
