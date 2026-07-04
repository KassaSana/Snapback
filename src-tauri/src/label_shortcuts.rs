use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_global_shortcut::{
    Code, GlobalShortcutExt, Modifiers, Shortcut, ShortcutState,
};

use crate::state::AppState;
use crate::types::{FocusLabel, LabelSource};

pub fn setup_label_shortcuts(app: &tauri::App) -> tauri::Result<()> {
    #[cfg(not(desktop))]
    {
        let _ = app;
        return Ok(());
    }

    #[cfg(desktop)]
    {
        let shortcuts = [
            (
                Shortcut::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::Digit1),
                FocusLabel::DeepFocus,
            ),
            (
                Shortcut::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::Digit2),
                FocusLabel::Productive,
            ),
            (
                Shortcut::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::Digit3),
                FocusLabel::PseudoProductive,
            ),
            (
                Shortcut::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::Digit4),
                FocusLabel::Distracted,
            ),
        ];

        app.handle().plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, shortcut, event| {
                    if event.state != ShortcutState::Pressed {
                        return;
                    }
                    let Some(label) = label_for_shortcut(shortcut) else {
                        return;
                    };
                    save_hotkey_label(app, label);
                })
                .build(),
        )?;

        for (shortcut, _) in shortcuts {
            if let Err(err) = app.global_shortcut().register(shortcut) {
                log::warn!("failed to register label shortcut: {err}");
            }
        }

        Ok(())
    }
}

fn label_for_shortcut(shortcut: &Shortcut) -> Option<FocusLabel> {
    let modifiers = Modifiers::CONTROL | Modifiers::SHIFT;
    if shortcut.matches(modifiers, Code::Digit1) {
        Some(FocusLabel::DeepFocus)
    } else if shortcut.matches(modifiers, Code::Digit2) {
        Some(FocusLabel::Productive)
    } else if shortcut.matches(modifiers, Code::Digit3) {
        Some(FocusLabel::PseudoProductive)
    } else if shortcut.matches(modifiers, Code::Digit4) {
        Some(FocusLabel::Distracted)
    } else {
        None
    }
}

fn save_hotkey_label(app: &AppHandle, label: FocusLabel) {
    let Some(state) = app.try_state::<AppState>() else {
        return;
    };

    let session = match state.storage.lock().get_active_session() {
        Ok(Some(session)) => session,
        Ok(None) => {
            let _ = app.emit(
                "label-hotkey",
                serde_json::json!({
                    "ok": false,
                    "message": "Start a session to save feedback.",
                }),
            );
            return;
        }
        Err(err) => {
            log::warn!("failed to read active session for hotkey label: {err}");
            return;
        }
    };

    if let Err(err) = state.storage.lock().save_label(
        &session.session_id,
        label,
        LabelSource::Hotkey,
        None,
    ) {
        log::warn!("failed to save hotkey label: {err}");
        let _ = app.emit(
            "label-hotkey",
            serde_json::json!({
                "ok": false,
                "message": "Could not save feedback.",
            }),
        );
        return;
    }

    let _ = app.emit(
        "label-hotkey",
        serde_json::json!({
            "ok": true,
            "label": focus_label_name(label),
            "sessionId": session.session_id,
            "message": format!("Hotkey saved: {}", focus_label_display(label)),
        }),
    );
}

fn focus_label_name(label: FocusLabel) -> &'static str {
    match label {
        FocusLabel::DeepFocus => "DEEP_FOCUS",
        FocusLabel::Productive => "PRODUCTIVE",
        FocusLabel::PseudoProductive => "PSEUDO_PRODUCTIVE",
        FocusLabel::Distracted => "DISTRACTED",
    }
}

fn focus_label_display(label: FocusLabel) -> &'static str {
    match label {
        FocusLabel::DeepFocus => "Deep focus",
        FocusLabel::Productive => "Focused",
        FocusLabel::PseudoProductive => "Drift",
        FocusLabel::Distracted => "Distracted",
    }
}
