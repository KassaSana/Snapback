// Linux start-on-login via a systemd user unit.
//
//  1. Writing the unit does not enable it: the `<target>.wants/` symlink does, and it is created
//     directly rather than via systemctl.
//  2. `%` is a systemd specifier, so paths are escaped.
//
// Limitation: hangs off graphical-session.target, which not every desktop provides; an XDG
// autostart file would be the fallback. Every function takes its directory as an argument so
// tests never touch a real login item.
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
