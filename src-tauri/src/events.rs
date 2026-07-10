//! Shared helper for emitting Tauri events without silently dropping failures.
//!
//! Mirrors the capture/storage philosophy elsewhere in the app: log dropped
//! work (`send_event`, `collect_rows_logging_dropped`) instead of swallowing the
//! error. An emit failing usually means the webview is gone, but a silent
//! `let _ = app.emit(...)` leaves no trace when a *meaningful* event (capture
//! failure, persistence failure) never reaches the UI.

use serde::Serialize;
use tauri::{AppHandle, Emitter};

/// The warning line logged when an event emit fails. Pure so it can be tested
/// without a running Tauri app.
fn emit_failure_message(event: &str, err: &str) -> String {
    format!("failed to emit '{event}': {err}")
}

/// Emit a Tauri event, logging a warning if it fails instead of discarding the
/// error.
pub fn emit_or_log<S: Serialize + Clone>(app: &AppHandle, event: &str, payload: S) {
    if let Err(err) = app.emit(event, payload) {
        log::warn!("{}", emit_failure_message(event, &err.to_string()));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn emit_failure_message_names_event_and_error() {
        let msg = emit_failure_message("prediction", "channel closed");
        assert!(msg.contains("prediction"), "should name the event");
        assert!(msg.contains("channel closed"), "should include the error");
    }
}
