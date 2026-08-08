#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "util/durable_file.hpp"

using namespace snapback;

namespace {

std::filesystem::path temp_path(const std::string& leaf) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("snapback_durable_" + leaf + "_" + std::to_string(ticks));
}

}  // namespace

TEST_CASE("durable_sync_file flushes an existing regular file") {
    const auto path = temp_path("file");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "settings";
    }
    CHECK(durable_sync_file(path));
    std::filesystem::remove(path);
}

TEST_CASE("durable_sync_file refuses a missing path") {
    const auto missing = temp_path("missing");
    REQUIRE_FALSE(std::filesystem::exists(missing));
    CHECK_FALSE(durable_sync_file(missing));
}

TEST_CASE("durable_sync_directory flushes an existing directory") {
    const auto dir = temp_path("dir");
    std::filesystem::create_directories(dir);
    CHECK(durable_sync_directory(dir));
    std::filesystem::remove(dir);
}

TEST_CASE("durable_sync_directory refuses a missing path") {
    const auto missing = temp_path("missing_dir");
    REQUIRE_FALSE(std::filesystem::exists(missing));
    CHECK_FALSE(durable_sync_directory(missing));
}
