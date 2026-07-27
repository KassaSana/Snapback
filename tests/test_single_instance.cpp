#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

#include "app/single_instance.hpp"

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace snapback;

namespace {

struct InstanceTempDir {
    std::filesystem::path path;

    InstanceTempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_instance_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~InstanceTempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

}  // namespace

bool run_instance_probe(const std::filesystem::path& lock_path, const char* expected) {
#if defined(_WIN32)
    const auto probe = std::filesystem::path(SNAPBACK_INSTANCE_PROBE).wstring();
    const auto expected_wide = std::filesystem::path(expected).wstring();
    return _wspawnl(_P_WAIT, probe.c_str(), probe.c_str(), lock_path.c_str(),
                    expected_wide.c_str(), static_cast<wchar_t*>(nullptr)) == 0;
#else
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        execl(SNAPBACK_INSTANCE_PROBE, SNAPBACK_INSTANCE_PROBE, lock_path.c_str(),
              expected, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

#if !defined(_WIN32)
pid_t start_hold_probe(const std::filesystem::path& ready_path,
                       const std::filesystem::path& release_path) {
    const pid_t child = fork();
    if (child != 0) return child;
    execl(SNAPBACK_INSTANCE_PROBE, SNAPBACK_INSTANCE_PROBE, "hold",
          ready_path.c_str(), release_path.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}

bool wait_for_path(const std::filesystem::path& path) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (std::filesystem::exists(path)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
#endif

TEST_CASE("single-instance guard rejects a second owner and releases on destruction") {
    InstanceTempDir temp;
    const auto lock_path = temp.path / "snapback.lock";

    {
        auto first = SingleInstanceGuard::acquire(lock_path);
        REQUIRE(first.acquired());

        auto second = SingleInstanceGuard::acquire(lock_path);
        CHECK(second.status() == SingleInstanceStatus::AlreadyRunning);
        CHECK_FALSE(second.acquired());
        CHECK(second.message().find("already running") != std::string::npos);
    }

    auto after_release = SingleInstanceGuard::acquire(lock_path);
    CHECK(after_release.acquired());
}

TEST_CASE("single-instance guard excludes a separate process") {
    InstanceTempDir temp;
    const auto lock_path = temp.path / "snapback.lock";
    {
        auto owner = SingleInstanceGuard::acquire(lock_path);
        REQUIRE(owner.acquired());
        CHECK(run_instance_probe(lock_path, "blocked"));
    }
    CHECK(run_instance_probe(lock_path, "acquired"));
}

#if !defined(_WIN32)
TEST_CASE("single-instance lock closes when a child execs") {
    InstanceTempDir temp;
    const auto lock_path = temp.path / "snapback.lock";
    const auto ready_path = temp.path / "child.ready";
    const auto release_path = temp.path / "child.release";
    pid_t child = -1;

    {
        auto owner = SingleInstanceGuard::acquire(lock_path);
        REQUIRE(owner.acquired());
        child = start_hold_probe(ready_path, release_path);
        REQUIRE(child > 0);
        if (!wait_for_path(ready_path)) {
            std::ofstream(release_path) << "release";
            waitpid(child, nullptr, 0);
            FAIL("child probe did not become ready");
        }
    }

    // The child is still alive. This succeeds only if exec closed its inherited copy of
    // the owner's descriptor; otherwise the child's copy keeps the flock alive.
    auto after_owner_exit = SingleInstanceGuard::acquire(lock_path);
    CHECK(after_owner_exit.acquired());
    std::ofstream(release_path) << "release";
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}
#endif

TEST_CASE("single-instance guard move keeps ownership unique") {
    InstanceTempDir temp;
    const auto lock_path = temp.path / "snapback.lock";

    auto original = SingleInstanceGuard::acquire(lock_path);
    REQUIRE(original.acquired());
    auto owner = std::move(original);

    auto contender = SingleInstanceGuard::acquire(lock_path);
    CHECK(owner.acquired());
    CHECK(contender.status() == SingleInstanceStatus::AlreadyRunning);
}

TEST_CASE("single-instance guard reports an unusable lock path") {
    InstanceTempDir temp;
    auto guard = SingleInstanceGuard::acquire(temp.path / "missing" / "snapback.lock");
    CHECK(guard.status() == SingleInstanceStatus::Error);
    CHECK_FALSE(guard.message().empty());
}
