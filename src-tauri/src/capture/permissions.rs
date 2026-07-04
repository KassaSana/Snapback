use crate::types::PermissionStatus;

pub fn check_permissions() -> PermissionStatus {
    let active_window_available = active_win_pos_rs::get_active_window().is_ok();
    let capture_available = probe_capture();
    let setup_steps =
        platform_setup_steps(!active_window_available, !capture_available);

    let message = if let Some(msg) = capture_unavailable_message() {
        msg
    } else if !active_window_available && !capture_available {
        platform_both_missing_message()
    } else if !active_window_available {
        "Active window detection unavailable. Grant Accessibility permission (see steps below)."
            .to_string()
    } else if !capture_available {
        "Input capture unavailable. Grant Input Monitoring permission (see steps below)."
            .to_string()
    } else {
        "Capture permissions look good.".to_string()
    };

    PermissionStatus {
        capture_available,
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
        "Capture listener exited — quit and relaunch Snapback after fixing permissions.".to_string(),
    );
    status
}

fn capture_unavailable_message() -> Option<String> {
    #[cfg(target_os = "linux")]
    {
        if std::env::var("WAYLAND_DISPLAY").is_ok() && std::env::var("DISPLAY").is_err() {
            return Some(
                "Wayland-only session detected. Global input capture may be blocked; use X11 or a compatible compositor."
                    .to_string(),
            );
        }
    }
    None
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
                "System Settings → Privacy & Security → Accessibility → enable Snapback.".to_string(),
            );
        }
        if need_input {
            steps.push(
                "System Settings → Privacy & Security → Input Monitoring → enable Snapback."
                    .to_string(),
            );
        }
        if need_accessibility || need_input {
            steps.push("Quit Snapback completely, reopen it, then click Refresh permissions.".to_string());
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
    #[cfg(all(not(target_os = "macos"), not(target_os = "windows")))]
    {
        let mut steps = Vec::new();
        if need_accessibility || need_input {
            steps.push(
                "On Wayland, global capture may be unavailable — try an X11 session if possible."
                    .to_string(),
            );
            steps.push(
                "On X11, ensure your user can access the input device (some distros need `input` group)."
                    .to_string(),
            );
            steps.push("Restart Snapback after changing session or groups.".to_string());
        }
        steps
    }
}

#[cfg(target_os = "macos")]
fn probe_capture() -> bool {
    active_win_pos_rs::get_active_window().is_ok()
}

#[cfg(not(target_os = "macos"))]
fn probe_capture() -> bool {
    true
}
