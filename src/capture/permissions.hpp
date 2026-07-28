#pragma once

#include "types.hpp"

namespace snapback {

// Read-only probe: never shows a dialog. Safe to call on every health poll.
// `capture_observed` must come from the live backend (for example, a successfully queued
// event), not from platform capability checks or thread liveness.
PermissionStatus check_capture_permissions(bool capture_running, bool capture_observed = false);

// Actively ask the OS for capture permission, showing the system dialog if one exists.
// Returns true if permission is already held (in which case no dialog appears).
//
// Split from check_capture_permissions deliberately: that one runs on every health poll,
// and prompting from a poll would spam the user with dialogs. Only call this from an
// explicit user action, e.g. the onboarding wizard's "Grant access" button.
//
// macOS is the only platform that has something to ask for today — Accessibility, via
// AXIsProcessTrustedWithOptions with the prompt option set. The dialog is shown once per
// app per system; afterwards macOS silently returns the stored answer, and the user has to
// go to System Settings. That's an OS constraint, not something we can retry around.
bool request_capture_permissions();

}  // namespace snapback
