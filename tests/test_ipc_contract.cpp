#include "doctest_wrapper.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/async_command_runner.hpp"
#include "app/command_handlers.hpp"
#include "app/command_registry.hpp"
#include "app/state.hpp"

#ifndef SNAPBACK_FIXTURES_DIR
#define SNAPBACK_FIXTURES_DIR "fixtures"
#endif

#ifndef SNAPBACK_SOURCE_DIR
#define SNAPBACK_SOURCE_DIR "."
#endif

using namespace snapback;

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

// The registry the app binds, built the way main.cpp builds it but against an in-memory
// AppState (Roadmap 14.3). Names come from the real registration calls, so a handler that
// is registered under the wrong name, twice, or not at all fails here rather than in a
// running window. The regex over commands.hpp that this replaced could not see a handler
// at all, only the text that mentioned it.
struct RegisteredCommands {
    std::unique_ptr<AppState> state;
    detail::AsyncCommandRunner runner;
    CommandRegistry registry;
    std::filesystem::path data_dir;

    RegisteredCommands() {
        auto storage = Storage::open_memory();
        if (!storage) throw std::runtime_error("failed to open in-memory storage");
        state = std::make_unique<AppState>(std::move(*storage));
        data_dir = std::filesystem::temp_directory_path() / "snapback_cpp_ipc_contract";
        register_command_handlers(registry, *state, data_dir, runner);
    }
    ~RegisteredCommands() { runner.shutdown(); }
};

std::set<std::string> registered_command_names() {
    RegisteredCommands commands;
    const auto names = commands.registry.names();
    return {names.begin(), names.end()};
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

TEST_CASE("IPC contract: the registered command set matches the canonical fixture") {
    const auto expected = load_expected_commands();
    const auto registered = registered_command_names();
    // Named both ways so a failure says which side the stray is on.
    for (const auto& name : registered) {
        CAPTURE(name);
        CHECK_MESSAGE(expected.count(name) == 1, "registered but missing from the fixture");
    }
    for (const auto& name : expected) {
        CAPTURE(name);
        CHECK_MESSAGE(registered.count(name) == 1, "in the fixture but not registered");
    }
    CHECK(registered == expected);
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

TEST_CASE("IPC contract: canonical set has 73 handler commands") {
    // 61 -> 65 with Roadmap 9.14's import path; 66 with Roadmap 2.8; 68 with Roadmap 10.14 file
    // dialogs; 71 with Roadmap 2.16's alert snooze, resume, and delivery preferences; 72 with
    // the daily focus series behind the Review trend surfaces; 73 with cancel_training once
    // a run could actually be stopped (Roadmap 14.6).
    CHECK(load_expected_commands().size() == 73);
}



