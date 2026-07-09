use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Modifiers, Shortcut, ShortcutState};

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

    if let Err(err) =
        state
            .storage
            .lock()
            .save_label(&session.session_id, label, LabelSource::Hotkey, None)
    {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn label_for_shortcut_maps_each_registered_digit() {
        let modifiers = Modifiers::CONTROL | Modifiers::SHIFT;
        assert_eq!(
            label_for_shortcut(&Shortcut::new(Some(modifiers), Code::Digit1)),
            Some(FocusLabel::DeepFocus)
        );
        assert_eq!(
            label_for_shortcut(&Shortcut::new(Some(modifiers), Code::Digit2)),
            Some(FocusLabel::Productive)
        );
        assert_eq!(
            label_for_shortcut(&Shortcut::new(Some(modifiers), Code::Digit3)),
            Some(FocusLabel::PseudoProductive)
        );
        assert_eq!(
            label_for_shortcut(&Shortcut::new(Some(modifiers), Code::Digit4)),
            Some(FocusLabel::Distracted)
        );
    }

    #[test]
    fn label_for_shortcut_ignores_unregistered_combinations() {
        let modifiers = Modifiers::CONTROL | Modifiers::SHIFT;
        // Digit5 was never registered as a label hotkey.
        assert_eq!(
            label_for_shortcut(&Shortcut::new(Some(modifiers), Code::Digit5)),
            None
        );
        // Same digit without the Ctrl+Shift modifiers shouldn't match either.
        assert_eq!(
            label_for_shortcut(&Shortcut::new(Some(Modifiers::CONTROL), Code::Digit1)),
            None
        );
    }

    #[test]
    fn focus_label_name_matches_serde_serialization() {
        // `focus_label_name` populates the `label` field of the `label-hotkey`
        // event payload. It must stay in lockstep with how the rest of the app
        // stringifies `FocusLabel` — serde's SCREAMING_SNAKE_CASE form — so the
        // string a consumer sees is the same regardless of which path produced
        // it. Asserting against the serde output (rather than hardcoded
        // literals) makes this catch drift if either side is renamed.
        //
        // Note: SQLite/CSV persistence stores the *integer* discriminant
        // (`label as i32` in storage::save_label_at), not this string.
        for label in [
            FocusLabel::DeepFocus,
            FocusLabel::Productive,
            FocusLabel::PseudoProductive,
            FocusLabel::Distracted,
        ] {
            let serde_form = serde_json::to_value(label)
                .expect("FocusLabel serializes")
                .as_str()
                .expect("FocusLabel serializes to a JSON string")
                .to_string();
            assert_eq!(focus_label_name(label), serde_form);
        }
    }

    #[test]
    fn focus_label_display_is_human_readable_for_every_variant() {
        for label in [
            FocusLabel::DeepFocus,
            FocusLabel::Productive,
            FocusLabel::PseudoProductive,
            FocusLabel::Distracted,
        ] {
            assert!(!focus_label_display(label).is_empty());
        }
    }
}
