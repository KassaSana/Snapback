#include "doctest_wrapper.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#ifndef SNAPBACK_FIXTURES_DIR
#define SNAPBACK_FIXTURES_DIR "fixtures"
#endif

#ifndef SNAPBACK_SOURCE_DIR
#define SNAPBACK_SOURCE_DIR "."
#endif

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::set<std::string> load_expected_commands() {
    const auto raw =
        read_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "ipc_commands.json");
    std::set<std::string> out;
    for (const auto& name : nlohmann::json::parse(raw)) {
        out.insert(name.get<std::string>());
    }
    return out;
}

std::set<std::string> extract_bind_commands(const std::string& source) {
    std::set<std::string> out;
    static const std::regex pattern(R"re(bind_cmd\(w,\s*"([a-z0-9_]+)")re");
    std::sregex_iterator it(source.begin(), source.end(), pattern);
    const std::sregex_iterator end;
    for (; it != end; ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

std::set<std::string> extract_frontend_invokes(const std::string& source) {
    std::set<std::string> out;
    static const std::regex pattern(R"re(invoke(?:<[^>]*>)?\("([a-z0-9_]+)")re");
    std::sregex_iterator it(source.begin(), source.end(), pattern);
    const std::sregex_iterator end;
    for (; it != end; ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

}  // namespace

TEST_CASE("IPC contract: C++ bind list matches the canonical command set") {
    const auto expected = load_expected_commands();
    const auto source =
        read_file(std::filesystem::path(SNAPBACK_SOURCE_DIR) / "src/app/commands.hpp");
    const auto bound = extract_bind_commands(source);
    CHECK(bound == expected);
}

TEST_CASE("IPC contract: frontend invoke names are a subset of the canonical set") {
    const auto expected = load_expected_commands();
    const auto source =
        read_file(std::filesystem::path(SNAPBACK_SOURCE_DIR) / "frontend/src/api.ts");
    const auto invoked = extract_frontend_invokes(source);
    for (const auto& name : invoked) {
        CAPTURE(name);
        CHECK(expected.count(name) == 1);
    }
}

TEST_CASE("IPC contract: canonical set has 39 handler commands") {
    CHECK(load_expected_commands().size() == 39);
}
