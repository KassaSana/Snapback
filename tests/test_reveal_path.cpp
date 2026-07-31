#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/reveal_path.hpp"

using namespace snapback;

namespace {

// Unique per call, matching test_support_bundle.cpp: a leftover path from a crashed run must
// not be able to decide whether a later run passes.
std::filesystem::path temp_path(const std::string& leaf) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("snapback_reveal_" + leaf + "_" + std::to_string(ticks));
}

}  // namespace

// The argv contract is the security boundary, so it is pinned on every OS rather than only
// where it runs: the path must be its own element. If it were ever concatenated into a single
// string, a directory named `x; rm -rf ~` would stop being a directory name.
TEST_CASE("file_manager_argv keeps the path as one argument, never shell text") {
    const auto argv = file_manager_argv("/home/kassa/My Data; rm -rf ~");
    REQUIRE(argv.size() == 2);
    CHECK(argv[0] == "xdg-open");
    CHECK(argv[1] == "/home/kassa/My Data; rm -rf ~");
}

TEST_CASE("file_manager_argv passes a path with spaces through unquoted") {
    // Quoting here would be a bug: posix_spawnp does not strip quotes, so the child would
    // receive a directory whose name literally contains them.
    const auto argv = file_manager_argv("/Users/kassa/Library/Application Support/Snapback");
    REQUIRE(argv.size() == 2);
    CHECK(argv[1] == "/Users/kassa/Library/Application Support/Snapback");
}

// Everything below is about refusing rather than guessing. None of these reaches a backend, so
// they run identically on a developer's desktop and on a headless CI runner — a test that
// actually opened Finder would be untrustworthy in exactly the environment CI provides.
TEST_CASE("reveal_directory refuses an empty path") {
    CHECK_FALSE(reveal_directory(""));
}

TEST_CASE("reveal_directory refuses a path that does not exist") {
    const auto missing = temp_path("missing");
    REQUIRE_FALSE(std::filesystem::exists(missing));
    CHECK_FALSE(reveal_directory(missing));
}

TEST_CASE("reveal_directory refuses a regular file") {
    const auto file = temp_path("file");
    {
        std::ofstream out(file);
        out << "not a directory";
    }
    REQUIRE(std::filesystem::is_regular_file(file));

    // The command reveals the *data folder*; handing a file to the OS opener would launch
    // whatever application claims that extension, which is not what the button promises.
    CHECK_FALSE(reveal_directory(file));

    std::filesystem::remove(file);
}

TEST_CASE("reveal_supported is true on the platforms with a backend") {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    CHECK(reveal_supported());
#else
    // No backend: the UI hides the control rather than offering a button that does nothing.
    CHECK_FALSE(reveal_supported());
    CHECK_FALSE(reveal_directory(std::filesystem::temp_directory_path()));
#endif
}
