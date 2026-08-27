// _GNU_SOURCE has to precede every system header: `struct ucred` and SO_PEERCRED are behind
// it in glibc, and by the time <sys/socket.h> has been included once, defining it is too late.
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "app/activation_channel.hpp"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace snapback {
namespace {

// A request line, plus the ack, are both a few dozen bytes. The cap exists so a client that
// connects and then streams cannot make the owner allocate: a byte past this is not a slow
// request, it is not our protocol at all.
constexpr std::size_t kMaxRequestBytes = 256;

std::int64_t clamp_timeout(std::int64_t timeout_ms) {
    if (timeout_ms < 0) return 0;
    return timeout_ms > kActivationTimeoutMs ? kActivationTimeoutMs : timeout_ms;
}

// The path the channel id is computed from.
//
// `weakly_canonical` rather than `canonical` because the data directory may legitimately not
// exist yet on the losing side of a first-run race, and `canonical` throws on a missing path --
// which would turn "the app is already running" into a crash.
std::string canonical_key(const std::filesystem::path& data_dir) {
    // Lexically first, then physically. `weakly_canonical` resolves only the part of the path
    // that exists, so a `..` hop through a directory that was never created is left for the
    // platform to interpret -- and macOS and Windows do not interpret it the same way, which
    // made two spellings of one directory two channels on exactly one OS. Collapsing `..` and
    // `.` before the call removes the disagreement instead of documenting it.
    std::error_code ignored;
    const auto normalized = data_dir.lexically_normal();
    auto resolved = std::filesystem::weakly_canonical(normalized, ignored);
    auto key = (ignored ? normalized : resolved).lexically_normal().string();
#if defined(_WIN32)
    // Windows paths are case-insensitive, so `C:\Data` and `c:\data` are one directory and
    // must be one channel. Folding here rather than at compare time keeps the id itself the
    // single point where this is decided.
    for (auto& c : key) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '/') c = '\\';
    }
#endif
    return key;
}

}  // namespace

std::string activation_channel_id(const std::filesystem::path& data_dir) {
    return detail::fnv1a_hex(canonical_key(data_dir));
}

// ---------------------------------------------------------------------------------------
#if defined(_WIN32)
// ---------------------------------------------------------------------------------------

namespace {

std::wstring pipe_name_wide(const std::filesystem::path& data_dir) {
    const auto narrow = activation_endpoint_for(data_dir);
    return std::wstring(narrow.begin(), narrow.end());  // ASCII by construction
}

// A security descriptor granting the calling user, and nobody else, access to the pipe.
//
// Named pipes are otherwise readable by every account on the machine, and their default DACL
// also admits Administrators. This endpoint's only power is "raise a window", which is not
// dangerous -- but it is reachable by name from any session on the box, and a per-user app
// with a machine-wide control surface is the kind of detail 8.13 exists to stop shipping.
//
// SDDL rather than SetEntriesInAcl: one string and one call, against roughly forty lines of
// ACL assembly with three allocation failure paths that would never be exercised.
bool current_user_descriptor(PSECURITY_DESCRIPTOR* out) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<unsigned char> buffer(needed ? needed : 1);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);

    LPWSTR sid_text = nullptr;
    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    if (!ConvertSidToStringSidW(user->User.Sid, &sid_text)) return false;

    // D: a DACL with one ACE — (Allow; ; Generic All; ; ; <this user>).
    const std::wstring sddl = L"D:(A;;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, out,
                                                                nullptr) != 0;
}

// One read or write on an overlapped handle, with a deadline.
//
// The handle is FILE_FLAG_OVERLAPPED, so a plain ReadFile/WriteFile against it is not "the
// blocking version" -- it is undefined, because the OVERLAPPED the API needs is missing. The
// deadline is why it is worth the extra shape at all: without one, a client that connects and
// then says nothing pins the listener thread forever, and the app looks fine right up until
// the second launch that needed it.
bool overlapped_io(HANDLE handle, HANDLE event, bool writing, void* buffer, DWORD size,
                   DWORD* transferred, DWORD timeout_ms) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    ResetEvent(event);
    const BOOL started = writing ? WriteFile(handle, buffer, size, nullptr, &overlapped)
                                 : ReadFile(handle, buffer, size, nullptr, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0) {
        CancelIo(handle);
        // Let the cancellation settle before `buffer` goes out of scope. A still-pending write
        // into a dead stack frame is the classic overlapped-I/O bug, and it corrupts something
        // else entirely by the time anyone notices.
        WaitForSingleObject(event, INFINITE);
        return false;
    }
    return GetOverlappedResult(handle, &overlapped, transferred, FALSE) != 0;
}

}  // namespace

std::string activation_endpoint_for(const std::filesystem::path& data_dir) {
    return "\\\\.\\pipe\\snapback-activate-" + activation_channel_id(data_dir);
}

bool activation_channel_supported() {
    return true;
}

struct ActivationListener::Impl {
    std::string endpoint;
    std::string channel_id;
    std::function<void()> on_activate;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE connect_event = nullptr;
    // A second event, rather than reusing connect_event: the connect is still notionally in
    // flight while the request is read, and one event serving two overlapped operations is a
    // race that only shows up under load.
    HANDLE io_event = nullptr;
    HANDLE stop_event = nullptr;
    std::thread worker;
    std::atomic<std::uint64_t> activations{0};

    ~Impl() {
        if (stop_event) SetEvent(stop_event);
        if (worker.joinable()) worker.join();
        if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
        if (connect_event) CloseHandle(connect_event);
        if (io_event) CloseHandle(io_event);
        if (stop_event) CloseHandle(stop_event);
    }

    // One accepted connection, start to finish. Every exit path disconnects, so the next
    // ConnectNamedPipe on the same handle starts clean.
    void serve_one() {
        char buffer[kMaxRequestBytes + 1] = {};
        DWORD read = 0;
        const bool got = overlapped_io(pipe, io_event, /*writing=*/false, buffer,
                                       static_cast<DWORD>(kMaxRequestBytes), &read,
                                       static_cast<DWORD>(kActivationTimeoutMs));
        const bool honour =
            got && detail::activation_request_matches(std::string_view(buffer, read), channel_id);

        std::string ack = std::string(honour ? kActivationAckOk : kActivationAckRefused) + "\n";
        DWORD written = 0;
        overlapped_io(pipe, io_event, /*writing=*/true, ack.data(), static_cast<DWORD>(ack.size()),
                      &written, static_cast<DWORD>(kActivationTimeoutMs));
        // Acknowledge before acting. The requester is a process whose only remaining job is to
        // exit; making it wait on our UI thread would couple its exit code to our redraw.
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);

        if (honour) {
            activations.fetch_add(1, std::memory_order_relaxed);
            if (on_activate) on_activate();
        }
    }

    void run() {
        while (true) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = connect_event;
            ResetEvent(connect_event);

            bool connected = ConnectNamedPipe(pipe, &overlapped) != 0;
            if (!connected) {
                const DWORD error = GetLastError();
                // A client that connected between CreateNamedPipe and here is already on the
                // wire; ERROR_PIPE_CONNECTED is success spelled as a failure.
                if (error == ERROR_PIPE_CONNECTED) {
                    connected = true;
                } else if (error == ERROR_IO_PENDING) {
                    HANDLE waits[] = {connect_event, stop_event};
                    const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                    if (which != WAIT_OBJECT_0) {
                        CancelIo(pipe);
                        return;  // stop_event, or a wait that failed: either way we are done
                    }
                    connected = true;
                } else {
                    return;
                }
            }
            if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) {
                DisconnectNamedPipe(pipe);
                return;
            }
            if (connected) serve_one();
        }
    }
};

std::optional<ActivationListener> ActivationListener::start(const std::filesystem::path& data_dir,
                                                            std::function<void()> on_activate) {
    auto impl = std::make_unique<Impl>();
    impl->endpoint = activation_endpoint_for(data_dir);
    impl->channel_id = activation_channel_id(data_dir);
    impl->on_activate = std::move(on_activate);

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{};
    SECURITY_ATTRIBUTES* attributes_ptr = nullptr;
    if (current_user_descriptor(&descriptor)) {
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = descriptor;
        attributes.bInheritHandle = FALSE;
        attributes_ptr = &attributes;
    }

    // PIPE_REJECT_REMOTE_CLIENTS: `\\host\pipe\...` is a real address. A single-user desktop
    // convenience has no business being reachable from another machine.
    const auto name = pipe_name_wide(data_dir);
    impl->pipe = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        /*nMaxInstances=*/1, static_cast<DWORD>(kMaxRequestBytes),
        static_cast<DWORD>(kMaxRequestBytes), /*nDefaultTimeOut=*/0, attributes_ptr);
    if (descriptor) LocalFree(descriptor);
    if (impl->pipe == INVALID_HANDLE_VALUE) return std::nullopt;

    impl->connect_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl->io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!impl->connect_event || !impl->io_event || !impl->stop_event) return std::nullopt;

    Impl* raw = impl.get();
    impl->worker = std::thread([raw] { raw->run(); });
    return ActivationListener(std::move(impl));
}

ActivationResult detail::send_activation_request(const std::filesystem::path& data_dir,
                                                 const std::string& request,
                                                 std::int64_t timeout_ms) {
    const auto budget = clamp_timeout(timeout_ms);
    const auto name = pipe_name_wide(data_dir);
    const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(budget);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (true) {
        pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        // Nothing is listening. Not a failure: the owner may have exited in the microseconds
        // between our lock attempt and this call.
        if (error == ERROR_FILE_NOT_FOUND) return ActivationResult::NoOwner;
        if (error != ERROR_PIPE_BUSY) return ActivationResult::Error;
        const auto now = GetTickCount64();
        if (now >= deadline) return ActivationResult::TimedOut;
        if (!WaitNamedPipeW(name.c_str(), static_cast<DWORD>(deadline - now))) {
            return GetLastError() == ERROR_FILE_NOT_FOUND ? ActivationResult::NoOwner
                                                          : ActivationResult::TimedOut;
        }
    }

    HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!io_event) {
        CloseHandle(pipe);
        return ActivationResult::Error;
    }
    // What is left of the budget, recomputed per call: the connect above may already have spent
    // most of it waiting out a busy pipe.
    const auto remaining = [deadline] {
        const auto now = GetTickCount64();
        return now >= deadline ? DWORD{0} : static_cast<DWORD>(deadline - now);
    };

    std::string outgoing = request;
    DWORD written = 0;
    const bool sent = overlapped_io(pipe, io_event, /*writing=*/true, outgoing.data(),
                                    static_cast<DWORD>(outgoing.size()), &written, remaining());

    char buffer[kMaxRequestBytes + 1] = {};
    DWORD read = 0;
    const bool got =
        sent && overlapped_io(pipe, io_event, /*writing=*/false, buffer,
                              static_cast<DWORD>(kMaxRequestBytes), &read, remaining());
    CloseHandle(io_event);
    CloseHandle(pipe);
    if (!sent) return ActivationResult::Error;
    if (!got || read == 0) return ActivationResult::TimedOut;
    const std::string_view ack(buffer, read);
    if (ack.rfind(kActivationAckOk, 0) == 0) return ActivationResult::Activated;
    if (ack.rfind(kActivationAckRefused, 0) == 0) return ActivationResult::Refused;
    return ActivationResult::Error;
}

// ---------------------------------------------------------------------------------------
#else  // POSIX: macOS and Linux both use an AF_UNIX socket.
// ---------------------------------------------------------------------------------------

namespace {

bool fill_address(sockaddr_un& address, const std::string& path) {
    if (path.size() >= sizeof(address.sun_path)) return false;
    address = {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

// Whether the process on the other end of `fd` belongs to the user running this one.
//
// The socket is already 0600 inside a 0700 directory, so this is the second lock on the same
// door -- deliberately, because the temp-directory fallback above puts the socket somewhere
// this process does not own the permissions of. A check that is redundant on the common path
// and load-bearing on the rare one is worth its ten lines.
bool peer_is_current_user(int fd) {
#if defined(__APPLE__)
    uid_t peer_uid = 0;
    gid_t peer_gid = 0;
    if (getpeereid(fd, &peer_uid, &peer_gid) != 0) return false;
    return peer_uid == geteuid();
#elif defined(SO_PEERCRED)
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) return false;
    return credentials.uid == geteuid();
#else
    (void)fd;
    return true;  // no peer-credential API; the 0600 mode is the whole guard
#endif
}

void set_socket_timeout(int fd, std::int64_t timeout_ms) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

}  // namespace

std::string activation_endpoint_for(const std::filesystem::path& data_dir) {
    sockaddr_un probe{};
    std::error_code ignored;
    return detail::unix_socket_path(data_dir, activation_channel_id(data_dir),
                                    std::filesystem::temp_directory_path(ignored),
                                    sizeof(probe.sun_path));
}

bool activation_channel_supported() {
    return true;
}

struct ActivationListener::Impl {
    std::string endpoint;
    std::string channel_id;
    std::function<void()> on_activate;
    int listen_fd = -1;
    int stop_read = -1;
    int stop_write = -1;
    std::thread worker;
    std::atomic<std::uint64_t> activations{0};

    ~Impl() {
        if (stop_write >= 0) {
            const char byte = 'x';
            ssize_t ignored = ::write(stop_write, &byte, 1);
            (void)ignored;
        }
        if (worker.joinable()) worker.join();
        if (listen_fd >= 0) ::close(listen_fd);
        if (stop_read >= 0) ::close(stop_read);
        if (stop_write >= 0) ::close(stop_write);
        // The socket file outlives its socket, so remove it rather than leaving a node that
        // the next start has to recognise as stale.
        std::error_code ignored;
        std::filesystem::remove(endpoint, ignored);
    }

    void serve_one(int fd) {
        char buffer[kMaxRequestBytes + 1] = {};
        const ssize_t read_bytes = ::read(fd, buffer, kMaxRequestBytes);
        const bool honour =
            read_bytes > 0 && peer_is_current_user(fd) &&
            detail::activation_request_matches(
                std::string_view(buffer, static_cast<std::size_t>(read_bytes)), channel_id);

        const std::string ack =
            std::string(honour ? kActivationAckOk : kActivationAckRefused) + "\n";
        ssize_t written = ::write(fd, ack.data(), ack.size());
        (void)written;
        ::close(fd);

        if (honour) {
            activations.fetch_add(1, std::memory_order_relaxed);
            if (on_activate) on_activate();
        }
    }

    void run() {
        while (true) {
            pollfd waits[2]{};
            waits[0].fd = listen_fd;
            waits[0].events = POLLIN;
            waits[1].fd = stop_read;
            waits[1].events = POLLIN;
            const int ready = ::poll(waits, 2, -1);
            if (ready < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (waits[1].revents & POLLIN) return;  // the destructor asked us to stop
            if (!(waits[0].revents & POLLIN)) continue;

            const int fd = ::accept(listen_fd, nullptr, nullptr);
            if (fd < 0) continue;
            set_socket_timeout(fd, kActivationTimeoutMs);
            serve_one(fd);
        }
    }
};

std::optional<ActivationListener> ActivationListener::start(const std::filesystem::path& data_dir,
                                                            std::function<void()> on_activate) {
    auto impl = std::make_unique<Impl>();
    impl->endpoint = activation_endpoint_for(data_dir);
    impl->channel_id = activation_channel_id(data_dir);
    impl->on_activate = std::move(on_activate);

    sockaddr_un address{};
    if (!fill_address(address, impl->endpoint)) return std::nullopt;

    impl->listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (impl->listen_fd < 0) return std::nullopt;

    // Stale-endpoint recovery, and the one decision here that is only safe because of where it
    // is called from. A crash leaves the socket file behind and bind() then fails with
    // EADDRINUSE; unlinking it is correct **because this process already holds the exclusive
    // instance lock**, which is the proof that no live owner is bound to it. Unlinking on the
    // weaker evidence of "it looked stale" is how a second database owner gets in -- exactly
    // what 9.8's lock exists to prevent.
    std::error_code ignored;
    std::filesystem::remove(impl->endpoint, ignored);

    if (::bind(impl->listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        return std::nullopt;
    }
    // Owner-only. The data directory is already 0700, but the temp-directory fallback is not.
    ::chmod(impl->endpoint.c_str(), S_IRUSR | S_IWUSR);
    if (::listen(impl->listen_fd, 4) != 0) return std::nullopt;

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) return std::nullopt;
    impl->stop_read = pipe_fds[0];
    impl->stop_write = pipe_fds[1];

    Impl* raw = impl.get();
    impl->worker = std::thread([raw] { raw->run(); });
    return ActivationListener(std::move(impl));
}

ActivationResult detail::send_activation_request(const std::filesystem::path& data_dir,
                                                 const std::string& request,
                                                 std::int64_t timeout_ms) {
    const auto budget = clamp_timeout(timeout_ms);
    const auto endpoint = activation_endpoint_for(data_dir);

    sockaddr_un address{};
    if (!fill_address(address, endpoint)) return ActivationResult::Error;

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return ActivationResult::Error;
    set_socket_timeout(fd, budget);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        const int error = errno;
        ::close(fd);
        // ENOENT: never created. ECONNREFUSED: the file survived an owner that did not.
        // Both mean the same thing to the caller, and neither is an error.
        if (error == ENOENT || error == ECONNREFUSED) return ActivationResult::NoOwner;
        return ActivationResult::Error;
    }

    if (::write(fd, request.data(), request.size()) < 0) {
        ::close(fd);
        return ActivationResult::Error;
    }

    char buffer[kMaxRequestBytes + 1] = {};
    const ssize_t read_bytes = ::read(fd, buffer, kMaxRequestBytes);
    ::close(fd);
    if (read_bytes <= 0) return ActivationResult::TimedOut;
    const std::string_view ack(buffer, static_cast<std::size_t>(read_bytes));
    if (ack.rfind(kActivationAckOk, 0) == 0) return ActivationResult::Activated;
    if (ack.rfind(kActivationAckRefused, 0) == 0) return ActivationResult::Refused;
    return ActivationResult::Error;
}

#endif

ActivationResult request_activation(const std::filesystem::path& data_dir,
                                    std::int64_t timeout_ms) {
    return detail::send_activation_request(
        data_dir, detail::activation_request_line(activation_channel_id(data_dir)), timeout_ms);
}

// --- the platform-neutral shell of the listener handle ---

ActivationListener::ActivationListener(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ActivationListener::~ActivationListener() = default;
ActivationListener::ActivationListener(ActivationListener&&) noexcept = default;
ActivationListener& ActivationListener::operator=(ActivationListener&&) noexcept = default;

const std::string& ActivationListener::endpoint() const {
    return impl_->endpoint;
}

std::uint64_t ActivationListener::activation_count() const {
    return impl_->activations.load(std::memory_order_relaxed);
}

}  // namespace snapback
