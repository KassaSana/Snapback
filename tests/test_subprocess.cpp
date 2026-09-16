#include "doctest_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "util/subprocess.hpp"

using namespace snapback;
using namespace std::chrono_literals;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_subprocess_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The production code never runs a shell; these tests do, because a shell is the one
// program every CI runner has that can exit with a chosen code or write to both streams.
std::vector<std::string> shell(const std::string& script) {
#if defined(_WIN32)
    return {"cmd", "/d", "/c", script};
#else
    return {"sh", "-c", script};
#endif
}

// Something that runs long enough to be cancelled and would otherwise finish on its own.
std::vector<std::string> sleeper() {
#if defined(_WIN32)
    // `timeout` refuses a redirected stdin; ping's per-reply wait is the idiom instead.
    return {"cmd", "/d", "/c", "ping", "-n", "30", "127.0.0.1"};
#else
    return {"sleep", "30"};
#endif
}

}  // namespace

TEST_CASE("windows_command_line leaves plain arguments bare and quotes the rest") {
    using subprocess::detail::windows_command_line;
    CHECK(windows_command_line({"py", "-3", "--version"}) == "py -3 --version");
    CHECK(windows_command_line({"python", "C:\\app data\\x"}) == "python \"C:\\app data\\x\"");
    // An empty argument still has to occupy a slot.
    CHECK(windows_command_line({"a", "", "b"}) == "a \"\" b");
}

TEST_CASE("windows_command_line escapes quotes and the backslashes that precede them") {
    using subprocess::detail::windows_command_line;
    // The C runtime reads \" as a literal quote, so a quote inside an argument needs its
    // own escaping backslash ...
    CHECK(windows_command_line({"a\"b"}) == "\"a\\\"b\"");
    // ... and backslashes that precede it are doubled so they stay literal.
    CHECK(windows_command_line({"a\\\"b"}) == "\"a\\\\\\\"b\"");
    // A trailing backslash would otherwise escape the closing quote we add.
    CHECK(windows_command_line({"C:\\dir with space\\"}) == "\"C:\\dir with space\\\\\"");
    // Backslashes not followed by a quote are literal and untouched.
    CHECK(windows_command_line({"C:\\a b\\c"}) == "\"C:\\a b\\c\"");
}

TEST_CASE("exit_code_from_wait_status follows the shell convention") {
    using subprocess::detail::exit_code_from_wait_status;
    CHECK(exit_code_from_wait_status(0) == 0);
#if defined(_WIN32)
    CHECK(exit_code_from_wait_status(2) == 2);
#else
    CHECK(exit_code_from_wait_status(512) == 2);          // 2 << 8
    CHECK(exit_code_from_wait_status(256) == 1);
    CHECK(exit_code_from_wait_status(9) == 128 + 9);      // killed by SIGKILL
    CHECK(exit_code_from_wait_status(15) == 128 + 15);    // SIGTERM
#endif
}

TEST_CASE("run reports the child's own exit code") {
    const auto ok = subprocess::run({shell("exit 0")}, nullptr);
    CHECK(ok.started);
    CHECK_FALSE(ok.cancelled);
    CHECK(ok.exit_code == 0);

    // The training pipeline signals "majority-classifier stub" with exit 2, so this code in
    // particular has to arrive undecorated.
    const auto stub = subprocess::run({shell("exit 2")}, nullptr);
    CHECK(stub.started);
    CHECK(stub.exit_code == 2);
}

TEST_CASE("run captures stdout and stderr into one file") {
    TempDir dir;
    const auto log = dir.path / "child.log";
#if defined(_WIN32)
    const auto script = "echo out & echo err 1>&2";
#else
    const auto script = "echo out; echo err >&2";
#endif
    subprocess::SpawnRequest request;
    request.argv = shell(script);
    request.output_path = log;
    const auto result = subprocess::run(request, nullptr);
    REQUIRE(result.started);
    CHECK(result.exit_code == 0);

    const auto text = read_text(log);
    CHECK(text.find("out") != std::string::npos);
    CHECK(text.find("err") != std::string::npos);
}

TEST_CASE("run truncates a stale output file") {
    TempDir dir;
    const auto log = dir.path / "child.log";
    {
        std::ofstream stale(log);
        stale << "leftover from the previous run\n";
    }
    subprocess::SpawnRequest request;
    request.argv = shell("echo fresh");
    request.output_path = log;
    REQUIRE(subprocess::run(request, nullptr).started);
    const auto text = read_text(log);
    CHECK(text.find("fresh") != std::string::npos);
    CHECK(text.find("leftover") == std::string::npos);
}

TEST_CASE("run starts the child in the requested directory") {
    // A marker created with a relative name lands wherever the child's cwd is; checking for
    // it sidesteps every platform's own idea of how to print a path.
    TempDir dir;
    subprocess::SpawnRequest request;
    request.argv = shell("mkdir marker");
    request.cwd = dir.path;
    const auto result = subprocess::run(request, nullptr);
    REQUIRE(result.started);
    CHECK(result.exit_code == 0);
    CHECK(std::filesystem::is_directory(dir.path / "marker"));
}

TEST_CASE("run treats a missing program as not started, not as a failed child") {
    const auto result = subprocess::run({{"snapback-no-such-program-4b1c"}}, nullptr);
    CHECK_FALSE(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exit_code == subprocess::kExitNotStarted);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("run refuses an empty argv") {
    const auto result = subprocess::run({}, nullptr);
    CHECK_FALSE(result.started);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("run terminates the child when the cancel flag is raised") {
    std::atomic<bool> cancel{false};
    std::thread raiser([&cancel] {
        std::this_thread::sleep_for(200ms);
        cancel.store(true, std::memory_order_release);
    });

    const auto started_at = std::chrono::steady_clock::now();
    const auto result = subprocess::run(
        {sleeper()}, [&cancel] { return cancel.load(std::memory_order_acquire); }, 20ms);
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    raiser.join();

    REQUIRE(result.started);
    CHECK(result.cancelled);
    // The sleeper would have taken ~30 s on its own; the cancel has to cut that short by
    // a wide margin, with room for the POSIX SIGTERM grace period.
    CHECK(elapsed < 10s);
}

TEST_CASE("a cancel raised before the run starts is honoured without waiting") {
    const auto started_at = std::chrono::steady_clock::now();
    const auto result = subprocess::run({sleeper()}, [] { return true; }, 20ms);
    CHECK(result.started);
    CHECK(result.cancelled);
    CHECK(std::chrono::steady_clock::now() - started_at < 10s);
}

TEST_CASE("wait_for returns nullopt while the child runs and the code once it ends") {
    auto child = subprocess::ChildProcess::spawn({sleeper()});
    REQUIRE(child.has_value());
    CHECK_FALSE(child->wait_for(0ms).has_value());
    CHECK(child->running());

    child->terminate();
    CHECK_FALSE(child->running());
    const auto code = child->wait_for(0ms);
    REQUIRE(code.has_value());
    CHECK(*code != 0);
    // Idempotent: a second terminate on an ended child is a no-op, not a stray signal.
    child->terminate();
    CHECK(child->wait() == *code);
}

TEST_CASE("destroying a running ChildProcess ends the child") {
    // The child pauses, then leaves a marker. Dropping the handle during the pause must end
    // it, so the marker never appears -- which is the observable difference between a
    // destructor that kills and one that merely lets go.
    TempDir dir;
#if defined(_WIN32)
    const auto script = "ping -n 3 127.0.0.1 >NUL & mkdir marker";
#else
    const auto script = "sleep 2; mkdir marker";
#endif
    subprocess::SpawnRequest request;
    request.argv = shell(script);
    request.cwd = dir.path;
    {
        auto child = subprocess::ChildProcess::spawn(request);
        REQUIRE(child.has_value());
        CHECK(child->running());
    }
    std::this_thread::sleep_for(3500ms);
    CHECK_FALSE(std::filesystem::exists(dir.path / "marker"));
}
