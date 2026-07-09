use crate::types::PermissionStatus;

pub fn check_permissions() -> PermissionStatus {
    let active_window_available = probe_active_window();
    let capture_available = probe_capture();
    let capture_probe_confirmed = capture_probe_confirmed();
    let setup_steps = platform_setup_steps(!active_window_available, !capture_available);

    let message = permission_message(
        active_window_available,
        capture_available,
        capture_probe_confirmed,
    );

    PermissionStatus {
        capture_available,
        capture_probe_confirmed,
        active_window_available,
        message,
        setup_steps,
    }
}

pub fn capture_failure_message(reason: &str) -> PermissionStatus {
    let mut status = check_permissions();
    status.message = format!("Input capture stopped: {reason}");
    if status.setup_steps.is_empty() {
        status.setup_steps = platform_setup_steps(true, true);
    }
    status.setup_steps.insert(
        0,
        "Capture listener exited — quit and relaunch Snapback after fixing permissions."
            .to_string(),
    );
    status
}

fn probe_active_window() -> bool {
    #[cfg(target_os = "macos")]
    {
        macos_accessibility_trusted()
    }
    #[cfg(not(target_os = "macos"))]
    {
        active_win_pos_rs::get_active_window().is_ok()
    }
}

fn capture_probe_confirmed() -> bool {
    #[cfg(target_os = "macos")]
    {
        true
    }
    #[cfg(not(target_os = "macos"))]
    {
        false
    }
}

fn permission_message(
    active_window_available: bool,
    capture_available: bool,
    capture_probe_confirmed: bool,
) -> String {
    if let Some(msg) = capture_unavailable_message() {
        msg
    } else if capture_available && active_window_available && !capture_probe_confirmed {
        unconfirmed_listener_message().to_string()
    } else if !active_window_available && !capture_available {
        platform_both_missing_message()
    } else if !active_window_available {
        "Active window detection unavailable. Grant Accessibility permission (see steps below)."
            .to_string()
    } else if !capture_available {
        platform_input_permission_message().to_string()
    } else {
        "Capture permissions look good.".to_string()
    }
}

fn unconfirmed_listener_message() -> &'static str {
    #[cfg(target_os = "macos")]
    {
        "Permission preflight passed, but global input capture is not confirmed until the listener starts."
    }
    #[cfg(target_os = "windows")]
    {
        "Active-window access looks available, but Windows global input capture is not confirmed until the listener starts."
    }
    #[cfg(all(not(target_os = "macos"), not(target_os = "windows")))]
    {
        "Capture prerequisites look available, but global input capture is not confirmed until the listener starts."
    }
}

fn capture_unavailable_message() -> Option<String> {
    #[cfg(target_os = "linux")]
    {
        return linux_capture_unavailable_message(
            std::env::var("WAYLAND_DISPLAY").is_ok(),
            std::env::var("DISPLAY").is_ok(),
        );
    }
    #[cfg(not(target_os = "linux"))]
    {
        None
    }
}

#[cfg(any(target_os = "linux", test))]
fn linux_capture_unavailable_message(
    wayland_display_present: bool,
    x11_display_present: bool,
) -> Option<String> {
    if wayland_display_present && !x11_display_present {
        return Some(
            "Wayland-only session detected. Global input capture is unavailable with the current rdev listener; use an X11 or XWayland-backed session."
                .to_string(),
        );
    }
    if !x11_display_present {
        return Some(
            "No X11 display detected. Global input capture requires an X11 session for the current rdev listener."
                .to_string(),
        );
    }
    None
}

fn platform_input_permission_message() -> &'static str {
    #[cfg(target_os = "macos")]
    {
        "Input capture unavailable. Grant Input Monitoring permission (see steps below)."
    }
    #[cfg(not(target_os = "macos"))]
    {
        "Input capture unavailable. Check platform setup steps below."
    }
}

fn platform_both_missing_message() -> String {
    #[cfg(target_os = "macos")]
    {
        "Grant Accessibility and Input Monitoring for Snapback in System Settings (see steps below)."
            .to_string()
    }
    #[cfg(target_os = "windows")]
    {
        "Snapback cannot read the active window or global input. Check steps below and restart the app."
            .to_string()
    }
    #[cfg(not(any(target_os = "macos", target_os = "windows")))]
    {
        "Snapback cannot read the active window or global input. Check steps below and restart the app."
            .to_string()
    }
}

fn platform_setup_steps(need_accessibility: bool, need_input: bool) -> Vec<String> {
    #[cfg(target_os = "macos")]
    {
        let mut steps = Vec::new();
        if need_accessibility {
            steps.push(
                "System Settings → Privacy & Security → Accessibility → enable Snapback."
                    .to_string(),
            );
        }
        if need_input {
            steps.push(
                "System Settings → Privacy & Security → Input Monitoring → enable Snapback."
                    .to_string(),
            );
        }
        if need_accessibility || need_input {
            steps.push(
                "Quit Snapback completely, reopen it, then click Refresh permissions.".to_string(),
            );
        }
        steps
    }
    #[cfg(target_os = "windows")]
    {
        let mut steps = Vec::new();
        if need_accessibility || need_input {
            steps.push(
                "Close tools that monopolize global hooks (some screen recorders or macro apps)."
                    .to_string(),
            );
            steps.push("Restart Snapback, then click Refresh permissions.".to_string());
            steps.push(
                "If capture still fails, reboot once — a stale hook can block rdev.".to_string(),
            );
        }
        steps
    }
    #[cfg(target_os = "linux")]
    {
        linux_setup_steps(need_accessibility, need_input)
    }
    #[cfg(all(
        not(target_os = "macos"),
        not(target_os = "windows"),
        not(target_os = "linux")
    ))]
    {
        let mut steps = Vec::new();
        if need_accessibility || need_input {
            steps.push(
                "Global capture support depends on your desktop session and input-device access."
                    .to_string(),
            );
            steps.push("Restart Snapback after changing session or permissions.".to_string());
        }
        steps
    }
}

#[cfg(target_os = "linux")]
fn linux_setup_steps(need_accessibility: bool, need_input: bool) -> Vec<String> {
    let mut steps = Vec::new();
    if need_accessibility || need_input {
        steps.push(
            "On Wayland, global capture is not supported by the current rdev listener — use an X11 or XWayland-backed session if possible."
                .to_string(),
        );
        steps.push(
            "On X11, ensure Snapback is launched with DISPLAY set and your user can access input devices if your distro requires it."
                .to_string(),
        );
        steps.push("Restart Snapback after changing session or groups.".to_string());
    }
    steps
}

#[cfg(target_os = "macos")]
fn probe_capture() -> bool {
    macos_input_monitoring_trusted()
}

#[cfg(not(target_os = "macos"))]
fn probe_capture() -> bool {
    non_macos_capture_available()
}

fn non_macos_capture_available() -> bool {
    #[cfg(target_os = "windows")]
    {
        // Windows does not expose a separate permission preflight here, so the
        // capture listener remains the source of truth after startup.
        true
    }
    #[cfg(target_os = "linux")]
    {
        linux_capture_unavailable_message(
            std::env::var("WAYLAND_DISPLAY").is_ok(),
            std::env::var("DISPLAY").is_ok(),
        )
        .is_none()
    }
    #[cfg(all(
        not(target_os = "macos"),
        not(target_os = "windows"),
        not(target_os = "linux")
    ))]
    {
        true
    }
}

#[cfg(target_os = "macos")]
fn macos_accessibility_trusted() -> bool {
    use std::ffi::c_uchar;

    #[link(name = "ApplicationServices", kind = "framework")]
    unsafe extern "C" {
        fn AXIsProcessTrusted() -> c_uchar;
    }

    unsafe { AXIsProcessTrusted() != 0 }
}

#[cfg(target_os = "macos")]
fn macos_input_monitoring_trusted() -> bool {
    use std::ffi::c_uchar;

    #[link(name = "CoreGraphics", kind = "framework")]
    unsafe extern "C" {
        fn CGPreflightListenEventAccess() -> c_uchar;
    }

    unsafe { CGPreflightListenEventAccess() != 0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn permission_message_marks_probe_only_state_as_unconfirmed() {
        let message = permission_message(true, true, false);
        assert!(message.contains("not confirmed until the listener starts"));
    }

    #[test]
    fn permission_message_marks_confirmed_probe_as_ready() {
        let message = permission_message(true, true, true);
        assert_eq!(message, "Capture permissions look good.");
    }

    #[test]
    fn linux_capture_message_flags_wayland_without_x11() {
        let message = linux_capture_unavailable_message(true, false);
        assert!(message.is_some());
        assert!(message
            .expect("wayland-only should block capture")
            .contains("Wayland-only session detected"));
    }

    #[test]
    fn linux_capture_message_flags_missing_display() {
        let message = linux_capture_unavailable_message(false, false);
        assert!(message.is_some());
        assert!(message
            .expect("missing DISPLAY should block capture")
            .contains("No X11 display detected"));
    }

    #[test]
    fn linux_capture_message_allows_x11_sessions() {
        assert!(linux_capture_unavailable_message(false, true).is_none());
        assert!(linux_capture_unavailable_message(true, true).is_none());
    }

    #[test]
    fn capture_failure_message_prepends_restart_guidance() {
        let status = capture_failure_message("listener died");
        assert_eq!(status.message, "Input capture stopped: listener died");
        assert!(!status.setup_steps.is_empty());
        assert!(status.setup_steps[0].contains("Capture listener exited"));
    }
}
