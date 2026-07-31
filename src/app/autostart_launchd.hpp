// macOS start-on-login, via a launchd LaunchAgent. Roadmap 3.0.
//
// launchd reads `~/Library/LaunchAgents/*.plist` when the user logs in, so installing the
// agent *is* enabling autostart — there is no daemon to notify and no `launchctl` subprocess
// to spawn. The trade is that a toggle takes effect at the next login rather than immediately,
// which is the semantics the setting promises anyway ("start Snapback when I log in").
//
// Everything here compiles on every OS even though only macOS calls it. That is deliberate:
// the plist text and the install/remove logic are then covered by the Linux and Windows CI
// jobs too, and the only macOS-specific part left is *which directory* to point at. It also
// avoids repeating Roadmap 11.7 — the Windows autostart test writes the real machine's
// registry, so it cannot run hermetically. Every function below takes its directory as an
// argument, so the tests use a temp directory and never touch a real login item.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace snapback {
namespace launchd {

// Reverse-DNS, matching what the bundle id will be when Roadmap 3.3 lands. Also the plist's
// filename, because launchd expects the two to agree.
inline constexpr std::string_view kAgentLabel = "com.snapback.app";

// XML has five reserved characters and a macOS path may legitimately contain three of them
// (`&`, `<`, `>` are all valid in HFS+/APFS names). An unescaped `&` makes the plist
// unparseable, and launchd's failure mode for a malformed agent is to ignore it silently —
// autostart would simply never happen, with nothing to see in the UI.
std::string escape_xml(std::string_view value);

// The plist launchd will read. Pure, so the exact bytes are pinned by a test rather than
// discovered by logging out and back in.
std::string agent_plist(std::string_view executable_path);

// `<dir>/com.snapback.app.plist`.
std::filesystem::path agent_path(const std::filesystem::path& dir);

// True when the agent file exists. This is the whole definition of "enabled": launchd owns
// the state, and asking the filesystem is asking launchd's source of truth.
bool agent_installed(const std::filesystem::path& dir);

// Writes the agent, creating `dir` if needed. False on an empty executable path (an agent
// pointing nowhere is worse than none: launchd retries it every login) or a failed write.
bool install_agent(const std::filesystem::path& dir, std::string_view executable_path);

// Removes the agent. Removing one that is already absent is success, not failure — the
// caller asked for "not enabled" and that is the state they get.
bool remove_agent(const std::filesystem::path& dir);

// `$HOME/Library/LaunchAgents`, or empty when HOME is unset (a daemon context, where a
// per-user login item is meaningless anyway).
std::filesystem::path user_agent_dir();

// Absolute path of the running executable, resolved through symlinks, or empty if it cannot
// be determined. Empty on non-Apple builds.
std::string current_executable_path();

}  // namespace launchd
}  // namespace snapback
