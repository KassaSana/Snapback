#pragma once

#include <cstdint>

#include "types.hpp"

namespace snapback {

// How long a Linux `command -v xdotool` answer is reused.
constexpr std::int64_t kCommandProbeTtlMs = 30'000;

// Read-only probe: never shows a dialog. Safe to call on every health poll.
// `capture_observed` must come from the live backend (for example, a successfully queued
// event), not from platform capability checks or thread liveness.
PermissionStatus check_capture_permissions(bool capture_running, bool capture_observed = false);

// Drop the cached tool-availability answer so the next check_capture_permissions() probes
// afresh. No-op on platforms whose probe is cheap enough not to be cached (Windows, macOS).
void invalidate_permission_probe_cache();

// Ask the OS for capture permission, showing the system dialog if one exists; true if already
// held. Only from an explicit user action (health polls use check_capture_permissions). Only
// macOS has something to ask for, and it shows the dialog once per app.
bool request_capture_permissions();

}  // namespace snapback
