// Roadmap 9.5. What uninstall removes, and the promise behind the choice.
#include "doctest_wrapper.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/uninstall.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir()
        : path(std::filesystem::temp_directory_path() /
               ("snapback-uninstall-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void touch(const std::filesystem::path& file, const std::string& contents = "x") {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << contents;
}

bool lists(const std::vector<UninstallArtifact>& targets, const std::filesystem::path& path) {
    return std::any_of(targets.begin(), targets.end(),
                       [&](const UninstallArtifact& a) { return a.path == path; });
}

bool mentions(const std::vector<std::string>& lines, const std::string& needle) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(needle) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("uninstall enumerates the database, and every copy of it") {
    // The item's whole point: of the four things left behind, the database is the one that
    // makes "I removed it" false.
    TempDir temp;
    touch(temp.path / "focoflow.db");
    touch(temp.path / "focoflow.db-wal");
    touch(temp.path / "focoflow.db.pre-v4.bak");
    touch(temp.path / "focoflow.db.pre-v5.bak");

    const auto targets = uninstall_artifacts(temp.path);
    CHECK(lists(targets, temp.path / "focoflow.db"));
    CHECK(lists(targets, temp.path / "focoflow.db-wal"));
    // Both backups, not just the one this build's schema version would predict.
    CHECK(lists(targets, temp.path / "focoflow.db.pre-v4.bak"));
    CHECK(lists(targets, temp.path / "focoflow.db.pre-v5.bak"));
}

TEST_CASE("uninstall enumerates the logs, including the rotated ones") {
    TempDir temp;
    touch(temp.path / "snapback.log");
    touch(temp.path / "snapback.log.1");
    touch(temp.path / "snapback.log.2");

    const auto targets = uninstall_artifacts(temp.path);
    CHECK(lists(targets, temp.path / "snapback.log"));
    CHECK(lists(targets, temp.path / "snapback.log.1"));
    CHECK(lists(targets, temp.path / "snapback.log.2"));
}

TEST_CASE("a file that merely starts like a log is left alone") {
    // The rotation suffix is digits. `snapback.log.bak` is somebody else's file, and an
    // uninstall that deletes files it does not own is a different kind of surprise.
    TempDir temp;
    touch(temp.path / "snapback.log.bak");
    touch(temp.path / "snapback.logbook");

    const auto targets = uninstall_artifacts(temp.path);
    CHECK_FALSE(lists(targets, temp.path / "snapback.log.bak"));
    CHECK_FALSE(lists(targets, temp.path / "snapback.logbook"));
}

TEST_CASE("uninstall removes everything it owns and says what went") {
    TempDir temp;
    touch(temp.path / "focoflow.db");
    touch(temp.path / "settings.json");
    touch(temp.path / "settings.json.bak");
    touch(temp.path / "snapback.log");
    touch(temp.path / "exports" / "personal" / "archive.md");
    touch(temp.path / "models" / "classifier.onnx");

    const auto result = purge_app_data(temp.path);

    CHECK_FALSE(std::filesystem::exists(temp.path / "focoflow.db"));
    CHECK_FALSE(std::filesystem::exists(temp.path / "settings.json"));
    CHECK_FALSE(std::filesystem::exists(temp.path / "snapback.log"));
    CHECK_FALSE(std::filesystem::exists(temp.path / "exports"));
    CHECK_FALSE(std::filesystem::exists(temp.path / "models"));

    CHECK(mentions(result.deleted, "recorded window titles"));
    CHECK(mentions(result.deleted, "settings"));
    CHECK(mentions(result.deleted, "exported"));
}

TEST_CASE("an artifact that was never created is not reported as a failure") {
    // Absence counts as removed. A failure list that cries wolf is a failure list nobody reads.
    TempDir temp;
    touch(temp.path / "focoflow.db");

    const auto result = purge_app_data(temp.path);
    CHECK(result.complete());
    CHECK(result.failed.empty());
}

TEST_CASE("uninstall leaves files it did not create, and the folder holding them") {
    // Not remove_all on the directory: someone who pointed SNAPBACK_DATA_DIR at a folder of
    // their own keeps what is theirs.
    TempDir temp;
    touch(temp.path / "focoflow.db");
    touch(temp.path / "my-notes.txt", "mine");

    const auto result = purge_app_data(temp.path);
    CHECK_FALSE(std::filesystem::exists(temp.path / "focoflow.db"));
    CHECK(std::filesystem::exists(temp.path / "my-notes.txt"));
    CHECK(std::filesystem::exists(temp.path));
    CHECK(result.complete());
}

TEST_CASE("an empty data directory reports a failure rather than silently succeeding") {
    // "Nowhere to look" is not "nothing to remove": an uninstall that says it cleaned up when
    // it never found the data is the exact false belief 9.5 is about.
    const auto result = purge_app_data({});
    CHECK_FALSE(result.complete());
    CHECK(result.deleted.empty());
}

TEST_CASE("a label appears once however many files carry it") {
    // Three rotated logs are one line in a list a person reads.
    TempDir temp;
    touch(temp.path / "snapback.log");
    touch(temp.path / "snapback.log.1");
    touch(temp.path / "snapback.log.2");

    const auto result = purge_app_data(temp.path);
    const auto logs = std::count(result.deleted.begin(), result.deleted.end(),
                                 std::string("the diagnostic logs"));
    CHECK(logs == 1);
}
