#include "util/subprocess.hpp"

#include <algorithm>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace snapback::subprocess {

namespace detail {

std::string windows_command_line(const std::vector<std::string>& argv) {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const auto& arg = argv[i];
        if (i > 0) out += ' ';
        if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
            out += arg;
            continue;
        }
        // The runtime's rules: a backslash is literal unless it precedes a quote, in which
        // case each pair of backslashes is one literal backslash and the odd one escapes the
        // quote. So a run of n backslashes before a quote we emit becomes 2n+1, and a run
        // before the closing quote we add becomes 2n. Everywhere else it is copied as is.
        out += '"';
        std::size_t backslashes = 0;
        for (char c : arg) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            if (c == '"') {
                out.append(backslashes * 2 + 1, '\\');
                out += '"';
                backslashes = 0;
                continue;
            }
            out.append(backslashes, '\\');
            backslashes = 0;
            out += c;
        }
        out.append(backslashes * 2, '\\');
        out += '"';
    }
    return out;
}

int exit_code_from_wait_status(int status) {
#if defined(_WIN32)
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return kExitNotStarted;
#endif
}

}  // namespace detail

namespace {

// The code a killed child reports, on both platforms: 128 + SIGKILL, so a cancelled run
// reads the same in a Windows log as in a Linux one.
constexpr int kKilledExitCode = 128 + 9;

}  // namespace

#if defined(_WIN32)

namespace {

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string last_error_text(const char* what) {
    return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

struct HandleCloser {
    HANDLE handle = nullptr;
    ~HandleCloser() {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};

}  // namespace

struct ChildProcess::Impl {
    HANDLE process = nullptr;
    HANDLE job = nullptr;  // null when the child could not be placed in one
    std::optional<int> exit_code;

    ~Impl() {
        if (process) CloseHandle(process);
        if (job) CloseHandle(job);
    }
};

std::optional<ChildProcess> ChildProcess::spawn(const SpawnRequest& request,
                                                std::string* error) {
    const auto fail = [error](std::string text) -> std::optional<ChildProcess> {
        if (error) *error = std::move(text);
        return std::nullopt;
    };
    if (request.argv.empty()) return fail("no program to run");

    // Only these two handles cross into the child. bInheritHandles alone would hand over
    // every inheritable handle in the process; the explicit list keeps a training run of
    // several minutes from holding open whatever file the app happened to have inheritable.
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HandleCloser stdin_null{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &inheritable, OPEN_EXISTING, 0, nullptr)};
    if (stdin_null.handle == INVALID_HANDLE_VALUE) return fail(last_error_text("open NUL"));

    HandleCloser output;
    if (request.output_path) {
        // FILE_SHARE_READ so the log tail can be read while the child is still writing.
        output.handle = CreateFileW(request.output_path->wstring().c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ, &inheritable, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    } else {
        output.handle = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &inheritable, OPEN_EXISTING, 0, nullptr);
    }
    if (output.handle == INVALID_HANDLE_VALUE) return fail(last_error_text("open output"));

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<unsigned char> attr_storage(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
        return fail(last_error_text("InitializeProcThreadAttributeList"));
    }
    struct AttrCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttrCleanup() { DeleteProcThreadAttributeList(list); }
    } attr_cleanup{attrs};
    HANDLE inherited[] = {stdin_null.handle, output.handle};
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr)) {
        return fail(last_error_text("UpdateProcThreadAttribute"));
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = stdin_null.handle;
    si.StartupInfo.hStdOutput = output.handle;
    si.StartupInfo.hStdError = output.handle;
    si.lpAttributeList = attrs;

    // The child starts suspended so it can be placed in the job before it runs a single
    // instruction -- otherwise `py -3` could launch python.exe outside the job in the gap.
    std::wstring command_line = to_wide(detail::windows_command_line(request.argv));
    const std::wstring cwd = request.cwd ? request.cwd->wstring() : std::wstring{};
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                        EXTENDED_STARTUPINFO_PRESENT;
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, flags, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi)) {
        return fail(last_error_text("CreateProcess"));
    }
    HandleCloser thread{pi.hThread};

    auto impl = std::make_unique<Impl>();
    impl->process = pi.hProcess;

    // KILL_ON_JOB_CLOSE also covers the parent dying: the last handle to the job closes with
    // the process, and the child tree goes with it.
    if (HANDLE job = CreateJobObjectW(nullptr, nullptr)) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                    sizeof(limits)) &&
            AssignProcessToJobObject(job, pi.hProcess)) {
            impl->job = job;
        } else {
            CloseHandle(job);
        }
    }
    ResumeThread(pi.hThread);
    return ChildProcess(std::move(impl));
}

std::optional<int> ChildProcess::wait_for(std::chrono::milliseconds timeout) {
    if (impl_->exit_code) return impl_->exit_code;
    const auto clamped = std::clamp<long long>(timeout.count(), 0, static_cast<long long>(INFINITE) - 1);
    const DWORD waited = WaitForSingleObject(impl_->process, static_cast<DWORD>(clamped));
    if (waited == WAIT_TIMEOUT) return std::nullopt;
    DWORD code = 0;
    if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(impl_->process, &code)) {
        impl_->exit_code = kExitNotStarted;
    } else {
        impl_->exit_code = static_cast<int>(code);
    }
    return impl_->exit_code;
}

int ChildProcess::wait() {
    for (;;) {
        if (const auto code = wait_for(std::chrono::hours(24))) return *code;
    }
}

void ChildProcess::terminate(std::chrono::milliseconds) {
    if (impl_->exit_code) return;
    if (impl_->job) {
        TerminateJobObject(impl_->job, static_cast<UINT>(kKilledExitCode));
    } else {
        TerminateProcess(impl_->process, static_cast<UINT>(kKilledExitCode));
    }
    wait();
}

#else  // POSIX

namespace {

std::string errno_text(const char* what, int err) {
    return std::string(what) + " failed: " + std::strerror(err);
}

}  // namespace

struct ChildProcess::Impl {
    pid_t pid = -1;
    std::optional<int> exit_code;
};

std::optional<ChildProcess> ChildProcess::spawn(const SpawnRequest& request,
                                                std::string* error) {
    const auto fail = [error](std::string text) -> std::optional<ChildProcess> {
        if (error) *error = std::move(text);
        return std::nullopt;
    };
    if (request.argv.empty()) return fail("no program to run");

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    struct ActionsCleanup {
        posix_spawn_file_actions_t* actions;
        ~ActionsCleanup() { posix_spawn_file_actions_destroy(actions); }
    } actions_cleanup{&actions};

    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    const std::string output = request.output_path ? request.output_path->string() : "/dev/null";
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    if (request.cwd) {
        // The _np chdir action is what lets this stay posix_spawn rather than fork+chdir+exec;
        // glibc 2.29 and macOS 10.15 both have it, and every CI runner is far newer.
        const std::string cwd = request.cwd->string();
        if (const int err = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str())) {
            return fail(errno_text("addchdir", err));
        }
    }

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    struct AttrCleanup {
        posix_spawnattr_t* attr;
        ~AttrCleanup() { posix_spawnattr_destroy(attr); }
    } attr_cleanup{&attr};
    // Own process group (pgid 0 means "the child's pid"), so terminate() can signal the
    // whole tree with kill(-pid) rather than only the launcher.
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    std::vector<char*> argv;
    argv.reserve(request.argv.size() + 1);
    // execv's signature predates const-correctness; it does not modify these.
    for (const auto& arg : request.argv) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    if (const int err = posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ)) {
        return fail(errno_text(request.argv[0].c_str(), err));
    }
    auto impl = std::make_unique<Impl>();
    impl->pid = pid;
    return ChildProcess(std::move(impl));
}

std::optional<int> ChildProcess::wait_for(std::chrono::milliseconds timeout) {
    if (impl_->exit_code) return impl_->exit_code;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        int status = 0;
        const pid_t reaped = waitpid(impl_->pid, &status, WNOHANG);
        if (reaped == impl_->pid) {
            impl_->exit_code = detail::exit_code_from_wait_status(status);
            return impl_->exit_code;
        }
        if (reaped < 0) {
            if (errno == EINTR) continue;
            impl_->exit_code = kExitNotStarted;  // ECHILD: nothing left to reap
            return impl_->exit_code;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::nullopt;
        // waitpid has no timeout of its own; short sleeps bound how late a cancel is noticed.
        std::this_thread::sleep_for(
            std::min(std::chrono::milliseconds(20),
                     std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)));
    }
}

int ChildProcess::wait() {
    for (;;) {
        if (const auto code = wait_for(std::chrono::hours(24))) return *code;
    }
}

void ChildProcess::terminate(std::chrono::milliseconds grace) {
    if (impl_->exit_code) return;
    kill(-impl_->pid, SIGTERM);
    if (wait_for(grace)) return;
    kill(-impl_->pid, SIGKILL);
    wait();
}

#endif

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ChildProcess::~ChildProcess() {
    if (impl_ && !impl_->exit_code) terminate();
}

ChildProcess::ChildProcess(ChildProcess&&) noexcept = default;

bool ChildProcess::running() { return !wait_for(std::chrono::milliseconds(0)); }

RunResult run(const SpawnRequest& request, const CancelPredicate& should_cancel,
              std::chrono::milliseconds poll, const PollCallback& on_poll) {
    RunResult result;
    auto child = ChildProcess::spawn(request, &result.error);
    if (!child) return result;
    result.started = true;
    for (;;) {
        if (should_cancel && should_cancel()) {
            child->terminate();
            result.cancelled = true;
            result.exit_code = child->wait();
            return result;
        }
        if (const auto code = child->wait_for(poll)) {
            result.exit_code = *code;
            return result;
        }
        if (on_poll) on_poll();
    }
}

}  // namespace snapback::subprocess
