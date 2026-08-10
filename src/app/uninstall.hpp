// Removing everything Snapback created. Roadmap 9.5.
//
// **The decision this item asked for: uninstall removes all of it, database included.**
//
// The four things an uninstall plausibly left behind were the database of window titles, the
// autostart entry pointing at a deleted binary, the logs and their rotations, and the exported
// CSVs. Of those the database is the one that matters, and the reason is not size or tidiness:
// a person who uninstalls a keystroke-observing application believes they have removed what it
// recorded. Leaving `focoflow.db` on disk makes that belief false, silently, which is the exact
// failure this product cannot afford. So the default is removal, and the caller is told what
// went and what did not.
//
// This is deliberately **not** `delete_all_activity_data` (Roadmap 8.12). That one clears the
// rows and the exports while keeping the database file, the settings, and the trained model,
// because the app is still running afterwards and the user is still using it. Uninstall keeps
// nothing — there is no "afterwards".
//
// The autostart entry is handled separately by `set_autostart_enabled(false)` rather than being
// listed here: it is a registry key or a launchd/systemd unit, not a path, and the platform
// modules already own the difference.
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

// Everything under `app_data_dir` that Snapback created.
//
// Enumerated by shape rather than by a fixed list where the count can vary: a database that
// survived two migrations has two backups, and rotated logs go as far as the rotation was
// configured to keep. A build that has since bumped kSchemaVersion would not know which older
// backups to look for, so anything matching the pattern is included.
std::vector<UninstallArtifact> uninstall_artifacts(const std::filesystem::path& app_data_dir);

// Removes every artifact above and disables autostart, reporting what happened to each.
//
// Reuses ActivityDeletionResult (8.12) rather than inventing a parallel shape: the question a
// caller asks afterwards is identical — what is gone, what is still here, and why — and one of
// the two answers being a different type would be the only difference.
//
// Absence counts as removed. A log that was never written is not a copy of anything, and
// reporting it as a failure would teach the user to ignore the failure list.
ActivityDeletionResult purge_app_data(const std::filesystem::path& app_data_dir);

}  // namespace snapback
