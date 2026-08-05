// Small JSON-backed app settings store.
//
// Keep the contract explicit: one settings.json file in the app-data directory, loaded
// at startup and rewritten when the user changes settings.
//
// Roadmap 7.19. Writing that file is a replace, not an append, so the naive version — open
// the only copy with trunc and stream into it — has a window where the user's configuration
// exists nowhere: truncated on disk and not yet rewritten. A crash or a full disk inside
// that window leaves an empty or half-written file, and the loader silently turned any
// unparseable file into defaults, so the user's settings quietly reverted with nothing said.
//
// The write is therefore staged through a sibling temp file and renamed over the
// destination, and the previous good copy is kept alongside as settings.json.bak. The
// loader falls back to that backup and says so.
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

// Reads settings.json, falling back to settings.json.bak and then to defaults.
//
// `logger` is optional so existing call sites compile unchanged; pass one and a malformed
// file is reported instead of silently becoming defaults. A *missing* file is not an error
// (first run) and is not logged.
AppSettings load_app_settings(const std::filesystem::path& app_data_dir,
                              Logger* logger = nullptr);

// Writes settings.json via a temp file + rename, keeping the previous copy as
// settings.json.bak. Throws if the settings could not be durably written, leaving the
// existing file untouched.
void save_app_settings(const std::filesystem::path& app_data_dir,
                       const AppSettings& settings);

}  // namespace snapback
