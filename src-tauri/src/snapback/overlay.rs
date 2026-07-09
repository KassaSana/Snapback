use tauri::{
    AppHandle, Emitter, Manager, PhysicalPosition, WebviewUrl, WebviewWindow, WebviewWindowBuilder,
};

use crate::types::SnapbackPayload;

/// When false, the overlay appears without pulling keyboard focus from the user's app.
pub const SNAPBACK_STEAL_FOCUS: bool = false;

const OVERLAY_WIDTH: f64 = 420.0;
const OVERLAY_HEIGHT: f64 = 250.0;
const SCREEN_MARGIN: i32 = 20;

pub fn show_snapback_overlay(app: &AppHandle, payload: &SnapbackPayload) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("snapback") {
        return present_overlay(&window, payload, SNAPBACK_STEAL_FOCUS);
    }

    let url = WebviewUrl::App("snapback.html".into());
    let window = WebviewWindowBuilder::new(app, "snapback", url)
        .title("Snapback")
        .inner_size(OVERLAY_WIDTH, OVERLAY_HEIGHT)
        .always_on_top(true)
        .decorations(false)
        .resizable(false)
        .skip_taskbar(true)
        .focused(SNAPBACK_STEAL_FOCUS)
        .visible(false)
        .build()
        .map_err(|err| format!("Could not create snapback overlay window: {err}"))?;

    present_overlay(&window, payload, SNAPBACK_STEAL_FOCUS)
}

fn present_overlay(
    window: &WebviewWindow,
    payload: &SnapbackPayload,
    steal_focus: bool,
) -> Result<(), String> {
    let _ = window.set_always_on_top(true);
    let _ = window.set_focusable(steal_focus);
    position_top_right(window);
    window
        .show()
        .map_err(|err| format!("Could not show snapback overlay window: {err}"))?;
    if steal_focus {
        window
            .set_focus()
            .map_err(|err| format!("Could not focus snapback overlay window: {err}"))?;
    }
    window
        .emit("snapback-data", payload)
        .map_err(|err| format!("Could not send data to snapback overlay: {err}"))?;
    Ok(())
}

fn position_top_right(window: &WebviewWindow) {
    let width = window
        .outer_size()
        .map(|size| size.width as i32)
        .unwrap_or(OVERLAY_WIDTH as i32);

    let Some(monitor) = window.primary_monitor().ok().flatten() else {
        return;
    };

    let monitor_pos = monitor.position();
    let monitor_size = monitor.size();
    let (x, y) = top_right_position(
        (monitor_pos.x, monitor_pos.y),
        (monitor_size.width as i32, monitor_size.height as i32),
        width,
        SCREEN_MARGIN,
    );
    let _ = window.set_position(PhysicalPosition::new(x, y));
}

/// Pulled out of `position_top_right` so the placement math can be tested
/// without a real window/monitor — this is what puts the overlay in the
/// corner instead of dead center or off-screen.
fn top_right_position(
    monitor_pos: (i32, i32),
    monitor_size: (i32, i32),
    window_width: i32,
    margin: i32,
) -> (i32, i32) {
    let x = monitor_pos.0 + monitor_size.0 - window_width - margin;
    let y = monitor_pos.1 + margin;
    (x, y)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn top_right_position_hugs_the_top_right_corner_with_margin() {
        let (x, y) = top_right_position((0, 0), (1920, 1080), 420, 20);
        assert_eq!(x, 1920 - 420 - 20);
        assert_eq!(y, 20);
    }

    #[test]
    fn top_right_position_accounts_for_a_non_origin_monitor() {
        // A monitor to the right of the primary one, e.g. positioned at
        // x=1920 in a multi-monitor layout.
        let (x, y) = top_right_position((1920, 0), (2560, 1440), 420, 20);
        assert_eq!(x, 1920 + 2560 - 420 - 20);
        assert_eq!(y, 20);
    }

    #[test]
    fn top_right_position_uses_the_configured_margin_and_width() {
        let (x, y) = top_right_position((0, 0), (800, 600), OVERLAY_WIDTH as i32, SCREEN_MARGIN);
        assert_eq!(x, 800 - OVERLAY_WIDTH as i32 - SCREEN_MARGIN);
        assert_eq!(y, SCREEN_MARGIN);
    }
}
