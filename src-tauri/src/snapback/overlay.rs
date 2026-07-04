use tauri::{AppHandle, Emitter, Manager, PhysicalPosition, WebviewUrl, WebviewWindow, WebviewWindowBuilder};

use crate::types::SnapbackPayload;

/// When false, the overlay appears without pulling keyboard focus from the user's app.
pub const SNAPBACK_STEAL_FOCUS: bool = false;

const OVERLAY_WIDTH: f64 = 420.0;
const OVERLAY_HEIGHT: f64 = 250.0;
const SCREEN_MARGIN: i32 = 20;

pub fn show_snapback_overlay(app: &AppHandle, payload: &SnapbackPayload) {
    if let Some(window) = app.get_webview_window("snapback") {
        present_overlay(&window, payload, SNAPBACK_STEAL_FOCUS);
        return;
    }

    let url = WebviewUrl::App("snapback.html".into());
    if let Ok(window) = WebviewWindowBuilder::new(app, "snapback", url)
        .title("Snapback")
        .inner_size(OVERLAY_WIDTH, OVERLAY_HEIGHT)
        .always_on_top(true)
        .decorations(false)
        .resizable(false)
        .skip_taskbar(true)
        .focused(SNAPBACK_STEAL_FOCUS)
        .visible(false)
        .build()
    {
        present_overlay(&window, payload, SNAPBACK_STEAL_FOCUS);
    }
}

fn present_overlay(window: &WebviewWindow, payload: &SnapbackPayload, steal_focus: bool) {
    let _ = window.set_always_on_top(true);
    let _ = window.set_focusable(steal_focus);
    position_top_right(window);
    let _ = window.show();
    if steal_focus {
        let _ = window.set_focus();
    }
    let _ = window.emit("snapback-data", payload);
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
    let x = monitor_pos.x + monitor_size.width as i32 - width - SCREEN_MARGIN;
    let y = monitor_pos.y + SCREEN_MARGIN;
    let _ = window.set_position(PhysicalPosition::new(x, y));
}
