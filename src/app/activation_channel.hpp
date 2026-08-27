// The channel a losing second launch uses to ask the running instance to show itself.
//
// Roadmap 9.15. `single_instance.hpp` next door answers "may this process run?"; this answers
// the question that comes immediately after a *no* — "then who does, and can they come to the
// front?". Without it a second launch of a tray-resident app whose window is hidden prints to
// a stderr stream a GUI process does not own and exits, which is indistinguishable from a
// broken app.
//
// **This channel is owned by the instance lock, not merely adjacent to it.** Every operation
// below assumes the caller's position relative to `SingleInstanceGuard` is already settled:
// the listener is started only by a process that *holds* the lock, and a request is sent only
// by a process that was *refused* it. Two of the decisions here are correct only under that
// assumption, and both are called out where they are made.
//
// The pure parts -- the channel id and the endpoint-path arithmetic -- live in this header so
// every platform's build can test them. Only the socket/pipe plumbing is per-platform.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace snapback {

// What happened to an activation request.
//
// Five outcomes rather than a bool, for the same reason `AlertSuppression` carries a cause:
// the caller has to decide between exiting silently and printing something, and "it did not
// work" is not enough to decide with. `NoOwner` in particular is not a failure -- it is the
// ordinary race where the owner exited between our lock attempt and our connect.
enum class ActivationResult {
    Activated,  // the owner acknowledged; it is raising its window
    NoOwner,    // nothing is listening on the endpoint
    TimedOut,   // something is listening but did not answer inside the budget
    Refused,    // the owner is listening for a *different* data directory
    Error,      // the platform call failed
};

inline const char* activation_result_as_str(ActivationResult r) noexcept {
    switch (r) {
        case ActivationResult::Activated:
            return "activated";
        case ActivationResult::NoOwner:
            return "no owner";
        case ActivationResult::TimedOut:
            return "timed out";
        case ActivationResult::Refused:
            return "refused";
        case ActivationResult::Error:
        default:
            return "error";
    }
}

// The longest an activation request may take before the caller gives up.
//
// 9.15 requires the window to surface "within one second". This is the ceiling on the
// *failure* path, not the expected cost: an owner that is running answers in microseconds,
// since the handler only queues work onto its UI thread. The budget exists so that a wedged
// owner costs a second-and-a-half stare rather than a hang.
inline constexpr std::int64_t kActivationTimeoutMs = 1500;

// The protocol, such as it is. One line out, one line back.
//
// Versioned from the first commit because both ends are the same binary *today* and will not
// be during an upgrade: a newly installed build can be launched while the previous one is
// still the running owner. An owner that cannot parse the request must refuse it rather than
// guess, and a version token is what makes that distinguishable from corruption.
inline constexpr const char* kActivationProtocolTag = "SNAPBACK-ACTIVATE";
inline constexpr int kActivationProtocolVersion = 1;
inline constexpr const char* kActivationAckOk = "OK";
inline constexpr const char* kActivationAckRefused = "REFUSED";

namespace detail {

// FNV-1a 64, rendered as 16 hex digits.
//
// Deliberately not shared with `data_export.cpp:archive_checksum`, which computes the same
// function for a different purpose: that one is an integrity claim printed in a document a
// user reads, this one is a naming scheme for an OS object. Coupling them would mean a future
// change to either -- a stronger digest there, a shorter name here -- silently changing the
// other. The five lines are cheaper than that coupling.
inline std::string fnv1a_hex(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex;
    for (int shift = 60; shift >= 0; shift -= 4) {
        out << "0123456789abcdef"[(hash >> shift) & 0xF];
    }
    return out.str();
}

// Where the Unix-domain socket lives, given how much room `sun_path` has.
//
// Pure and compiled everywhere, including Windows, precisely because this is the branch that
// will never run on the machine that writes it: `sun_path` is 104 bytes on macOS and 108 on
// Linux, and an app-data directory under a long home directory plus a deep container path can
// exceed it. Bind then fails with a truncated path, which looks like a permissions problem and
// is not one.
//
// The preferred home is inside the data directory: it is already `0700`, it is removed by
// `--purge` along with everything else the app created, and it keeps the endpoint beside the
// lock that owns it. The temp fallback gives that up and must therefore carry the channel id
// in its name, since two data directories would otherwise collide in a shared directory.
inline std::string unix_socket_path(const std::filesystem::path& data_dir,
                                    const std::string& channel_id,
                                    const std::filesystem::path& temp_dir,
                                    std::size_t max_path_len) {
    auto preferred = (data_dir / "activate.sock").string();
    if (preferred.size() < max_path_len) return preferred;
    return (temp_dir / ("snapback-activate-" + channel_id + ".sock")).string();
}

// The one line a requesting process sends. Trailing newline included: the reader frames on it
// rather than on a length, so a truncated write is a request that never completes rather than
// one that is misread as a shorter valid one.
inline std::string activation_request_line(const std::string& channel_id) {
    return std::string(kActivationProtocolTag) + " " + std::to_string(kActivationProtocolVersion) +
           " " + channel_id + "\n";
}

// Whether a received line is a request this owner should honour.
//
// Three independent reasons to say no, all collapsed into one `false` because the sender
// cannot act differently on any of them: wrong protocol (something else is talking to our
// endpoint), wrong version (a build we do not understand), wrong channel id (a hash collision,
// or a request meant for a different data directory). The owner refuses rather than guesses --
// raising the window for a different database is worse than not raising it at all.
inline bool activation_request_matches(std::string_view line, const std::string& channel_id) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.remove_suffix(1);
    std::string expected = activation_request_line(channel_id);
    expected.pop_back();  // the framing newline, already stripped from `line`
    return line == expected;
}

}  // namespace detail

// A stable identifier for "the instance owning this data directory".
//
// Derived from the path rather than assigned, so the two processes agree without either
// having written anything down -- the loser cannot read the owner's state, which is the whole
// difficulty. `weakly_canonical` normalises `..`, a trailing separator, and (on Windows) case,
// so `C:\Data\Snapback` and `c:\data\snapback\` are one channel and not two.
//
// A hash collision between two real data directories is not defended against by being
// unlikely: the request carries the full id and the owner compares it, so a collision costs a
// `Refused`, not a window raised for the wrong database.
std::string activation_channel_id(const std::filesystem::path& data_dir);

// The platform endpoint the id resolves to: a named pipe on Windows, a socket path elsewhere.
// Exposed mainly so tests and logs can name the thing that failed.
std::string activation_endpoint_for(const std::filesystem::path& data_dir);

// Whether this build can carry activation requests at all. False leaves the second launch on
// its old behaviour rather than pretending it succeeded.
bool activation_channel_supported();

// The owner's half. Start it *after* `SingleInstanceGuard` reports Acquired.
//
// `on_activate` runs on the listener's own thread, never on the caller's. It must therefore do
// nothing but hand the work to the UI thread -- see `main.cpp`, which wraps it in
// `webview::dispatch` for exactly this reason. Touching a window directly from here would be
// the ordinary cross-thread UI bug, arriving only on the second launch and therefore rarely
// on the developer's machine.
class ActivationListener {
public:
    // nullopt when the endpoint could not be created. The caller keeps running: an app that
    // refused to start because a convenience channel failed would have traded a small annoyance
    // for a total outage.
    static std::optional<ActivationListener> start(const std::filesystem::path& data_dir,
                                                   std::function<void()> on_activate);

    ~ActivationListener();
    ActivationListener(ActivationListener&&) noexcept;
    ActivationListener& operator=(ActivationListener&&) noexcept;
    ActivationListener(const ActivationListener&) = delete;
    ActivationListener& operator=(const ActivationListener&) = delete;

    [[nodiscard]] const std::string& endpoint() const;

    // How many requests this listener has accepted and acknowledged. Present for the tests,
    // which otherwise have to prove "exactly once" by sleeping and hoping.
    [[nodiscard]] std::uint64_t activation_count() const;

private:
    struct Impl;
    explicit ActivationListener(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

namespace detail {

// Send one already-composed request to the endpoint of `data_dir` and read the ack.
//
// The request line is a parameter rather than derived inside, and that is the only reason the
// refusal path is testable at all: `request_activation` computes the endpoint and the id from
// the same path, so it can never disagree with itself, and nothing else in the product can
// produce a request an owner should refuse. A test that cannot construct the wrong request
// cannot prove the owner rejects it.
ActivationResult send_activation_request(const std::filesystem::path& data_dir,
                                         const std::string& request, std::int64_t timeout_ms);

}  // namespace detail

// The loser's half. Call it *after* `SingleInstanceGuard` reports AlreadyRunning.
//
// Blocking, with `timeout_ms` as a hard ceiling. It is called from a process that has opened
// no database, started no capture, and installed no tray, so blocking the only thread it has
// is the simplest correct thing it can do.
ActivationResult request_activation(const std::filesystem::path& data_dir,
                                    std::int64_t timeout_ms = kActivationTimeoutMs);

}  // namespace snapback
