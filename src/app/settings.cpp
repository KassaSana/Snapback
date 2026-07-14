#include "app/settings.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace snapback {

AppSettings load_app_settings(const std::filesystem::path& app_data_dir) {
    const auto path = app_data_dir / kSettingsFileName;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    try {
        return nlohmann::json::parse(in).get<AppSettings>();
    } catch (...) {
        return {};
    }
}

void save_app_settings(const std::filesystem::path& app_data_dir,
                       const AppSettings& settings) {
    std::filesystem::create_directories(app_data_dir);
    const auto path = app_data_dir / kSettingsFileName;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write settings.json");
    }
    out << nlohmann::json(settings).dump(2) << '\n';
}

}  // namespace snapback
