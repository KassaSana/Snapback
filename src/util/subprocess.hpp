// Run a child process from an argv array, wait on it with a timeout, and kill it.
//
// This replaced std::system in the training path. std::system was three problems in one
// call: it runs a shell, so every user-controlled path had to be quoted against the shell's
// grammar (the repo path once executed `$(...)` because it was double-quoted); it blocks the
// calling thread until the child exits, so a training run of several minutes had no way to
// yield to shutdown; and on Windows a GUI process calling it flashes a console window.
//
// Here the program and each argument occupy their own argv element -- a path is data, never
// program text, the same rule reveal_path.hpp states for its spawn. The child's stdout and
// stderr go to one file (or nowhere); stdin is /dev/null so a child that reads it fails
// rather than waiting on a terminal the app does not have.
//
// Killing a child must kill what it spawned: `py -3` is a launcher that execs python.exe and
// waits, so terminating the launcher alone would orphan the training run. Windows puts the
// child in a Job object with KILL_ON_JOB_CLOSE; POSIX gives it its own process group and
// signals the group.
//
// POSIX spawns with posix_spawnp, never fork(): the app forks with a capture thread running
// and a SQLite connection open, and a forked child inherits locks it must never touch.
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace snapback::subprocess {

struct SpawnRequest {
    // argv[0] is looked up on PATH, as posix_spawnp and CreateProcess both do.
    std::vector<std::string> argv;
    // Working directory for the child; the parent's when unset.
    std::optional<std::filesystem::path> cwd;
    // stdout and stderr, interleaved, truncating the file first. Discarded when unset.
    std::optional<std::filesystem::path> output_path;
};

// Exit code convention: the child's own code when it exited; 128 + signal when a signal
// ended it (the shell convention, so a SIGKILLed child reads as 137 in a log); -1 when the
// child never started or could not be reaped.
constexpr int kExitNotStarted = -1;

class ChildProcess {
public:
    // Nullopt with `error` filled in when the program cannot be started at all. A program
    // that starts and fails is a running child with a non-zero exit code, not an error.
    static std::optional<ChildProcess> spawn(const SpawnRequest& request,
                                             std::string* error = nullptr);

    // A child that is still running when its handle is destroyed is terminated and reaped.
    // Nothing that outlives the object may keep running or leave a zombie behind.
    ~ChildProcess();
    ChildProcess(ChildProcess&&) noexcept;
    // Assignment would silently drop a running child; there is no caller that needs it.
    ChildProcess& operator=(ChildProcess&&) = delete;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // The exit code once the child has ended, nullopt if it is still running after `timeout`.
    std::optional<int> wait_for(std::chrono::milliseconds timeout);
    // Blocks until the child has ended.
    int wait();
    // Ends the child and everything it spawned. Idempotent; a no-op once the child has ended.
    // POSIX sends SIGTERM to the group first and SIGKILL after `grace` so Python can flush
    // its log; Windows terminates the job outright (there is no graceful signal to send).
    void terminate(std::chrono::milliseconds grace = std::chrono::milliseconds(1000));
    bool running();

private:
    struct Impl;
    explicit ChildProcess(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct RunResult {
    bool started{};       // false: `error` says why, and exit_code is kExitNotStarted
    bool cancelled{};     // the cancel flag was seen and the child was terminated
    int exit_code{kExitNotStarted};
    std::string error;
};

// Asked between waits; true ends the run. A predicate rather than a flag so a caller can
// combine sources -- a user's cancel and the app shutting down -- without a shared bit that
// one of them has to remember to reset.
using CancelPredicate = std::function<bool()>;

// Called once per `poll` slice while the child runs, on the calling thread. The place to
// read a log the child is writing and report progress; it must return promptly, since the
// next cancel check waits on it.
using PollCallback = std::function<void()>;

// Spawn, then wait in `poll` slices, asking `should_cancel` between them. When it answers
// true the child is terminated and `cancelled` is set; the exit code is then whatever the
// kill produced and should not be interpreted. An empty predicate waits unconditionally.
RunResult run(const SpawnRequest& request, const CancelPredicate& should_cancel,
              std::chrono::milliseconds poll = std::chrono::milliseconds(100),
              const PollCallback& on_poll = {});

namespace detail {

// Windows has no argv: the child re-parses one command line under the C runtime's rules,
// so this is the inverse of those rules (Microsoft, "Parsing C command-line arguments").
// Pure and platform-independent, so its tests run in every CI leg.
std::string windows_command_line(const std::vector<std::string>& argv);

// Turn a POSIX wait status into the exit code convention above. Exposed so the decoding is
// tested with known statuses, not only through a real child.
int exit_code_from_wait_status(int status);

}  // namespace detail

}  // namespace snapback::subprocess
