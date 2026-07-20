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

namespace detail {

// Quote a single argument for the platform shell so its contents can never be interpreted
// as syntax. Exposed for testing — the repo path reaches std::system, and it comes from a
// user-writable env var / file, so this is the boundary that has to hold.
std::string shell_quote(const std::string& value);

}  // namespace detail

}  // namespace snapback::training_deploy
