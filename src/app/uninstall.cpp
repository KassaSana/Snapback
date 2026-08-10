#include "app/uninstall.hpp"

#include <algorithm>
#include <system_error>

#include "app/autostart.hpp"
#include "app/settings.hpp"

namespace snapback {
namespace {

// A log file rotates to `snapback.log.1`, `.2`, ... so the rotations are matched by prefix
// rather than by asking the logger how many it was configured to keep — that number lives in a
// running process, and an uninstall runs when there isn't one.
bool is_rotated_log(const std::string& name) {
    static constexpr const char* kBase = "snapback.log";
    if (name.rfind(kBase, 0) != 0) return false;
    if (name == kBase) return true;
    const auto suffix = name.substr(std::char_traits<char>::length(kBase));
    if (suffix.size() < 2 || suffix[0] != '.') return false;
    return std::all_of(suffix.begin() + 1, suffix.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool is_database_backup(const std::string& name) {
    return name.rfind("focoflow.db.pre-v", 0) == 0 && name.size() > 4 &&
           name.compare(name.size() - 4, 4, ".bak") == 0;
}

}  // namespace

std::vector<UninstallArtifact> uninstall_artifacts(const std::filesystem::path& app_data_dir) {
    if (app_data_dir.empty()) return {};

    std::vector<UninstallArtifact> targets{
        // The database first in the list because it is first in importance: it is the file that
        // makes "I uninstalled it" true or false.
        {app_data_dir / "focoflow.db", "your recorded window titles and sessions", false},
        {app_data_dir / kSettingsFileName, "your settings", false},
        {app_data_dir / kSettingsBackupFileName, "the settings backup", false},
        {app_data_dir / kSettingsTempFileName, "a partially written settings file", false},
        {app_data_dir / "exports", "everything you exported", true},
        {app_data_dir / "models", "the trained model and its metadata", true},
    };

    // SQLite may leave these beside the database after a crash. They hold recent writes, which
    // for this app means recent window titles, so removing the database and leaving them is the
    // same mistake in a smaller file.
    for (const char* companion : {"focoflow.db-wal", "focoflow.db-shm", "focoflow.db-journal"}) {
        targets.push_back({app_data_dir / companion, "unflushed database writes", false});
    }

    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(app_data_dir, error)) {
        const auto name = entry.path().filename().string();
        if (is_database_backup(name)) {
            targets.push_back({entry.path(), "a pre-migration database backup", false});
        } else if (is_rotated_log(name)) {
            targets.push_back({entry.path(), "the diagnostic logs", false});
        }
    }
    // A directory that cannot be listed yields the fixed targets only, which is still the
    // important half; the caller reports whatever could not be removed.
    return targets;
}

ActivityDeletionResult purge_app_data(const std::filesystem::path& app_data_dir) {
    ActivityDeletionResult result;
    if (app_data_dir.empty()) {
        result.failed.emplace_back("no application data directory was found to remove");
        return result;
    }

    for (const auto& artifact : uninstall_artifacts(app_data_dir)) {
        std::error_code error;
        if (artifact.is_directory) {
            std::filesystem::remove_all(artifact.path, error);
        } else {
            std::filesystem::remove(artifact.path, error);
        }
        // remove/remove_all report false-with-no-error for something that was not there, which
        // is success: nothing is left behind either way.
        if (error) {
            result.failed.emplace_back(artifact.label + " (" + error.message() + ")");
        } else if (std::find(result.deleted.begin(), result.deleted.end(), artifact.label) ==
                   result.deleted.end()) {
            // De-duplicated by label: three rotated logs are one line in a list a person reads.
            result.deleted.emplace_back(artifact.label);
        }
    }

    // The startup entry goes last. It is the one artifact that is not a file, and leaving it
    // behind points the OS at a binary that is about to stop existing.
    if (autostart_supported() && !set_autostart_enabled(false)) {
        result.failed.emplace_back("the start-on-login entry");
    } else {
        result.deleted.emplace_back("the start-on-login entry");
    }

    // Finally the directory itself, if nothing else put anything there. Deliberately not
    // remove_all: a user who chose this folder for their own files keeps them.
    std::error_code error;
    std::filesystem::remove(app_data_dir, error);
    return result;
}

}  // namespace snapback
