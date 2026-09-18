#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/acceptance_harness.hpp"

using namespace snapback;
using nlohmann::json;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_acceptance_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

}  // namespace

TEST_CASE("acceptance script loading refuses missing and empty input") {
    TempDir temp;
    CHECK_THROWS_WITH_AS(load_acceptance_script(temp.path / "missing.js"),
                         doctest::Contains("could not be opened"), std::runtime_error);

    const auto empty = temp.path / "empty.js";
    std::ofstream(empty).close();
    CHECK_THROWS_WITH_AS(load_acceptance_script(empty), doctest::Contains("is empty"),
                         std::runtime_error);
}

TEST_CASE("acceptance script loading preserves the page-side program") {
    TempDir temp;
    const auto script = temp.path / "acceptance.js";
    std::ofstream(script, std::ios::binary) << "window.acceptanceRan = true;\n";
    CHECK(load_acceptance_script(script) == "window.acceptanceRan = true;\n");
}

TEST_CASE("acceptance verdict publication is complete JSON and replaces stale output") {
    TempDir temp;
    const auto verdict = temp.path / "acceptance-verdict.json";
    std::ofstream(verdict) << "stale";

    write_acceptance_verdict(verdict, json{{"passed", true}, {"checks", json::array({"ipc"})}});

    std::ifstream in(verdict);
    const auto parsed = json::parse(in);
    CHECK(parsed.at("passed") == true);
    CHECK(parsed.at("checks").size() == 1);
    CHECK_FALSE(std::filesystem::exists(verdict.string() + ".tmp"));
}

TEST_CASE("acceptance verdict publication refuses a non-object") {
    TempDir temp;
    CHECK_THROWS_WITH_AS(write_acceptance_verdict(temp.path / "verdict.json", json::array()),
                         doctest::Contains("must be a JSON object"), std::runtime_error);
}
