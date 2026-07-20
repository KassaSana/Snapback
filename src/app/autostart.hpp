// Start-on-login (autostart) toggle. Roadmap 1.3.
//
// Windows: reads/writes a value under HKCU\...\CurrentVersion\Run — the standard
// per-user mechanism, no elevation required. launchd (macOS) and systemd user units
// (Linux) are follow-ups; until then autostart_enabled() is always false and
// set_autostart_enabled() is a documented no-op there, so the app and its tests build
// and run identically on every OS (unlike Tray/Overlay, which only link on Windows —
// see Roadmap 3.1/3.2 — this module is a single translation unit with an #if inside it,
// not a singleton only one platform implements).
#pragma once

#include <string>
#include <string_view>

namespace snapback {

// Pure: the exact command line written to the Run key. Quoted so paths containing
// spaces (e.g. "C:\Program Files\Snapback\snapback.exe") parse correctly. Exposed here
// (not hidden in the .cpp) so it's testable without touching the registry.
inline std::string autostart_command_line(std::string_view executable_path) {
    return "\"" + std::string(executable_path) + "\"";
}

// True if Snapback is currently registered to start on login.
bool autostart_enabled();

// True when this build has a real start-on-login backend.
bool autostart_supported();

// Enable/disable start-on-login. Returns false if the platform has no backend yet, or
// the write failed (e.g. registry access denied) — never throws.
bool set_autostart_enabled(bool enabled);

}  // namespace snapback
