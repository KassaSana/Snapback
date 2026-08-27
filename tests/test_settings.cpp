#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/settings.hpp"
#include "util/logger.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_settings_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

AppSettings sample_settings() {
    AppSettings settings;
    settings.default_focus_mode = FocusMode::Deep;
    settings.private_mode = true;
    settings.excluded_apps = {"Slack", "Discord"};
    return settings;
}

}  // namespace

TEST_CASE("settings round-trip through save and load") {
    TempDir temp;
    save_app_settings(temp.path, sample_settings());

    const auto loaded = load_app_settings(temp.path);
    CHECK(loaded.default_focus_mode == FocusMode::Deep);
    CHECK(loaded.private_mode == true);
    REQUIRE(loaded.excluded_apps.size() == 2);
    CHECK(loaded.excluded_apps[0] == "Slack");
}

TEST_CASE("a missing settings file is a first run, not an error") {
    TempDir temp;
    std::ostringstream sink;
    Logger logger(sink, LogLevel::Trace);

    const auto loaded = load_app_settings(temp.path, &logger);
    CHECK(loaded.private_mode == false);
    CHECK(loaded.excluded_apps.empty());
    // Nothing is wrong on a first run, so nothing is reported. A warning here would train
    // the user to ignore the one that matters.
    CHECK(sink.str().empty());
}

TEST_CASE("saving keeps the previous settings as a backup") {
    TempDir temp;
    save_app_settings(temp.path, sample_settings());
    const auto first_contents = read_file(temp.path / kSettingsFileName);

    AppSettings changed;
    changed.default_focus_mode = FocusMode::Normal;
    changed.private_mode = false;
    save_app_settings(temp.path, changed);

    // The backup holds what was on disk before this save, byte for byte.
    REQUIRE(std::filesystem::exists(temp.path / kSettingsBackupFileName));
    CHECK(read_file(temp.path / kSettingsBackupFileName) == first_contents);
    CHECK(load_app_settings(temp.path).private_mode == false);

    // A third save must *overwrite* the existing backup, not leave the stale one. This is
    // the case the first two saves cannot reach: on save one there is no settings.json to
    // back up, and on save two there is no settings.json.bak to overwrite. Only here does
    // the copy actually have to replace something -- which is exactly where libstdc++ on
    // MinGW ignores copy_options::overwrite_existing (ROADMAP 11.8).
    const auto second_contents = read_file(temp.path / kSettingsFileName);
    AppSettings third;
    third.default_focus_mode = FocusMode::Deep;
    save_app_settings(temp.path, third);

    CHECK(read_file(temp.path / kSettingsBackupFileName) == second_contents);
    CHECK(read_file(temp.path / kSettingsBackupFileName) != first_contents);
}

TEST_CASE("settings save leaves no temp file after a durable flush") {
    // Roadmap 7.21. The temp is synced, then renamed; a leftover `.tmp` would mean the
    // durable path never finished and a later crash could confuse the loader.
    TempDir temp;
    save_app_settings(temp.path, sample_settings());
    CHECK(std::filesystem::exists(temp.path / kSettingsFileName));
    CHECK_FALSE(std::filesystem::exists(temp.path / kSettingsTempFileName));
    CHECK(load_app_settings(temp.path).private_mode == true);
}

TEST_CASE("malformed settings recover from the backup and say so") {
    // Roadmap 7.19. The old loader caught every parse error and returned defaults with no
    // log line, so a user whose configuration silently reverted had nothing to look at.
    TempDir temp;
    save_app_settings(temp.path, sample_settings());  // establishes settings.json
    save_app_settings(temp.path, sample_settings());  // establishes settings.json.bak

    write_file(temp.path / kSettingsFileName, "{ this is not json");

    std::ostringstream sink;
    Logger logger(sink, LogLevel::Trace);
    const auto loaded = load_app_settings(temp.path, &logger);

    // Recovered the real configuration, not defaults.
    CHECK(loaded.default_focus_mode == FocusMode::Deep);
    CHECK(loaded.private_mode == true);
    REQUIRE(loaded.excluded_apps.size() == 2);

    const auto log = sink.str();
    CHECK(log.find("unusable") != std::string::npos);
    CHECK(log.find("recovered configuration") != std::string::npos);
}

TEST_CASE("a known key with the wrong type is malformed, not just unparseable") {
    // Valid JSON that violates the schema takes the same path as a syntax error: get_or
    // calls get<T>() on a key that is present, which throws json::type_error. Both must be
    // reported rather than swallowed.
    TempDir temp;
    write_file(temp.path / kSettingsFileName, R"({"privateMode": "yes please"})");

    std::ostringstream sink;
    Logger logger(sink, LogLevel::Trace);
    const auto loaded = load_app_settings(temp.path, &logger);

    CHECK(loaded.private_mode == false);  // defaults; there is no backup to recover from
    const auto log = sink.str();
    CHECK(log.find("unusable") != std::string::npos);
    CHECK(log.find("does not exist") != std::string::npos);
}

TEST_CASE("an unknown or absent key is tolerated silently, by design") {
    // get_or treats missing and null as "use the default" on purpose, so the settings file
    // survives schema drift in both directions. That is deliberately NOT malformed: an old
    // file missing a new key, or a key this build no longer reads, must load quietly.
    //
    // Pinned here because the obvious "improvement" -- reporting anything unrecognised --
    // would make every upgrade log a warning the user cannot act on.
    TempDir temp;
    write_file(temp.path / kSettingsFileName,
               R"({"privateMode": true, "someKeyFromAFutureBuild": 42})");

    std::ostringstream sink;
    Logger logger(sink, LogLevel::Trace);
    const auto loaded = load_app_settings(temp.path, &logger);

    CHECK(loaded.private_mode == true);
    CHECK(loaded.default_focus_mode == FocusMode::Normal);  // absent key -> its default
    CHECK(sink.str().empty());
}

TEST_CASE("malformed settings with no usable backup fall back to defaults loudly") {
    TempDir temp;
    write_file(temp.path / kSettingsFileName, "{ broken");
    write_file(temp.path / kSettingsBackupFileName, "{ also broken");

    std::ostringstream sink;
    Logger logger(sink, LogLevel::Trace);
    const auto loaded = load_app_settings(temp.path, &logger);

    CHECK(loaded.private_mode == false);
    CHECK(loaded.excluded_apps.empty());
    const auto log = sink.str();
    CHECK(log.find("falling back to defaults") != std::string::npos);
    CHECK(log.find("is also unusable") != std::string::npos);
}

TEST_CASE("a failed save leaves the existing settings file untouched") {
    // The failure seam: settings.json.tmp is a directory, so the staging write cannot open
    // it. This is deterministic and needs no privileges, unlike making a directory
    // read-only. It exercises the property that matters -- a save that cannot complete must
    // not damage the configuration already on disk, which is exactly what the old
    // open-the-destination-with-trunc write could not promise.
    TempDir temp;
    save_app_settings(temp.path, sample_settings());
    const auto good_contents = read_file(temp.path / kSettingsFileName);

    std::filesystem::create_directories(temp.path / kSettingsTempFileName);

    AppSettings replacement;
    replacement.default_focus_mode = FocusMode::Normal;
    CHECK_THROWS_AS(save_app_settings(temp.path, replacement), std::runtime_error);

    // Unchanged on disk, and still loads as the original configuration.
    CHECK(read_file(temp.path / kSettingsFileName) == good_contents);
    const auto loaded = load_app_settings(temp.path);
    CHECK(loaded.default_focus_mode == FocusMode::Deep);
    CHECK(loaded.private_mode == true);
}

TEST_CASE("a failed replace does not leave the staging file behind") {
    // The other half of the failure path: the staging write succeeds and the rename fails.
    // Making the destination a directory fails the rename on both POSIX and Win32. The temp
    // file must be cleaned up rather than left to be mistaken for a pending save.
    TempDir temp;
    std::filesystem::create_directories(temp.path / kSettingsFileName);

    CHECK_THROWS_AS(save_app_settings(temp.path, sample_settings()), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(temp.path / kSettingsTempFileName));
}

TEST_CASE("settings.json is never left truncated by a partial write") {
    // The window 7.19 exists to close: the destination is only ever replaced by a rename of
    // a fully written file, so it is either the old contents or the new contents and never
    // an empty file in between. Asserting the temp file is gone afterwards is what shows the
    // rename happened rather than a copy.
    TempDir temp;
    save_app_settings(temp.path, sample_settings());

    CHECK(std::filesystem::exists(temp.path / kSettingsFileName));
    CHECK_FALSE(std::filesystem::exists(temp.path / kSettingsTempFileName));
    CHECK_FALSE(read_file(temp.path / kSettingsFileName).empty());
}

// Roadmap 2.16. Alert delivery is the first nested settings object with a wire form that is
// not a mirror of its struct, so these pin the crossing rather than the struct.

TEST_CASE("alert delivery settings round-trip through save and load") {
    TempDir temp;
    AppSettings settings;
    settings.alerts.snapback = AlertChannels{false, true, true};  // the explicit "both"
    settings.alerts.hyperfocus = AlertChannels{};                 // "do not interrupt me"
    settings.alerts.preview = AlertPreviewMode::Generic;
    settings.alerts.quiet_hours_enabled = true;
    settings.alerts.quiet_hours_start_min = 22 * 60 + 30;
    settings.alerts.quiet_hours_end_min = 7 * 60;
    settings.alerts.snoozed_until_wall_ms = 1'700'000'000'000;
    save_app_settings(temp.path, settings);

    const auto loaded = load_app_settings(temp.path);
    CHECK(loaded.alerts.snapback.overlay == true);
    CHECK(loaded.alerts.snapback.native == true);
    CHECK(loaded.alerts.snapback.in_app == false);
    CHECK(loaded.alerts.hyperfocus.any() == false);
    CHECK(loaded.alerts.preview == AlertPreviewMode::Generic);
    CHECK(loaded.alerts.quiet_hours_enabled == true);
    CHECK(loaded.alerts.quiet_hours_start_min == 22 * 60 + 30);
    CHECK(loaded.alerts.snoozed_until_wall_ms == 1'700'000'000'000);
}

TEST_CASE("a settings file written before 2.16 loads with alert defaults") {
    // The upgrade path. Every other preference in the file must survive, and the alert block
    // must arrive as its defaults rather than as an all-off object that would silently stop
    // the app interrupting at all.
    TempDir temp;
    write_file(temp.path / kSettingsFileName,
               R"({"privateMode": true, "idleThresholdSecs": 600})");

    std::ostringstream sink;
    Logger logger(sink, LogLevel::Trace);
    const auto loaded = load_app_settings(temp.path, &logger);

    CHECK(loaded.private_mode == true);
    CHECK(loaded.idle_threshold_secs == 600);
    CHECK(loaded.alerts.snapback.overlay == true);
    CHECK(loaded.alerts.hyperfocus.native == true);
    CHECK(loaded.alerts.pomodoro.in_app == true);
    CHECK(loaded.alerts.quiet_hours_enabled == false);
    CHECK(loaded.alerts.snoozed_until_wall_ms == 0);
    CHECK(sink.str().empty());  // an absent key is not a complaint
}

TEST_CASE("an out-of-range quiet-hours minute falls back to its default") {
    // Rejected, not clamped -- the same call idleThresholdSecs makes. A clamped 1500 would
    // become 23:59 and look like a range the user chose.
    TempDir temp;
    write_file(temp.path / kSettingsFileName,
               R"({"alerts": {"quietHoursStartMin": 1500, "quietHoursEndMin": -1}})");

    const auto loaded = load_app_settings(temp.path);
    CHECK(loaded.alerts.quiet_hours_start_min == 22 * 60);
    CHECK(loaded.alerts.quiet_hours_end_min == 7 * 60);
}

TEST_CASE("an unknown alert channel name is dropped, not fatal") {
    // A file written by a newer build degrades to the channels this one understands. The
    // alternative -- rejecting the object -- would revert every delivery preference at once.
    TempDir temp;
    write_file(temp.path / kSettingsFileName,
               R"({"alerts": {"snapback": ["overlay", "holograph"]}})");

    const auto loaded = load_app_settings(temp.path);
    CHECK(loaded.alerts.snapback.overlay == true);
    CHECK(loaded.alerts.snapback.native == false);
    CHECK(loaded.alerts.snapback.in_app == false);
}

TEST_CASE("an empty channel array means this event does not interrupt me") {
    // Distinct from the key being absent, which yields the default. `[]` is a choice.
    TempDir temp;
    write_file(temp.path / kSettingsFileName, R"({"alerts": {"snapback": []}})");

    const auto loaded = load_app_settings(temp.path);
    CHECK(loaded.alerts.snapback.any() == false);
    CHECK(loaded.alerts.hyperfocus.native == true);  // untouched sibling keeps its default
}
