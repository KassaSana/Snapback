#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/autostart_systemd.hpp"

using namespace snapback;
using namespace snapback::systemd;

namespace {

// Hermetic by construction — the directory is a parameter, so nothing here can register the
// test binary to start at login (Roadmap 11.7).
struct UnitDir {
    std::filesystem::path path;

    UnitDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_systemd_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    ~UnitDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("escape_exec_path quotes the path so a space cannot split it into two arguments") {
    CHECK(escape_exec_path("/opt/snapback/snapback") == "\"/opt/snapback/snapback\"");
    CHECK(escape_exec_path("/home/kassa/my apps/snapback") == "\"/home/kassa/my apps/snapback\"");
}

// `%` is a systemd specifier prefix: `%h` expands to the home directory, and an unknown
// specifier stops the unit loading entirely. A path is data, so every `%` in it must be the
// literal `%%`.
TEST_CASE("escape_exec_path escapes a percent so systemd does not expand it") {
    CHECK(escape_exec_path("/home/kassa/100%backup/snapback") ==
          "\"/home/kassa/100%%backup/snapback\"");
    CHECK(escape_exec_path("/home/%h/snapback") == "\"/home/%%h/snapback\"");
}

TEST_CASE("escape_exec_path escapes backslashes and quotes inside the quoted word") {
    CHECK(escape_exec_path("/home/back\\slash") == "\"/home/back\\\\slash\"");
    CHECK(escape_exec_path("/home/qu\"ote") == "\"/home/qu\\\"ote\"");
}

TEST_CASE("unit_file declares a graphical-session login item that does not relaunch itself") {
    const auto unit = unit_file("/opt/snapback/snapback");

    CHECK(contains(unit, "[Unit]"));
    CHECK(contains(unit, "[Service]"));
    CHECK(contains(unit, "[Install]"));
    CHECK(contains(unit, "ExecStart=\"/opt/snapback/snapback\""));
    CHECK(contains(unit, "WantedBy=graphical-session.target"));
    // PartOf makes logging out stop Snapback, rather than leaving it capturing input for a
    // session that has ended.
    CHECK(contains(unit, "PartOf=graphical-session.target"));
    // Restart=no for the same reason the launchd agent has no KeepAlive.
    CHECK(contains(unit, "Restart=no"));
}

// The difference from launchd that is easiest to get wrong: systemd runs only what a target
// *wants*, so a unit file with no enable link is installed-but-off.
TEST_CASE("a unit file without its enable link does not count as enabled") {
    UnitDir dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(unit_path(dir.path), std::ios::binary) << unit_file("/opt/snapback/snapback");

    CHECK(std::filesystem::exists(unit_path(dir.path)));
    // A checked toggle with nothing starting at login is the exact bug this asserts against.
    CHECK_FALSE(unit_enabled(dir.path));
}

TEST_CASE("install_unit writes the unit and the enable link systemctl would create") {
    UnitDir dir;
    REQUIRE(install_unit(dir.path, "/opt/snapback/snapback"));

    CHECK(unit_enabled(dir.path));
    CHECK(contains(read_file(unit_path(dir.path)), "ExecStart=\"/opt/snapback/snapback\""));
    CHECK(std::filesystem::exists(enable_link_path(dir.path)));
    CHECK(enable_link_path(dir.path).parent_path().filename() ==
          "graphical-session.target.wants");
}

TEST_CASE("remove_unit clears both the link and the unit, leaving nothing dangling") {
    UnitDir dir;
    REQUIRE(install_unit(dir.path, "/opt/snapback/snapback"));
    REQUIRE(remove_unit(dir.path));

    CHECK_FALSE(unit_enabled(dir.path));
    CHECK_FALSE(std::filesystem::exists(unit_path(dir.path)));
    // A link left pointing at a deleted unit makes systemd warn on every login.
    std::error_code ec;
    CHECK_FALSE(std::filesystem::exists(std::filesystem::symlink_status(enable_link_path(dir.path), ec)));
}

TEST_CASE("removing a unit that is already gone is success, not failure") {
    UnitDir dir;
    CHECK(remove_unit(dir.path));
    CHECK_FALSE(unit_enabled(dir.path));
}

TEST_CASE("install_unit refuses a unit that points nowhere") {
    UnitDir dir;
    CHECK_FALSE(install_unit(dir.path, ""));
    CHECK_FALSE(unit_enabled(dir.path));
}

TEST_CASE("an empty directory is refused rather than resolved against the process CWD") {
    CHECK_FALSE(unit_enabled(""));
    CHECK_FALSE(install_unit("", "/opt/snapback/snapback"));
    CHECK_FALSE(remove_unit(""));
}

// Roadmap 9.4's upgrade case: reinstalled to a new path, the login entry must not still name
// the old binary — and re-enabling must not fail because the link is already there.
TEST_CASE("install_unit is repeatable and points at the newest path") {
    UnitDir dir;
    REQUIRE(install_unit(dir.path, "/old/snapback"));
    REQUIRE(install_unit(dir.path, "/new/snapback"));

    CHECK(unit_enabled(dir.path));
    const auto unit = read_file(unit_path(dir.path));
    CHECK(contains(unit, "/new/snapback"));
    CHECK_FALSE(contains(unit, "/old/snapback"));
}

TEST_CASE("user_unit_dir prefers XDG_CONFIG_HOME and falls back to HOME") {
    // Reads the ambient environment rather than mutating it: setenv during a test run is
    // process-global and would leak into every other test in this binary.
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const auto dir = user_unit_dir();

    if (xdg != nullptr && *xdg != '\0') {
        CHECK(dir == std::filesystem::path(xdg) / "systemd" / "user");
    } else if (home != nullptr && *home != '\0') {
        CHECK(dir == std::filesystem::path(home) / ".config" / "systemd" / "user");
    } else {
        CHECK(dir.empty());
    }
}

#if defined(__linux__)

TEST_CASE("current_executable_path resolves this test binary through /proc/self/exe") {
    const auto path = current_executable_path();
    REQUIRE_FALSE(path.empty());
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::path(path).is_absolute());
}

#else

TEST_CASE("current_executable_path is empty where /proc/self/exe does not exist") {
    CHECK(current_executable_path().empty());
}

#endif
