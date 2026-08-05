#include "app/autostart.hpp"

#include "app/autostart_launchd.hpp"
#include "app/autostart_run_key.hpp"
#include "app/autostart_systemd.hpp"

namespace snapback {

#if defined(_WIN32)

// The mechanism lives in autostart_run_key.cpp so the registry round trip can be tested
// against a scratch key instead of the shared Run key; this file only decides *which* key.
// Same split as the launchd and systemd branches below (Roadmap 11.7).
bool autostart_enabled() { return run_key::entry_present(run_key::user_run_key_path()); }

bool autostart_supported() { return true; }

bool set_autostart_enabled(bool enabled) {
    const auto key_path = run_key::user_run_key_path();
    if (!enabled) return run_key::remove_entry(key_path);

    const auto executable = run_key::current_executable_path();
    if (executable.empty()) return false;
    return run_key::install_entry(key_path, executable);
}

#elif defined(__APPLE__)  // Roadmap 3.0

// The mechanism lives in autostart_launchd.cpp so its plist text and install/remove logic are
// compiled and tested on every OS; this file only decides *where* the agent goes.
bool autostart_enabled() { return launchd::agent_installed(launchd::user_agent_dir()); }

bool autostart_supported() { return true; }

bool set_autostart_enabled(bool enabled) {
    const auto dir = launchd::user_agent_dir();
    if (dir.empty()) return false;  // no HOME: not a login session, so no login item
    if (!enabled) return launchd::remove_agent(dir);

    const auto executable = launchd::current_executable_path();
    if (executable.empty()) return false;
    return launchd::install_agent(dir, executable);
}

#elif defined(__linux__)  // Roadmap 3.0

// Same shape as the launchd branch: the mechanism is in autostart_systemd.cpp so it is
// compiled and tested on all three CI hosts, and this file only chooses the directory.
bool autostart_enabled() { return systemd::unit_enabled(systemd::user_unit_dir()); }

bool autostart_supported() { return true; }

bool set_autostart_enabled(bool enabled) {
    const auto dir = systemd::user_unit_dir();
    if (dir.empty()) return false;  // neither XDG_CONFIG_HOME nor HOME: no user session
    if (!enabled) return systemd::remove_unit(dir);

    const auto executable = systemd::current_executable_path();
    if (executable.empty()) return false;
    return systemd::install_unit(dir, executable);
}

#else  // no start-on-login backend on this platform

bool autostart_enabled() { return false; }

bool autostart_supported() { return false; }

bool set_autostart_enabled(bool) { return false; }

#endif

}  // namespace snapback
