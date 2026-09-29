// macOS start-on-login via a launchd LaunchAgent: installing the plist is enabling it, taking
// effect at next login. Compiled and tested on every OS; every function takes its directory as
// an argument so tests never touch a real login item.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace snapback {
namespace launchd {

// Also the plist's filename; launchd expects the two to agree.
inline constexpr std::string_view kAgentLabel = "com.snapback.app";

// APFS names may contain `&`, `<`, `>`, and launchd silently ignores a malformed plist.
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
