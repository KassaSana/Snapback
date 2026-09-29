// Start-on-login (autostart) toggle. Roadmap 1.3.
//
// Windows writes a value under HKCU\...\CurrentVersion\Run (autostart_run_key), macOS a
// launchd agent (autostart_launchd), Linux a systemd user unit (autostart_systemd). Any other
// platform has no backend: autostart_supported() is false and set_autostart_enabled() is a
// documented no-op. This module is a single translation unit with an #if inside it, not a
// singleton only one platform implements (unlike Tray/Overlay — see Roadmap 3.1/3.2).
#pragma once

namespace snapback {

// True if Snapback is currently registered to start on login.
bool autostart_enabled();

// True when this build has a real start-on-login backend.
bool autostart_supported();

// Enable/disable start-on-login. Returns false if the platform has no backend yet, or
// the write failed (e.g. registry access denied) — never throws.
bool set_autostart_enabled(bool enabled);

}  // namespace snapback
