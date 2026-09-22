#include "doctest_wrapper.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/async_command_runner.hpp"
#include "app/command_handlers.hpp"
#include "app/command_registry.hpp"
#include "app/events.hpp"
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

nlohmann::json load_fixture() {
    return nlohmann::json::parse(
        read_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "ipc_commands.json"));
}

std::set<std::string> load_expected_commands() {
    // The fixture is bound to a local, not iterated off the temporary: `at()` returns a
    // reference into the json, and before C++23 the temporary dies before the loop body runs.
    const auto fixture = load_fixture();
    std::set<std::string> out;
    for (const auto& name : fixture.at("commands")) {
        out.insert(name.get<std::string>());
    }
    return out;
}

// Roadmap 11.13. Events the binary actually emits, and events the frontend is allowed to
// listen for ahead of an emitter because an open roadmap item owns the other half.
std::set<std::string> load_emitted_events() {
    const auto fixture = load_fixture();
    std::set<std::string> out;
    for (const auto& name : fixture.at("events").at("emitted")) {
        out.insert(name.get<std::string>());
    }
    return out;
}

std::map<std::string, std::string> load_planned_events() {
    const auto fixture = load_fixture();
    std::map<std::string, std::string> out;
    for (const auto& [name, owner] : fixture.at("events").at("planned").items()) {
        out.emplace(name, owner.get<std::string>());
    }
    return out;
}

std::set<std::string> registry_event_names() {
    return {events::kAll.begin(), events::kAll.end()};
}

std::set<std::string> extract_frontend_listens(const std::string& source) {
    std::set<std::string> out;
    // A line-anchored scan rather than `<[^>]*>`: almost every call is written
    // `listen<Record<string, unknown>>(`, and a bracket class that stops at the first `>`
    // cannot cross that nested generic. The `invoke` pattern below had exactly that bug and
    // saw 12 of 73 names -- a subset test passing on a twelfth of the surface, which is the
    // same "nothing could notice" this item is about.
    static const std::regex pattern(R"re(listen[^\n(]*\("([a-z0-9_-]+)")re");
    std::sregex_iterator it(source.begin(), source.end(), pattern);
    const std::sregex_iterator end;
    for (; it != end; ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

// Every emit call site in the product, so a name that never reached `events.hpp` cannot hide
// as a string literal. Kept to the three call shapes that actually push to the frontend:
// `AppState::emit_event`, the engine tick's `emit_to_frontend` hook, and main.cpp's `emit(w, ...)`.
std::vector<std::string> raw_string_emit_sites() {
    std::vector<std::string> out;
    static const std::regex pattern(
        R"re((emit_event|emit_to_frontend)\("|emit\(w, ")re");
    const std::filesystem::path src = std::filesystem::path(SNAPBACK_SOURCE_DIR) / "src";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") continue;
        if (entry.path().filename() == "events.hpp") continue;
        const auto source = read_file(entry.path());
        if (std::regex_search(source, pattern)) {
            out.push_back(entry.path().filename().string());
        }
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
    // See extract_frontend_listens: this pattern used to stop at the first `>`, so it could
    // not see the 47 calls written as `invoke<Record<string, unknown>>(`.
    static const std::regex pattern(R"re(invoke[^\n(]*\("([a-z0-9_]+)")re");
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

TEST_CASE("IPC contract: the emitted event set matches the canonical fixture") {
    // Roadmap 11.13. The commands had a registry, a fixture, and this test; events had none of
    // the three, and four dead `listen(...)` calls accumulated because nothing could notice.
    // `events::kAll` is the registry's equivalent for events.
    const auto expected = load_emitted_events();
    const auto emitted = registry_event_names();
    for (const auto& name : emitted) {
        CAPTURE(name);
        CHECK_MESSAGE(expected.count(name) == 1, "emitted but missing from the fixture");
    }
    for (const auto& name : expected) {
        CAPTURE(name);
        CHECK_MESSAGE(emitted.count(name) == 1, "in the fixture but never emitted");
    }
    CHECK(emitted == expected);
}

TEST_CASE("IPC contract: no emit site names an event with a raw string") {
    // `events::kAll` is only the truth if every emit site is spelled from it. Without this, a
    // new `emit_event("something-new", ...)` would pass the test above by never appearing in
    // either set -- exactly how the four dead listeners survived.
    const auto offenders = raw_string_emit_sites();
    for (const auto& file : offenders) {
        CAPTURE(file);
        CHECK_MESSAGE(false, "emit site uses a string literal; use a constant from events.hpp");
    }
    CHECK(offenders.empty());
}

TEST_CASE("IPC contract: every frontend listener has an emitter or a named owner") {
    // The rule this file exists to enforce, extended to events: a `listen(...)` for a name
    // nothing emits fails the build, unless the fixture records the roadmap item that owns
    // the missing half. 9.6 (persistence-failed) and 12.6 (label-hotkey) each deliberately
    // shipped the frontend scaffolding first and say so; `capture-failed` and `overlay-failed`
    // had no such owner and were deleted.
    const auto emitted = load_emitted_events();
    const auto planned = load_planned_events();
    const auto source =
        read_file(std::filesystem::path(SNAPBACK_SOURCE_DIR) / "frontend/src/api.ts");
    const auto listened = extract_frontend_listens(source);
    REQUIRE_FALSE(listened.empty());  // a regex that matches nothing would pass silently
    for (const auto& name : listened) {
        CAPTURE(name);
        const bool claimed = emitted.count(name) == 1 || planned.count(name) == 1;
        CHECK_MESSAGE(claimed,
                      "listened for but never emitted, and no roadmap item claims it");
    }

    // A planned event that has since gained an emitter must move to `emitted`, or the fixture
    // keeps advertising an open item that is actually closed.
    for (const auto& [name, owner] : planned) {
        CAPTURE(name);
        CAPTURE(owner);
        CHECK_MESSAGE(emitted.count(name) == 0, "emitted now; move it out of 'planned'");
        CHECK_FALSE(owner.empty());
    }
}

TEST_CASE("IPC contract: canonical set has 74 handler commands") {
    // 61 -> 65 with Roadmap 9.14's import path; 66 with Roadmap 2.8; 68 with Roadmap 10.14 file
    // dialogs; 71 with Roadmap 2.16's alert snooze, resume, and delivery preferences; 72 with
    // the daily focus series behind the Review trend surfaces; 73 with cancel_training once
    // a run could actually be stopped (Roadmap 14.6); 74 is the test-build-only acceptance
    // verdict sink that proves the page crossed the real webview bridge (Roadmap 10.1).
    CHECK(load_expected_commands().size() == 74);
}



