#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace snapback::training_deploy {

std::filesystem::path export_dir(const std::filesystem::path& app_data_dir);
bool is_training_repo(const std::filesystem::path& path);
std::optional<std::filesystem::path> read_training_repo_path(
    const std::filesystem::path& app_data_dir);
void write_training_repo_path(const std::filesystem::path& app_data_dir,
                              const std::filesystem::path& repo_path);
nlohmann::json training_deploy_status(const std::filesystem::path& app_data_dir);
nlohmann::json train_from_export(const std::filesystem::path& app_data_dir);
std::string build_pipeline_command(const std::filesystem::path& output_dir);

}  // namespace snapback::training_deploy
