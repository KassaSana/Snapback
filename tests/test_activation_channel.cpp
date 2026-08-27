// ROADMAP 9.15. The channel a losing second launch uses to raise the running window.
#include "doctest_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "app/activation_channel.hpp"
#include "app/single_instance.hpp"

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace snapback;

namespace {

struct ChannelTempDir {
    std::filesystem::path path;

    ChannelTempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_activation_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~ChannelTempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// Wait for a counter to reach `want`, or give up. The listener acknowledges *before* it runs
// the callback, so a request can return Activated a few microseconds before the count moves;
// polling here rather than sleeping a fixed amount keeps the case fast and non-flaky.
bool wait_for_count(const ActivationListener& listener, std::uint64_t want) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (listener.activation_count() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool run_activation_probe(const std::filesystem::path& data_dir, const char* expected) {
#if defined(_WIN32)
    const auto probe = std::filesystem::path(SNAPBACK_INSTANCE_PROBE).wstring();
    const auto data_wide = data_dir.wstring();
    const auto expected_wide = std::filesystem::path(expected).wstring();
    return _wspawnl(_P_WAIT, probe.c_str(), probe.c_str(), L"activate", data_wide.c_str(),
                    expected_wide.c_str(), static_cast<wchar_t*>(nullptr)) == 0;
#else
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        execl(SNAPBACK_INSTANCE_PROBE, SNAPBACK_INSTANCE_PROBE, "activate", data_dir.c_str(),
              expected, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

}  // namespace

TEST_CASE("activation channel id is stable per data directory and differs across them") {
    ChannelTempDir first;
    ChannelTempDir second;

    CHECK(activation_channel_id(first.path) == activation_channel_id(first.path));
    CHECK(activation_channel_id(first.path) != activation_channel_id(second.path));
    CHECK(activation_channel_id(first.path).size() == 16);

    // The two processes never exchange the id before using it, so spellings of the same
    // directory have to agree by construction. A trailing separator and a `..` hop are the two
    // that a shortcut, a shell, and an installer actually produce.
    CHECK(activation_channel_id(first.path) == activation_channel_id(first.path / "child" / ".."));
    CHECK(activation_channel_id(first.path) == activation_channel_id(first.path.string() + "/"));

    CHECK_FALSE(activation_endpoint_for(first.path).empty());
    CHECK(activation_endpoint_for(first.path) != activation_endpoint_for(second.path));
}

TEST_CASE("a unix socket path falls back to the temp directory when sun_path is too short") {
    const std::filesystem::path data_dir("/home/someone/Library/Application Support/Snapback");
    const std::filesystem::path temp_dir("/tmp");

    // Roomy: the socket belongs beside the lock that owns it.
    const auto inside = detail::unix_socket_path(data_dir, "abc123", temp_dir, 512);
    CHECK(inside == (data_dir / "activate.sock").string());

    // Cramped: it moves, and takes the channel id with it — two data directories sharing /tmp
    // must not share one endpoint.
    const auto moved = detail::unix_socket_path(data_dir, "abc123", temp_dir, 32);
    CHECK(moved.find("/tmp") == 0);
    CHECK(moved.find("abc123") != std::string::npos);
    CHECK(moved.size() < 32);
    CHECK(detail::unix_socket_path(data_dir, "def456", temp_dir, 32) != moved);
}

TEST_CASE("an activation request with no owner reports NoOwner without burning the budget") {
    ChannelTempDir temp;

    const auto started = std::chrono::steady_clock::now();
    const auto result = request_activation(temp.path, kActivationTimeoutMs);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();

    CHECK(result == ActivationResult::NoOwner);
    // The whole point of separating NoOwner from TimedOut: a first launch must not stare at
    // nothing for a second and a half before deciding it is the owner.
    CHECK(elapsed < kActivationTimeoutMs / 2);
}

TEST_CASE("the owner acknowledges a request and runs the handler exactly once") {
    ChannelTempDir temp;
    std::atomic<int> raised{0};

    auto listener = ActivationListener::start(temp.path, [&raised] { raised.fetch_add(1); });
    REQUIRE(listener.has_value());

    CHECK(request_activation(temp.path, kActivationTimeoutMs) == ActivationResult::Activated);
    REQUIRE(wait_for_count(*listener, 1));
    CHECK(raised.load() == 1);

    // A second click on a dock icon is an ordinary thing to do. The listener accepts serially,
    // so this also proves it went back to accepting rather than servicing one caller and
    // wedging — the failure that would look like "activation works" in a single-request test.
    CHECK(request_activation(temp.path, kActivationTimeoutMs) == ActivationResult::Activated);
    REQUIRE(wait_for_count(*listener, 2));
    CHECK(raised.load() == 2);
}

TEST_CASE("a request naming a different channel is refused and raises nothing") {
    ChannelTempDir owner_dir;
    ChannelTempDir other_dir;
    std::atomic<int> raised{0};

    auto listener = ActivationListener::start(owner_dir.path, [&raised] { raised.fetch_add(1); });
    REQUIRE(listener.has_value());

    // Reaches the owner's endpoint carrying somebody else's id — a hash collision, or a build
    // that computed the id differently. Raising the window for a different database would be
    // worse than doing nothing, so the owner must say no rather than guess.
    const auto foreign = detail::activation_request_line(activation_channel_id(other_dir.path));
    CHECK(detail::send_activation_request(owner_dir.path, foreign, kActivationTimeoutMs) ==
          ActivationResult::Refused);
    CHECK(detail::send_activation_request(owner_dir.path, "hello\n", kActivationTimeoutMs) ==
          ActivationResult::Refused);

    // Still answering afterwards: a refusal is not a reason to stop listening.
    CHECK(request_activation(owner_dir.path, kActivationTimeoutMs) == ActivationResult::Activated);
    REQUIRE(wait_for_count(*listener, 1));
    CHECK(raised.load() == 1);
}

TEST_CASE("activation request line and matcher agree, and disagree with everything else") {
    const std::string id = "0123456789abcdef";
    const auto line = detail::activation_request_line(id);

    CHECK(line.back() == '\n');
    CHECK(detail::activation_request_matches(line, id));
    // Framing is by newline, and a carriage return is what a pipe on Windows can add.
    CHECK(detail::activation_request_matches(std::string(line).insert(line.size() - 1, "\r"), id));
    CHECK_FALSE(detail::activation_request_matches(line, "fedcba9876543210"));
    CHECK_FALSE(detail::activation_request_matches("", id));
    CHECK_FALSE(detail::activation_request_matches("SNAPBACK-ACTIVATE 2 " + id, id));
    CHECK_FALSE(detail::activation_request_matches("SOMETHING-ELSE 1 " + id, id));
    // A prefix of a valid line is not a valid line: framing on the newline is what makes a
    // truncated write an incomplete request rather than a shorter accepted one.
    CHECK_FALSE(detail::activation_request_matches(line.substr(0, line.size() - 4), id));
}

TEST_CASE("an owner that has gone away leaves requests reporting NoOwner, not hanging") {
    ChannelTempDir temp;
    {
        auto listener = ActivationListener::start(temp.path, [] {});
        REQUIRE(listener.has_value());
        CHECK(request_activation(temp.path, kActivationTimeoutMs) == ActivationResult::Activated);
    }

    const auto started = std::chrono::steady_clock::now();
    CHECK(request_activation(temp.path, kActivationTimeoutMs) == ActivationResult::NoOwner);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    CHECK(elapsed < kActivationTimeoutMs);
}

TEST_CASE("a second process can activate the owner") {
    ChannelTempDir temp;
    std::atomic<int> raised{0};

    auto listener = ActivationListener::start(temp.path, [&raised] { raised.fetch_add(1); });
    REQUIRE(listener.has_value());

    CHECK(run_activation_probe(temp.path, "activated"));
    REQUIRE(wait_for_count(*listener, 1));
    CHECK(raised.load() == 1);
}

TEST_CASE("a second process finds no owner once the listener is gone") {
    ChannelTempDir temp;
    {
        auto listener = ActivationListener::start(temp.path, [] {});
    }
    CHECK(run_activation_probe(temp.path, "noowner"));
}

#if !defined(_WIN32)
TEST_CASE("a socket file left by a crash does not block the next owner") {
    ChannelTempDir temp;

    // What a crash leaves: the node, with nothing bound to it. Nothing else in this test is
    // decoration — the lock is held for real, because holding it is the *entire* justification
    // for unlinking a socket that might otherwise belong to a live owner.
    auto lock = SingleInstanceGuard::acquire(temp.path / "snapback.lock");
    REQUIRE(lock.acquired());
    const auto endpoint = activation_endpoint_for(temp.path);
    {
        std::ofstream stale(endpoint);
    }
    REQUIRE(std::filesystem::exists(endpoint));

    std::atomic<int> raised{0};
    auto listener = ActivationListener::start(temp.path, [&raised] { raised.fetch_add(1); });
    REQUIRE(listener.has_value());
    CHECK(request_activation(temp.path, kActivationTimeoutMs) == ActivationResult::Activated);
    REQUIRE(wait_for_count(*listener, 1));
    CHECK(raised.load() == 1);
}

TEST_CASE("the endpoint is removed when the listener stops") {
    ChannelTempDir temp;
    const auto endpoint = activation_endpoint_for(temp.path);
    {
        auto listener = ActivationListener::start(temp.path, [] {});
        REQUIRE(listener.has_value());
        CHECK(std::filesystem::exists(endpoint));
    }
    CHECK_FALSE(std::filesystem::exists(endpoint));
}
#endif
