// Small JSON-backed app settings store.
//
// Keep the contract explicit: one settings.json file in the app-data directory, loaded
// at startup and rewritten when the user changes settings.
#pragma once

#include <filesystem>

#include "types.hpp"

namespace snapback {

inline constexpr const char* kSettingsFileName = "settings.json";

AppSettings load_app_settings(const std::filesystem::path& app_data_dir);
void save_app_settings(const std::filesystem::path& app_data_dir,
                       const AppSettings& settings);

}  // namespace snapback
