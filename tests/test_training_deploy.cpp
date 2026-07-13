#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

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
}

TEST_CASE("training_deploy rejects invalid configured repo") {
    TempDir app_data;
    TempDir not_repo;

    CHECK_THROWS_AS(training_deploy::write_training_repo_path(app_data.path, not_repo.path),
                    std::runtime_error);
}

TEST_CASE("training_deploy builds platform command with output dir") {
    const auto command = training_deploy::build_pipeline_command("C:/app data/exports/training");
    CHECK(command.find("-m ml.pipeline_cli") != std::string::npos);
    CHECK(command.find("--skip-export") != std::string::npos);
    CHECK(command.find("\"C:/app data/exports/training\"") != std::string::npos);
}
