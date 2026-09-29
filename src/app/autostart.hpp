// Start-on-login toggle. Windows: HKCU Run key (autostart_run_key); macOS: launchd agent
// (autostart_launchd); Linux: systemd user unit (autostart_systemd). Elsewhere
// autostart_supported() is false and set_autostart_enabled() is a no-op.
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
