// Removing everything Snapback created, database included: someone uninstalling a
// keystroke-observing app believes its recordings are gone. Unlike delete_all_activity_data,
// nothing is kept. Autostart is handled separately by set_autostart_enabled(false).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "types.hpp"

namespace snapback {

// One thing the app owns, and the name a user would recognise it by.
struct UninstallArtifact {
    std::filesystem::path path;
    std::string label;
    bool is_directory = false;
};

// Everything under `app_data_dir` that Snapback created, matched by shape where counts vary
// (migration backups, rotated logs).
std::vector<UninstallArtifact> uninstall_artifacts(const std::filesystem::path& app_data_dir);

// Removes every artifact above and disables autostart, reporting each outcome. Absence counts
// as removed.
ActivityDeletionResult purge_app_data(const std::filesystem::path& app_data_dir);

}  // namespace snapback
