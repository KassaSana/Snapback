#include "doctest_wrapper.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "app/training_deploy.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_training_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("training_deploy detects Snapback repo by pipeline_cli") {
    TempDir temp;

    CHECK_FALSE(training_deploy::is_training_repo(temp.path));
    write_text(temp.path / "ml" / "pipeline_cli.py", "# test\n");
    CHECK(training_deploy::is_training_repo(temp.path));
}

TEST_CASE("training_deploy stores and reads configured repo path") {
    TempDir app_data;
    TempDir repo;
    write_text(repo.path / "ml" / "pipeline_cli.py", "# test\n");

    training_deploy::write_training_repo_path(app_data.path, repo.path);
    auto configured = training_deploy::read_training_repo_path(app_data.path);

    REQUIRE(configured.has_value());
    CHECK(*configured == repo.path);
}

TEST_CASE("training_deploy status counts export rows labels metrics and model") {
    TempDir app_data;
    const auto out_dir = training_deploy::export_dir(app_data.path);
    write_text(out_dir / "features.csv", "timestamp,feature\n1,2\n3,4\n");
    write_text(out_dir / "labels.csv",
               "timestamp,label,source,session_id,notes\n"
               "1,-1,manual,s1,\n"
               "2,1,manual,s1,\n"
               "3,1,manual,s1,\n"
               "4,2,manual,s1,\n");
    write_text(out_dir / "metrics.json",
               "{\"cv_accuracy\":0.75,\"ignored\":\"text\",\"recall_distracted\":0.5}");
    write_text(out_dir / "model.onnx", "fake");

    auto status = training_deploy::training_deploy_status(app_data.path);

    CHECK(status.at("exportDir").get<std::string>() == out_dir.string());
    CHECK(status.at("featureCount").get<std::uint64_t>() == 2);
    CHECK(status.at("labelCount").get<std::uint64_t>() == 4);
    CHECK(status.at("hasExport").get<bool>());
    CHECK(status.at("modelOnnxExists").get<bool>());
    CHECK(status.at("metricsExists").get<bool>());
    CHECK(status.at("labelBreakdown").at("DISTRACTED").get<int>() == 1);
    CHECK(status.at("labelBreakdown").at("PRODUCTIVE").get<int>() == 2);
    CHECK(status.at("labelBreakdown").at("DEEP_FOCUS").get<int>() == 1);
    CHECK(status.at("metrics").at("cv_accuracy").get<double>() == doctest::Approx(0.75));
    CHECK(status.at("metrics").contains("ignored") == false);
    CHECK(status.at("qualityGate").at("passed").get<bool>());
    CHECK_FALSE(status.at("rollbackAvailable").get<bool>());
}

TEST_CASE("model quality gate requires held-out quality and protects the deployed baseline") {
    const auto accepted = training_deploy::evaluate_model_quality(
        nlohmann::json{{"held_out_accuracy", 0.72}}, std::nullopt);
    CHECK(accepted.accepted);
    CHECK(accepted.metric == "held_out_accuracy");
    CHECK(accepted.threshold == doctest::Approx(0.60));

    const auto too_low = training_deploy::evaluate_model_quality(
        nlohmann::json{{"held_out_accuracy", 0.59}}, std::nullopt);
    CHECK_FALSE(too_low.accepted);
    CHECK(too_low.reason.find("rejected") != std::string::npos);

    const auto regressed = training_deploy::evaluate_model_quality(
        nlohmann::json{{"held_out_accuracy", 0.79}},
        nlohmann::json{{"metric", "held_out_accuracy"}, {"score", 0.80}});
    CHECK_FALSE(regressed.accepted);
    CHECK(regressed.threshold == doctest::Approx(0.80));

    const auto unchanged = training_deploy::evaluate_model_quality(
        nlohmann::json{{"held_out_accuracy", 0.80}},
        nlohmann::json{{"metric", "held_out_accuracy"}, {"score", 0.80}});
    CHECK(unchanged.accepted);

    const auto in_sample_only = training_deploy::evaluate_model_quality(
        nlohmann::json{{"in_sample_accuracy", 0.99}}, std::nullopt);
    CHECK_FALSE(in_sample_only.accepted);
    CHECK(in_sample_only.reason.find("held-out") != std::string::npos);
}

TEST_CASE("model deployment promotes the model and quality metadata as one pair") {
    TempDir app_data;
    TempDir candidate;
    write_text(app_data.path / "model.onnx", "old-model");
    write_text(app_data.path / "model_quality.json",
               "{\"metric\":\"cv_accuracy\",\"score\":0.7}");
    write_text(candidate.path / "model.onnx", "new-model");

    training_deploy::ModelQualityDecision quality;
    quality.accepted = true;
    quality.metric = "cv_accuracy";
    quality.candidate_score = 0.8;
    REQUIRE(training_deploy::deploy_model_candidate(
        app_data.path, candidate.path / "model.onnx", quality));

    CHECK(read_text(app_data.path / "model.onnx") == "new-model");
    const auto deployed_quality =
        nlohmann::json::parse(read_text(app_data.path / "model_quality.json"));
    CHECK(deployed_quality.at("metric") == "cv_accuracy");
    CHECK(deployed_quality.at("score").get<double>() == doctest::Approx(0.8));
    CHECK(deployed_quality.contains("modelId"));
    CHECK(read_text(app_data.path / "model.onnx.previous") == "old-model");
    CHECK(read_text(app_data.path / "model_quality.json.previous").find("0.7") !=
          std::string::npos);
}

TEST_CASE("model deployment leaves the accepted pair unchanged when metadata cannot stage") {
    TempDir app_data;
    TempDir candidate;
    write_text(app_data.path / "model.onnx", "accepted-model");
    write_text(app_data.path / "model_quality.json",
               "{\"metric\":\"cv_accuracy\",\"score\":0.75}");
    write_text(app_data.path / "model.onnx.previous", "older-model");
    write_text(app_data.path / "model_quality.json.previous",
               "{\"metric\":\"cv_accuracy\",\"score\":0.65}");
    write_text(candidate.path / "model.onnx", "candidate-model");
    // A non-empty directory at the staging path forces preparation to fail before the
    // live model is touched.
    write_text(app_data.path / "model_quality.json.deploying" / "blocked", "x");

    training_deploy::ModelQualityDecision quality;
    quality.accepted = true;
    quality.metric = "cv_accuracy";
    quality.candidate_score = 0.8;
    CHECK_FALSE(training_deploy::deploy_model_candidate(
        app_data.path, candidate.path / "model.onnx", quality));

    CHECK(read_text(app_data.path / "model.onnx") == "accepted-model");
    CHECK(read_text(app_data.path / "model_quality.json").find("0.75") !=
          std::string::npos);
    CHECK(read_text(app_data.path / "model.onnx.previous") == "older-model");
    CHECK(read_text(app_data.path / "model_quality.json.previous").find("0.65") !=
          std::string::npos);
}

TEST_CASE("model deployment recovery rolls back an interrupted uncommitted promotion") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "candidate-model");
    write_text(app_data.path / "model.onnx.deploy-backup", "accepted-model");
    write_text(app_data.path / "model_quality.json.deploy-backup",
               "{\"metric\":\"cv_accuracy\",\"score\":0.75}");
    write_text(app_data.path / "model_quality.json.deploying",
               "{\"metric\":\"cv_accuracy\",\"score\":0.8}");
    write_text(app_data.path / "model.onnx.previous", "accepted-model");
    write_text(app_data.path / "model.onnx.previous.deploy-backup", "older-model");
    write_text(app_data.path / "model_quality.json.previous",
               "{\"metric\":\"cv_accuracy\",\"score\":0.75}");
    write_text(app_data.path / "model_quality.json.previous.deploy-backup",
               "{\"metric\":\"cv_accuracy\",\"score\":0.65}");
    write_text(app_data.path / "model_deploy.transaction.json",
               "{\"hadModel\":true,\"hadQuality\":true,"
               "\"hadPreviousModel\":true,\"hadPreviousQuality\":true}");

    training_deploy::recover_model_deployment(app_data.path);

    CHECK(read_text(app_data.path / "model.onnx") == "accepted-model");
    CHECK(read_text(app_data.path / "model_quality.json").find("0.75") !=
          std::string::npos);
    CHECK(read_text(app_data.path / "model.onnx.previous") == "older-model");
    CHECK(read_text(app_data.path / "model_quality.json.previous").find("0.65") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_deploy.transaction.json"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_quality.json.deploying"));
}

TEST_CASE("model deployment recovery cleans staging left before marker publication") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "accepted-model");
    write_text(app_data.path / "model_quality.json",
               "{\"metric\":\"cv_accuracy\",\"score\":0.75}");
    write_text(app_data.path / "model.onnx.deploying", "candidate-model");
    write_text(app_data.path / "model_quality.json.deploying", "partial");
    write_text(app_data.path / "model.onnx.deploy-backup", "orphaned-model");
    write_text(app_data.path / "model_quality.json.deploy-backup", "orphaned-quality");
    write_text(app_data.path / "model.onnx.previous.deploy-backup", "orphaned-previous");
    write_text(app_data.path / "model_quality.json.previous.deploy-backup",
               "orphaned-previous-quality");
    write_text(app_data.path / "model_deploy.transaction.json.tmp", "{\"hadModel\"");
    write_text(app_data.path / "model_deploy.transaction.committed", "stale");

    training_deploy::recover_model_deployment(app_data.path);

    CHECK(read_text(app_data.path / "model.onnx") == "accepted-model");
    CHECK(read_text(app_data.path / "model_quality.json").find("0.75") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model.onnx.deploying"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_quality.json.deploying"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model.onnx.deploy-backup"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_quality.json.deploy-backup"));
    CHECK_FALSE(
        std::filesystem::exists(app_data.path / "model.onnx.previous.deploy-backup"));
    CHECK_FALSE(
        std::filesystem::exists(app_data.path / "model_quality.json.previous.deploy-backup"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_deploy.transaction.json.tmp"));
    CHECK_FALSE(
        std::filesystem::exists(app_data.path / "model_deploy.transaction.committed"));
}

TEST_CASE("model deployment recovery keeps a committed pair during interrupted cleanup") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "new-model");
    write_text(app_data.path / "model_quality.json",
               "{\"metric\":\"cv_accuracy\",\"score\":0.8}");
    write_text(app_data.path / "model.onnx.deploy-backup", "old-model");
    write_text(app_data.path / "model_quality.json.deploy-backup",
               "{\"metric\":\"cv_accuracy\",\"score\":0.7}");
    write_text(app_data.path / "model.onnx.previous", "old-model");
    write_text(app_data.path / "model.onnx.previous.deploy-backup", "older-model");
    write_text(app_data.path / "model_deploy.transaction.json",
               "{\"hadModel\":true,\"hadQuality\":true,"
               "\"hadPreviousModel\":true,\"hadPreviousQuality\":false}");
    write_text(app_data.path / "model_deploy.transaction.committed", "committed\n");

    training_deploy::recover_model_deployment(app_data.path);

    CHECK(read_text(app_data.path / "model.onnx") == "new-model");
    CHECK(read_text(app_data.path / "model_quality.json").find("0.8") !=
          std::string::npos);
    CHECK(read_text(app_data.path / "model.onnx.previous") == "old-model");
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model.onnx.deploy-backup"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_deploy.transaction.json"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_deploy.transaction.committed"));
}

TEST_CASE("model deployment recovery retains commit state until debris cleanup succeeds") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "new-model");
    write_text(app_data.path / "model_quality.json",
               "{\"metric\":\"cv_accuracy\",\"score\":0.8}");
    write_text(app_data.path / "model_deploy.transaction.json",
               "{\"hadModel\":true,\"hadQuality\":true,"
               "\"hadPreviousModel\":false,\"hadPreviousQuality\":false}");
    write_text(app_data.path / "model_deploy.transaction.committed", "committed\n");
    write_text(app_data.path / "model.onnx.deploy-backup" / "locked", "x");

    CHECK_THROWS(training_deploy::recover_model_deployment(app_data.path));

    CHECK(read_text(app_data.path / "model.onnx") == "new-model");
    CHECK(std::filesystem::exists(app_data.path / "model_deploy.transaction.json"));
    CHECK(std::filesystem::exists(app_data.path / "model_deploy.transaction.committed"));

    std::filesystem::remove(app_data.path / "model.onnx.deploy-backup" / "locked");
    training_deploy::recover_model_deployment(app_data.path);

    CHECK(read_text(app_data.path / "model.onnx") == "new-model");
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model.onnx.deploy-backup"));
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_deploy.transaction.json"));
    CHECK_FALSE(
        std::filesystem::exists(app_data.path / "model_deploy.transaction.committed"));
}

TEST_CASE("startup recovery returns degraded instead of throwing on locked cleanup debris") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "new-model");
    write_text(app_data.path / "model_quality.json",
               "{\"metric\":\"cv_accuracy\",\"score\":0.8}");
    write_text(app_data.path / "model_deploy.transaction.json",
               "{\"hadModel\":true,\"hadQuality\":true,"
               "\"hadPreviousModel\":false,\"hadPreviousQuality\":false}");
    write_text(app_data.path / "model_deploy.transaction.committed", "committed\n");
    write_text(app_data.path / "model.onnx.deploy-backup" / "locked", "x");

    const auto outcome = training_deploy::recover_model_deployment_for_startup(app_data.path);

    CHECK_FALSE(outcome.ok);
    CHECK(outcome.retry_cleanup_available);
    CHECK(outcome.message.find("cleanup") != std::string::npos);
    CHECK(read_text(app_data.path / "model.onnx") == "new-model");
    CHECK(std::find(outcome.preserved_paths.begin(), outcome.preserved_paths.end(),
                    "model.onnx") != outcome.preserved_paths.end());
}

TEST_CASE("startup recovery succeeds after cleanup debris is released") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "new-model");
    write_text(app_data.path / "model_deploy.transaction.json",
               "{\"hadModel\":true,\"hadQuality\":true,"
               "\"hadPreviousModel\":false,\"hadPreviousQuality\":false}");
    write_text(app_data.path / "model_deploy.transaction.committed", "committed\n");
    write_text(app_data.path / "model.onnx.deploy-backup" / "locked", "x");

    CHECK_FALSE(training_deploy::recover_model_deployment_for_startup(app_data.path).ok);
    std::filesystem::remove(app_data.path / "model.onnx.deploy-backup" / "locked");

    const auto retry = training_deploy::retry_model_deployment_cleanup(app_data.path);
    CHECK(retry.ok);
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_deploy.transaction.json"));
}

TEST_CASE("to_model_deployment_health maps degraded outcomes for health reporting") {
    training_deploy::ModelDeploymentRecoveryOutcome outcome;
    outcome.ok = false;
    outcome.message = "could not finish committed model deployment cleanup";
    outcome.preserved_paths = {"model.onnx", "model_deploy.transaction.json"};
    outcome.retry_cleanup_available = true;
    outcome.rollback_available = true;

    const auto health = training_deploy::to_model_deployment_health(outcome);
    CHECK(health.state == "degraded");
    REQUIRE(health.message.has_value());
    CHECK(health.message->find("cleanup") != std::string::npos);
    CHECK(health.retry_cleanup_available);
    CHECK(health.rollback_available);
}

TEST_CASE("rollback_model swaps the deployed model and quality metadata") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "new-model");
    write_text(app_data.path / "model.onnx.previous", "old-model");
    write_text(app_data.path / "model_quality.json", "{\"metric\":\"cv_accuracy\",\"score\":0.9}");
    write_text(app_data.path / "model_quality.json.previous",
               "{\"metric\":\"cv_accuracy\",\"score\":0.8}");

    CHECK(training_deploy::rollback_available(app_data.path));
    const auto result = training_deploy::rollback_model(app_data.path);
    CHECK(result.at("success").get<bool>());
    CHECK(read_text(app_data.path / "model.onnx") == "old-model");
    CHECK(read_text(app_data.path / "model.onnx.previous") == "new-model");
    CHECK(read_text(app_data.path / "model_quality.json").find("0.8") != std::string::npos);
    CHECK(read_text(app_data.path / "model_quality.json.previous").find("0.9") !=
          std::string::npos);

    // The swap is reversible, so a mistaken rollback can itself be undone.
    training_deploy::rollback_model(app_data.path);
    CHECK(read_text(app_data.path / "model.onnx") == "new-model");
    CHECK(read_text(app_data.path / "model.onnx.previous") == "old-model");
}

TEST_CASE("rollback_model preserves current metadata when the rollback target has none") {
    TempDir app_data;
    write_text(app_data.path / "model.onnx", "new-model");
    write_text(app_data.path / "model.onnx.previous", "old-model");
    write_text(app_data.path / "model_quality.json", "{\"metric\":\"cv_accuracy\",\"score\":0.9}");

    const auto result = training_deploy::rollback_model(app_data.path);
    CHECK(result.at("success").get<bool>());
    CHECK(read_text(app_data.path / "model.onnx") == "old-model");
    CHECK_FALSE(std::filesystem::exists(app_data.path / "model_quality.json"));
    CHECK(read_text(app_data.path / "model_quality.json.previous").find("0.9") !=
          std::string::npos);
}

TEST_CASE("training_deploy rejects invalid configured repo") {
    TempDir app_data;
    TempDir not_repo;

    CHECK_THROWS_AS(training_deploy::write_training_repo_path(app_data.path, not_repo.path),
                    std::runtime_error);
}

namespace {

// A stand-in for the training pipeline (the real one is not in this checkout, ADR-0006):
// enough of an `ml/pipeline_cli.py` for train_from_export to accept the repo and run it.
// The script body decides what the "pipeline" does.
void write_fake_repo(const std::filesystem::path& repo, const std::string& script_body) {
    write_text(repo / "ml" / "pipeline_cli.py", script_body);
}

void write_minimal_export(const std::filesystem::path& app_data) {
    const auto out_dir = training_deploy::export_dir(app_data);
    write_text(out_dir / "features.csv", "a,b\n1,2\n");
    write_text(out_dir / "labels.csv", "label\n1\n");
}

// The C++ tests do not require Python, so the two integration cases below step aside
// without it rather than fail. Every CI runner has it; a developer box might not.
bool python_available(const std::filesystem::path& app_data) {
    return training_deploy::training_deploy_status(app_data).value("pythonAvailable", false);
}

}  // namespace

TEST_CASE("train_from_export runs the pipeline inside the repo and reports its exit code") {
    TempDir app_data;
    TempDir repo;
    // Exit 2 is the pipeline's "majority-classifier stub" signal; the message the user sees
    // depends on that code arriving intact through the spawn. The prints prove the working
    // directory and the arguments reached the child, via the log the app captured.
    write_fake_repo(repo.path,
                    "import os, sys\n"
                    "print('cwd=' + os.getcwd())\n"
                    "print('argv=' + ' '.join(sys.argv[1:]))\n"
                    "sys.exit(2)\n");
    write_minimal_export(app_data.path);
    training_deploy::write_training_repo_path(app_data.path, repo.path);
    if (!python_available(app_data.path)) {
        MESSAGE("python not found; skipping the pipeline integration case");
        return;
    }

    const auto result = training_deploy::train_from_export(app_data.path);

    CHECK_FALSE(result.value("trainingSucceeded", true));
    CHECK_FALSE(result.value("cancelled", true));
    CHECK(result.value("message", "").find("majority-classifier") != std::string::npos);
    const auto log_tail = result.value("logTail", "");
    CHECK(log_tail.find("argv=-m") == std::string::npos);  // -m is the interpreter's, not ours
    CHECK(log_tail.find("--skip-export") != std::string::npos);
    CHECK(log_tail.find("--output-dir") != std::string::npos);
    // The child's cwd is the repo. Compared through the filesystem rather than as text, since
    // Python reports the OS's spelling of the path and TempDir holds the C++ one.
    const auto cwd_line = log_tail.find("cwd=");
    REQUIRE(cwd_line != std::string::npos);
    const auto cwd_end = log_tail.find('\n', cwd_line);
    const std::filesystem::path reported = log_tail.substr(cwd_line + 4, cwd_end - cwd_line - 4);
    CHECK(std::filesystem::equivalent(reported, repo.path));
}

TEST_CASE("train_from_export reports the growing log tail as progress while it runs") {
    TempDir app_data;
    TempDir repo;
    // Two lines a second apart, unbuffered so they reach the file when printed. Progress is
    // read from that file, so the second report has to contain what the first could not.
    write_fake_repo(repo.path,
                    "import sys, time\n"
                    "print('epoch 1', flush=True)\n"
                    "time.sleep(1.5)\n"
                    "print('epoch 2', flush=True)\n"
                    "time.sleep(1.2)\n"
                    "sys.exit(2)\n");
    write_minimal_export(app_data.path);
    training_deploy::write_training_repo_path(app_data.path, repo.path);
    if (!python_available(app_data.path)) {
        MESSAGE("python not found; skipping the progress integration case");
        return;
    }

    std::vector<training_deploy::TrainingProgress> reports;
    const auto result = training_deploy::train_from_export(
        app_data.path, {}, [&reports](const training_deploy::TrainingProgress& p) {
            reports.push_back(p);
        });

    CHECK_FALSE(result.value("trainingSucceeded", true));
    REQUIRE(reports.size() >= 2);
    // Reports only go out when the tail changed, so each one says something new ...
    for (std::size_t i = 1; i < reports.size(); ++i) {
        CHECK(reports[i].log_tail != reports[i - 1].log_tail);
        CHECK(reports[i].elapsed_ms >= reports[i - 1].elapsed_ms);
    }
    // ... and the last one has seen both lines.
    CHECK(reports.back().log_tail.find("epoch 1") != std::string::npos);
    CHECK(reports.back().log_tail.find("epoch 2") != std::string::npos);
}

TEST_CASE("train_from_export ends a running pipeline when asked to cancel") {
    TempDir app_data;
    TempDir repo;
    write_fake_repo(repo.path, "import time\ntime.sleep(30)\n");
    write_minimal_export(app_data.path);
    training_deploy::write_training_repo_path(app_data.path, repo.path);
    if (!python_available(app_data.path)) {
        MESSAGE("python not found; skipping the cancellation integration case");
        return;
    }

    const auto started_at = std::chrono::steady_clock::now();
    const auto result = training_deploy::train_from_export(app_data.path, [started_at] {
        return std::chrono::steady_clock::now() - started_at > std::chrono::milliseconds(500);
    });
    const auto elapsed = std::chrono::steady_clock::now() - started_at;

    CHECK(result.value("cancelled", false));
    CHECK_FALSE(result.value("trainingSucceeded", true));
    CHECK_FALSE(result.value("success", true));
    CHECK(result.value("message", "").find("cancelled") != std::string::npos);
    CHECK(elapsed < std::chrono::seconds(10));
}

TEST_CASE("training_deploy builds platform command with output dir") {
    const auto command = training_deploy::build_pipeline_command("C:/app data/exports/training");
    CHECK(command.find("-m ml.pipeline_cli") != std::string::npos);
    CHECK(command.find("--skip-export") != std::string::npos);
    // Quoting is platform-specific: this string is meant to be pasted into the user's own
    // shell, and POSIX shells need single quotes for the path to stay literal.
#if defined(_WIN32)
    CHECK(command.find("\"C:/app data/exports/training\"") != std::string::npos);
#else
    CHECK(command.find("'C:/app data/exports/training'") != std::string::npos);
#endif
}

TEST_CASE("shell_quote neutralizes command substitution in repo paths") {
    // shell_quote now guards only pipelineCommand, the string the user pastes into their
    // own shell; the app's run passes the path as argv (see training_spawn_request). The
    // pasted string still has to survive a directory literally named "$(...)", which
    // std::filesystem treats as a name and the shell does not.
    const auto quoted = training_deploy::detail::shell_quote("/tmp/$(touch /tmp/pwned)");

#if defined(_WIN32)
    CHECK(quoted == "\"/tmp/$(touch /tmp/pwned)\"");
#else
    // Single-quoted: the shell performs no substitution whatsoever inside these.
    CHECK(quoted == "'/tmp/$(touch /tmp/pwned)'");
    CHECK(quoted.front() == '\'');
    CHECK(quoted.back() == '\'');
#endif
}

TEST_CASE("shell_quote survives quotes in the path itself") {
    // The escape must not let the argument terminate early — that's what would reopen the
    // injection it exists to close.
#if defined(_WIN32)
    CHECK(training_deploy::detail::shell_quote("a\"b") == "\"a\"\"b\"");
#else
    CHECK(training_deploy::detail::shell_quote("a'b") == "'a'\\''b'");
    // Backticks are the other POSIX substitution syntax; single quotes cover them too.
    CHECK(training_deploy::detail::shell_quote("`id`") == "'`id`'");
#endif
}

TEST_CASE("shell_quote leaves ordinary paths usable") {
    const auto quoted = training_deploy::detail::shell_quote("/home/kassa/Snapback");
#if defined(_WIN32)
    CHECK(quoted == "\"/home/kassa/Snapback\"");
#else
    CHECK(quoted == "'/home/kassa/Snapback'");
#endif
}

TEST_CASE("training_spawn_request keeps the repo path as one argv element") {
    // The repo path comes from SNAPBACK_REPO or training_repo.txt -- both writable by any
    // local process running as the user. When the run went through std::system, a directory
    // literally named "$(...)" passed is_training_repo()'s existence check and then executed.
    // Now the path is handed to the child as data: one argv element, no shell in between, and
    // the working directory rather than a `cd` in a command string.
    const std::filesystem::path hostile_repo = "/tmp/$(touch /tmp/pwned)";
    const auto request = training_deploy::detail::training_spawn_request(
        hostile_repo, {"py", "-3"}, "C:/app data/exports/training",
        "C:/app data/exports/training/training.log");

    const std::vector<std::string> expected_argv = {
        "py", "-3", "-m", "ml.pipeline_cli", "--output-dir", "C:/app data/exports/training",
        "--skip-export"};
    CHECK(request.argv == expected_argv);
    REQUIRE(request.cwd.has_value());
    CHECK(*request.cwd == hostile_repo);
    REQUIRE(request.output_path.has_value());
    CHECK(*request.output_path == std::filesystem::path("C:/app data/exports/training/training.log"));
}
