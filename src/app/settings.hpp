// JSON-backed settings: one settings.json in the app-data directory. Writes go through a
// sibling temp file renamed over the destination, keeping the previous copy as
// settings.json.bak, so a crash mid-write never leaves the only copy truncated.
#pragma once

#include <filesystem>

#include "types.hpp"
#include "util/logger.hpp"

namespace snapback {

inline constexpr const char* kSettingsFileName = "settings.json";
// Written before the destination is replaced, so a crash mid-write damages only this file.
inline constexpr const char* kSettingsTempFileName = "settings.json.tmp";
// The copy that was on disk before the most recent successful save.
inline constexpr const char* kSettingsBackupFileName = "settings.json.bak";

// Reads settings.json, falling back to .bak and then defaults. With a logger, a malformed file
// is reported; a missing one (first run) is not.
AppSettings load_app_settings(const std::filesystem::path& app_data_dir,
                              Logger* logger = nullptr);

// Writes settings.json via a temp file + rename, keeping the previous copy as
// settings.json.bak. Throws if the settings could not be durably written, leaving the
// existing file untouched.
void save_app_settings(const std::filesystem::path& app_data_dir,
                       const AppSettings& settings);

}  // namespace snapback
