// How a second launch asks the running instance to show itself. The listener is started only by
// the process holding SingleInstanceGuard, and requests are sent only by a process that was
// refused it; two decisions below rely on that. Pure parts (channel id, endpoint path) live
// here so every platform tests them.
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

// What happened to an activation request. NoOwner is not a failure: the owner exited between
// our lock attempt and our connect.
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

// Ceiling on the failure path; a live owner answers in microseconds.
inline constexpr std::int64_t kActivationTimeoutMs = 1500;

// One line out, one line back. Versioned because during an upgrade the two ends can be
// different builds; an owner refuses what it cannot parse.
inline constexpr const char* kActivationProtocolTag = "SNAPBACK-ACTIVATE";
inline constexpr int kActivationProtocolVersion = 1;
inline constexpr const char* kActivationAckOk = "OK";
inline constexpr const char* kActivationAckRefused = "REFUSED";

namespace detail {

// FNV-1a 64 as 16 hex digits. Not shared with data_export's archive_checksum on purpose:
// different purposes that should be free to change independently.
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

// Where the Unix socket lives, given sun_path's size (104 bytes on macOS, 108 on Linux).
// Prefers the data directory (0700, removed by --purge); the temp fallback carries the channel
// id so two data directories cannot collide.
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

// Whether a received line is a request this owner should honour: right protocol, version, and
// channel id. Refuses rather than guesses.
inline bool activation_request_matches(std::string_view line, const std::string& channel_id) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.remove_suffix(1);
    std::string expected = activation_request_line(channel_id);
    expected.pop_back();  // the framing newline, already stripped from `line`
    return line == expected;
}

}  // namespace detail

// Stable id for "the instance owning this data directory", derived from the path so both
// processes agree without sharing state. weakly_canonical normalises `..`, trailing separators,
// and (on Windows) case. The owner compares the full id, so a hash collision costs a Refused,
// not a wrong window.
std::string activation_channel_id(const std::filesystem::path& data_dir);

// The platform endpoint the id resolves to: a named pipe on Windows, a socket path elsewhere.
// Exposed mainly so tests and logs can name the thing that failed.
std::string activation_endpoint_for(const std::filesystem::path& data_dir);

// Whether this build can carry activation requests at all. False leaves the second launch on
// its old behaviour rather than pretending it succeeded.
bool activation_channel_supported();

// The owner's half; start it after SingleInstanceGuard reports Acquired. `on_activate` runs on
// the listener thread and must only dispatch to the UI thread (see main.cpp).
class ActivationListener {
public:
    // nullopt when the endpoint could not be created; the caller keeps running.
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

// Send an already-composed request and read the ack. The request is a parameter so tests can
// send one the owner must refuse.
ActivationResult send_activation_request(const std::filesystem::path& data_dir,
                                         const std::string& request, std::int64_t timeout_ms);

}  // namespace detail

// The loser's half; call after SingleInstanceGuard reports AlreadyRunning. Blocking, bounded by
// `timeout_ms`.
ActivationResult request_activation(const std::filesystem::path& data_dir,
                                    std::int64_t timeout_ms = kActivationTimeoutMs);

}  // namespace snapback
