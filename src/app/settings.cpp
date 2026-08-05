#include "app/settings.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace snapback {
namespace {

// Parses one file. Returns nullopt for "not usable" — absent, unreadable, or malformed —
// and sets `existed` so the caller can tell a first run apart from a corrupt file. Only the
// second deserves a log line.
std::optional<AppSettings> read_settings_file(const std::filesystem::path& path,
                                              bool& existed,
                                              std::string& error) {
    std::error_code ec;
    existed = std::filesystem::exists(path, ec) && !ec;
    if (!existed) return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "could not be opened";
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(in).get<AppSettings>();
    } catch (const std::exception& e) {
        // Both a JSON syntax error and a type mismatch land here. Reporting the reason is
        // the whole point of 7.19: the old code caught this and returned defaults, so a
        // user whose settings reverted had nothing to look at.
        error = e.what();
        return std::nullopt;
    }
}

}  // namespace

AppSettings load_app_settings(const std::filesystem::path& app_data_dir, Logger* logger) {
    const auto path = app_data_dir / kSettingsFileName;
    const auto backup = app_data_dir / kSettingsBackupFileName;

    bool existed = false;
    std::string error;
    if (auto settings = read_settings_file(path, existed, error)) return *settings;

    if (!existed) return {};  // First run: no file, nothing to report.

    if (logger) {
        logger->warn(std::string("settings: ") + kSettingsFileName + " is unusable (" +
                     error + "); trying " + kSettingsBackupFileName);
    }

    bool backup_existed = false;
    std::string backup_error;
    if (auto recovered = read_settings_file(backup, backup_existed, backup_error)) {
        if (logger) {
            logger->info(std::string("settings: recovered configuration from ") +
                         kSettingsBackupFileName);
        }
        // The damaged file is deliberately left in place rather than repaired here: the next
        // save rewrites it anyway, and a loader that writes is a loader that can fail on a
        // read-only directory during startup.
        return *recovered;
    }

    if (logger) {
        logger->error(std::string("settings: no usable configuration; falling back to "
                                  "defaults. ") +
                      kSettingsBackupFileName +
                      (backup_existed ? " is also unusable (" + backup_error + ")"
                                      : " does not exist"));
    }
    return {};
}

void save_app_settings(const std::filesystem::path& app_data_dir,
                       const AppSettings& settings) {
    std::filesystem::create_directories(app_data_dir);
    const auto path = app_data_dir / kSettingsFileName;
    const auto temp = app_data_dir / kSettingsTempFileName;
    const auto backup = app_data_dir / kSettingsBackupFileName;

    // 1. Stage the new contents beside the destination. Nothing the user depends on is
    //    touched yet, so a failure here is a failure that changed nothing.
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("failed to open settings temp file");

        out << nlohmann::json(settings).dump(2) << '\n';
        out.flush();

        // Check the stream *before* the rename. The old code checked only that the file
        // opened, so a disk that filled up mid-write produced a truncated settings.json and
        // a silent success. flush() forces the failure to surface here rather than in the
        // destructor, where it would be discarded.
        if (!out) throw std::runtime_error("failed to write settings temp file");
    }

    // 2. Keep the copy that is currently on disk as the backup, before it is replaced. A
    //    crash between here and the rename leaves both files valid and identical in content
    //    to what the user had; the new settings are simply not applied yet.
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        std::filesystem::copy_file(path, backup,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        // A failed backup is not worth losing the save over — the rename below is still
        // atomic, so the user's new settings land either way. Only the recovery copy is lost.
        ec.clear();
    }

    // 3. Replace the destination in one step. rename over an existing file is atomic on both
    //    POSIX and Win32 (MoveFileEx with MOVEFILE_REPLACE_EXISTING), which is what removes
    //    the window where settings.json exists but is empty.
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("failed to replace settings.json");
    }
}

}  // namespace snapback
