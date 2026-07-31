#include "app/autostart_launchd.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>

#include <climits>
#include <vector>
#endif

namespace snapback {
namespace launchd {

std::string escape_xml(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            // `&` is replaced first by virtue of being its own case — a naive sequence of
            // string replacements that handled `<` before `&` would turn `&lt;` into
            // `&amp;lt;` and corrupt the escape it just wrote.
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string agent_plist(std::string_view executable_path) {
    std::string plist;
    plist += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    plist +=
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    plist += "<plist version=\"1.0\">\n";
    plist += "<dict>\n";
    plist += "\t<key>Label</key>\n";
    plist += "\t<string>" + std::string(kAgentLabel) + "</string>\n";
    plist += "\t<key>ProgramArguments</key>\n";
    plist += "\t<array>\n";
    plist += "\t\t<string>" + escape_xml(executable_path) + "</string>\n";
    plist += "\t</array>\n";
    // RunAtLoad and nothing else. KeepAlive is deliberately absent: it would make launchd
    // restart Snapback every time the user quits it, turning a login item into something the
    // user cannot switch off from the app — the behaviour people file bugs about.
    plist += "\t<key>RunAtLoad</key>\n";
    plist += "\t<true/>\n";
    // Interactive keeps Snapback out of the throttled background band, which matters for an
    // app that owns a tray item and a window.
    plist += "\t<key>ProcessType</key>\n";
    plist += "\t<string>Interactive</string>\n";
    plist += "</dict>\n";
    plist += "</plist>\n";
    return plist;
}

std::filesystem::path agent_path(const std::filesystem::path& dir) {
    return dir / (std::string(kAgentLabel) + ".plist");
}

bool agent_installed(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(agent_path(dir), ec) && !ec;
}

bool install_agent(const std::filesystem::path& dir, std::string_view executable_path) {
    if (dir.empty() || executable_path.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    std::ofstream out(agent_path(dir), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << agent_plist(executable_path);
    // Checked after writing as well: a full disk fails at flush, not at open, and a truncated
    // plist is exactly the malformed-agent case launchd ignores without complaint.
    out.close();
    return static_cast<bool>(out);
}

bool remove_agent(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::remove(agent_path(dir), ec);
    return !ec;
}

std::filesystem::path user_agent_dir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return {};
    return std::filesystem::path(home) / "Library" / "LaunchAgents";
}

std::string current_executable_path() {
#if defined(__APPLE__)
    // _NSGetExecutablePath reports the size it needs when the buffer is too small, so the
    // first call is a query and the second is the read.
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};

    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};

    // The reported path may be relative or contain symlinks; launchd needs one that resolves
    // from any working directory, at login, years later.
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(buffer.data(), ec);
    if (ec) return std::string(buffer.data());
    return canonical.string();
#else
    return {};
#endif
}

}  // namespace launchd
}  // namespace snapback
