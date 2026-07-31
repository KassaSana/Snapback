// Linux start-on-login, via a systemd **user** unit. Roadmap 3.0, second half.
//
// Two things make this different from the launchd agent, and both are easy to get wrong:
//
//  1. **Writing the unit file does not enable it.** launchd starts anything it finds in
//     ~/Library/LaunchAgents; systemd only starts units that something *wants*. `systemctl
//     --user enable` reads the unit's `[Install] WantedBy=` and creates a symlink in
//     `<target>.wants/`. That symlink is the enablement, so this module creates it directly
//     rather than shelling out to systemctl — the same end state, no subprocess.
//  2. **Unit files have their own escaping.** `%` introduces a systemd specifier (`%h` is the
//     home directory), so an unescaped `%` in a path silently expands into something else.
//     A directory like `/home/kassa/100%backup` is unusual but entirely legal.
//
// Known limitation, stated rather than hidden: this hangs Snapback off
// `graphical-session.target`, which only exists on desktops that integrate with systemd.
// GNOME does; some others do not, and there the unit is written but never triggered. The
// alternative — an XDG `~/.config/autostart/*.desktop` file — is desktop-environment driven
// and more universal. The roadmap specified systemd, so that is what this implements; the
// XDG fallback is the natural follow-up if a real Linux user reports it.
//
// As with launchd, everything here compiles and is tested on every OS, and every function
// takes its directory as an argument so no test can touch a real login item (Roadmap 11.7).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace snapback {
namespace systemd {

inline constexpr std::string_view kUnitName = "snapback.service";

// The target Snapback attaches to. Also names the `.wants` directory holding the enable link.
inline constexpr std::string_view kWantedBy = "graphical-session.target";

// Renders one path as a systemd `ExecStart` word: always quoted, with `\`, `"` and `%`
// escaped. Quoting unconditionally is simpler than deciding per path, and it is what keeps a
// home directory containing a space from being read as two arguments.
std::string escape_exec_path(std::string_view path);

// The unit file's contents. Pure, so a test pins the exact bytes.
std::string unit_file(std::string_view executable_path);

// `<dir>/snapback.service`.
std::filesystem::path unit_path(const std::filesystem::path& dir);

// `<dir>/graphical-session.target.wants/snapback.service` — the symlink `systemctl --user
// enable` would create, and the thing that actually makes the unit run.
std::filesystem::path enable_link_path(const std::filesystem::path& dir);

// True only when both the unit file and the enable link exist. A unit file on its own is
// installed-but-off, which must not report as enabled: the user would see a checked box and
// nothing would start.
bool unit_enabled(const std::filesystem::path& dir);

// Writes the unit and creates the enable link. False on an empty path or a failed write.
bool install_unit(const std::filesystem::path& dir, std::string_view executable_path);

// Removes the enable link and the unit file. Removing what is already absent is success.
bool remove_unit(const std::filesystem::path& dir);

// `$XDG_CONFIG_HOME/systemd/user`, falling back to `$HOME/.config/systemd/user`. Empty when
// neither variable is set.
std::filesystem::path user_unit_dir();

// Absolute path of the running executable via /proc/self/exe, or empty off Linux.
std::string current_executable_path();

}  // namespace systemd
}  // namespace snapback
