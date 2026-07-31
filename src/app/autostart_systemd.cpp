#include "app/autostart_systemd.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace snapback {
namespace systemd {

std::string escape_exec_path(std::string_view path) {
    std::string out;
    out.reserve(path.size() + 2);
    out += '"';
    for (const char c : path) {
        switch (c) {
            // Inside quotes systemd honours C-style escapes, so a literal backslash or quote
            // has to be escaped or it consumes the character after it.
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            // `%` starts a systemd specifier: `%h` becomes the home directory, `%i` the
            // instance name, and an unknown one is an error that stops the unit loading. `%%`
            // is the documented way to mean a literal percent sign.
            case '%': out += "%%"; break;
            default: out += c; break;
        }
    }
    out += '"';
    return out;
}

std::string unit_file(std::string_view executable_path) {
    std::string unit;
    unit += "[Unit]\n";
    unit += "Description=Snapback focus telemetry\n";
    // After= orders it behind the graphical session; PartOf= makes it stop when that session
    // ends, so logging out does not leave Snapback capturing input for a session that is gone.
    unit += "After=" + std::string(kWantedBy) + "\n";
    unit += "PartOf=" + std::string(kWantedBy) + "\n";
    unit += "\n";
    unit += "[Service]\n";
    unit += "Type=simple\n";
    unit += "ExecStart=" + escape_exec_path(executable_path) + "\n";
    // Restart=no for the same reason the launchd agent omits KeepAlive: a login item that
    // relaunches itself when the user quits is an app the user cannot close.
    unit += "Restart=no\n";
    unit += "\n";
    unit += "[Install]\n";
    unit += "WantedBy=" + std::string(kWantedBy) + "\n";
    return unit;
}

std::filesystem::path unit_path(const std::filesystem::path& dir) {
    return dir / std::string(kUnitName);
}

std::filesystem::path enable_link_path(const std::filesystem::path& dir) {
    return dir / (std::string(kWantedBy) + ".wants") / std::string(kUnitName);
}

bool unit_enabled(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code unit_ec;
    std::error_code link_ec;
    // symlink_status, not status: a link pointing at a unit file that was deleted separately
    // still exists as a link, and reporting it as absent would leave a dangling enable link
    // behind on the next disable.
    const bool has_unit = std::filesystem::is_regular_file(unit_path(dir), unit_ec) && !unit_ec;
    const auto link = std::filesystem::symlink_status(enable_link_path(dir), link_ec);
    const bool has_link = !link_ec && std::filesystem::exists(link);
    return has_unit && has_link;
}

bool install_unit(const std::filesystem::path& dir, std::string_view executable_path) {
    if (dir.empty() || executable_path.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    {
        std::ofstream out(unit_path(dir), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << unit_file(executable_path);
        out.close();
        if (!out) return false;
    }

    const auto link = enable_link_path(dir);
    std::filesystem::create_directories(link.parent_path(), ec);
    if (ec) return false;

    // Replace any existing link first: create_symlink fails if the target exists, and an
    // upgrade that changed the install path must not keep the old link.
    std::filesystem::remove(link, ec);
    ec.clear();
    std::filesystem::create_symlink(unit_path(dir), link, ec);
    if (!ec) return true;

    // Some filesystems (and Windows without developer mode, where this code compiles for the
    // tests) refuse symlinks. A real file with the same contents enables the unit just as
    // well — systemd only requires that the name exist in the .wants directory.
    std::ofstream fallback(link, std::ios::binary | std::ios::trunc);
    if (!fallback) return false;
    fallback << unit_file(executable_path);
    fallback.close();
    return static_cast<bool>(fallback);
}

bool remove_unit(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code link_ec;
    std::error_code unit_ec;
    // The link goes first: if removing the unit succeeded and removing the link did not, the
    // in-between state is a dangling link that systemd complains about on every login.
    std::filesystem::remove(enable_link_path(dir), link_ec);
    std::filesystem::remove(unit_path(dir), unit_ec);
    return !link_ec && !unit_ec;
}

std::filesystem::path user_unit_dir() {
    // XDG_CONFIG_HOME wins when set — a user who has moved their config directory expects
    // everything to follow, and systemd itself reads it the same way.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "systemd" / "user";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "systemd" / "user";
    }
    return {};
}

std::string current_executable_path() {
#if defined(__linux__)
    std::error_code ec;
    // /proc/self/exe is a symlink to the running binary; reading it survives the binary being
    // renamed or the process having been started through a relative path.
    const auto resolved = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return resolved.string();
#else
    return {};
#endif
}

}  // namespace systemd
}  // namespace snapback
