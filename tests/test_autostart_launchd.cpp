#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/autostart_launchd.hpp"

using namespace snapback;
using namespace snapback::launchd;

namespace {

// Roadmap 11.7's complaint about the Windows autostart test is that it writes the real
// machine's registry. The launchd backend takes its directory as an argument precisely so this
// fixture can exist: nothing here can register the test binary to start at login.
struct AgentDir {
    std::filesystem::path path;

    AgentDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_launchd_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    ~AgentDir() {
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

// launchd ignores a malformed agent without reporting anything, so a broken plist looks
// exactly like a working one until the user notices Snapback never starts. That makes the
// escaping a correctness concern, not a formatting one.
TEST_CASE("escape_xml escapes the three characters a macOS path may contain") {
    CHECK(escape_xml("/Users/kassa/R&D") == "/Users/kassa/R&amp;D");
    CHECK(escape_xml("/Users/kassa/<draft>") == "/Users/kassa/&lt;draft&gt;");
    CHECK(escape_xml("/Applications/Snapback.app") == "/Applications/Snapback.app");
}

TEST_CASE("escape_xml does not double-escape its own output") {
    // The bug this pins: replacing `<` before `&` turns `&lt;` into `&amp;lt;`. Escaping in a
    // single pass makes that unrepresentable, and this asserts the single pass stays.
    CHECK(escape_xml("a & b < c") == "a &amp; b &lt; c");
}

TEST_CASE("agent_plist declares a login item and nothing more") {
    const auto plist = agent_plist("/Applications/Snapback.app/Contents/MacOS/snapback");

    CHECK(contains(plist, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
    CHECK(contains(plist, "<!DOCTYPE plist PUBLIC"));
    CHECK(contains(plist, "<key>Label</key>"));
    CHECK(contains(plist, "<string>com.snapback.app</string>"));
    CHECK(contains(plist, "<string>/Applications/Snapback.app/Contents/MacOS/snapback</string>"));
    CHECK(contains(plist, "<key>RunAtLoad</key>"));
    CHECK(contains(plist, "<true/>"));

    // KeepAlive would have launchd relaunch Snapback every time the user quits it, which
    // turns a login item into an app that cannot be closed.
    CHECK_FALSE(contains(plist, "KeepAlive"));
}

TEST_CASE("agent_plist escapes the executable path it embeds") {
    const auto plist = agent_plist("/Users/kassa/R&D/snapback");
    CHECK(contains(plist, "<string>/Users/kassa/R&amp;D/snapback</string>"));
}

TEST_CASE("agent_path names the file after the label, as launchd expects") {
    CHECK(agent_path("/tmp/agents").filename() == "com.snapback.app.plist");
}

TEST_CASE("install_agent then remove_agent round-trips through a real directory") {
    AgentDir dir;
    CHECK_FALSE(agent_installed(dir.path));

    // The directory does not exist yet: a fresh macOS account has no ~/Library/LaunchAgents
    // until something creates it, and that is the first-run path.
    REQUIRE(install_agent(dir.path, "/Applications/Snapback.app/Contents/MacOS/snapback"));
    CHECK(agent_installed(dir.path));
    CHECK(contains(read_file(agent_path(dir.path)), "<key>RunAtLoad</key>"));

    REQUIRE(remove_agent(dir.path));
    CHECK_FALSE(agent_installed(dir.path));
}

TEST_CASE("removing an agent that is already gone is success, not failure") {
    AgentDir dir;
    // The caller asked for "not enabled" and that is the resulting state. Reporting failure
    // here would make the settings toggle show an error for doing nothing wrong.
    CHECK(remove_agent(dir.path));
    CHECK_FALSE(agent_installed(dir.path));
}

TEST_CASE("install_agent refuses to write an agent that points nowhere") {
    AgentDir dir;
    // An agent with an empty ProgramArguments entry is worse than no agent: launchd would
    // retry it at every login forever.
    CHECK_FALSE(install_agent(dir.path, ""));
    CHECK_FALSE(agent_installed(dir.path));
}

TEST_CASE("an empty directory is refused rather than resolved against the process CWD") {
    CHECK_FALSE(agent_installed(""));
    CHECK_FALSE(install_agent("", "/Applications/Snapback.app/Contents/MacOS/snapback"));
    CHECK_FALSE(remove_agent(""));
}

TEST_CASE("install_agent overwrites a stale agent instead of appending to it") {
    AgentDir dir;
    REQUIRE(install_agent(dir.path, "/old/path/snapback"));
    REQUIRE(install_agent(dir.path, "/new/path/snapback"));

    const auto plist = read_file(agent_path(dir.path));
    CHECK(contains(plist, "/new/path/snapback"));
    // The upgrade case from Roadmap 9.4: after reinstalling to a new location, the login item
    // must not still name the deleted binary.
    CHECK_FALSE(contains(plist, "/old/path/snapback"));
}

#if defined(__APPLE__)

TEST_CASE("the real agent directory is under the user's Library") {
    const auto dir = user_agent_dir();
    REQUIRE_FALSE(dir.empty());
    CHECK(dir.filename() == "LaunchAgents");
    CHECK(dir.parent_path().filename() == "Library");
}

TEST_CASE("current_executable_path finds this test binary") {
    const auto path = current_executable_path();
    REQUIRE_FALSE(path.empty());
    CHECK(std::filesystem::exists(path));
    // Absolute and symlink-resolved: launchd runs it at login from a different working
    // directory, so a relative path would silently never start.
    CHECK(std::filesystem::path(path).is_absolute());
}

#endif
