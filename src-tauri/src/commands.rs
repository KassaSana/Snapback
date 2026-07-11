use tauri::{Manager, State};

use crate::state::{classifier_status, AppState};
use crate::training_deploy::{TrainFromExportResult, TrainingDeployStatus};
use crate::types::{
    AppRuleRecord, ClassifierStatus, ContextSnapshotDto, ExportTrainingResult, FocusMode,
    HealthStatus, LabelRequest, PredictionRecord, SessionRecap, SessionRecord, SessionSummary,
    UpsertAppRuleRequest,
};

const MAX_HISTORY_LIMIT: usize = 500;
const MAX_SESSION_GOAL_LEN: usize = 280;
const MAX_LABEL_NOTES_LEN: usize = 2_000;
const MAX_APP_RULE_PATTERN_LEN: usize = 200;
const MAX_APP_RULE_NOTE_LEN: usize = 500;
const MAX_REPO_PATH_LEN: usize = 4_096;

fn clamp_limit(limit: Option<usize>, default: usize) -> usize {
    limit.unwrap_or(default).min(MAX_HISTORY_LIMIT)
}

fn validate_required_text(name: &str, value: &str, max_len: usize) -> Result<String, String> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return Err(format!("{name} is required."));
    }
    if trimmed.chars().count() > max_len {
        return Err(format!("{name} must be at most {max_len} characters."));
    }
    Ok(trimmed.to_string())
}

fn validate_optional_text(
    name: &str,
    value: Option<String>,
    max_len: usize,
) -> Result<Option<String>, String> {
    match value {
        Some(text) => {
            let trimmed = text.trim();
            if trimmed.is_empty() {
                Ok(None)
            } else if trimmed.chars().count() > max_len {
                Err(format!("{name} must be at most {max_len} characters."))
            } else {
                Ok(Some(trimmed.to_string()))
            }
        }
        None => Ok(None),
    }
}

#[tauri::command]
pub fn get_health(app: tauri::AppHandle, state: State<'_, AppState>) -> HealthStatus {
    let app_data_dir = app.path().app_data_dir().ok();
    state.build_health_status(app_data_dir.as_deref())
}

#[tauri::command]
pub fn get_latest_prediction(state: State<'_, AppState>) -> Option<PredictionRecord> {
    if let Some(pred) = state.latest_prediction.lock().clone() {
        return Some(pred);
    }
    state.storage.lock().latest_prediction().ok().flatten()
}

#[tauri::command]
pub fn get_prediction_history(
    state: State<'_, AppState>,
    limit: Option<usize>,
) -> Result<Vec<PredictionRecord>, String> {
    state
        .storage
        .lock()
        .recent_predictions(clamp_limit(limit, 8))
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn start_session(
    state: State<'_, AppState>,
    goal: String,
    focus_mode: Option<String>,
) -> Result<SessionRecord, String> {
    start_session_for_state(&state, &goal, focus_mode.as_deref())
}

fn start_session_for_state(
    state: &AppState,
    goal: &str,
    focus_mode: Option<&str>,
) -> Result<SessionRecord, String> {
    let goal = validate_required_text("Session goal", &goal, MAX_SESSION_GOAL_LEN)?;
    let mode = focus_mode
        .map(FocusMode::from_str)
        .unwrap_or(FocusMode::Normal);
    *state.focus_mode.lock() = mode;
    state.classifier.lock().set_focus_mode(mode);
    let record = state
        .storage
        .lock()
        .start_session(&goal, mode.as_str())
        .map_err(|e| e.to_string())?;
    let started_at = record
        .started_at
        .clone()
        .unwrap_or_else(|| chrono::Utc::now().to_rfc3339());
    state.sync_feature_session_start(&started_at);
    Ok(record)
}

#[tauri::command]
pub fn stop_session(
    state: State<'_, AppState>,
    session_id: String,
) -> Result<SessionRecord, String> {
    stop_session_for_state(&state, &session_id)
}

fn stop_session_for_state(state: &AppState, session_id: &str) -> Result<SessionRecord, String> {
    let record = state
        .storage
        .lock()
        .stop_session(&session_id)
        .map_err(|e| e.to_string())?;

    if let Err(err) = state.storage.lock().save_auto_session_label(&session_id) {
        log::warn!("failed to save automatic session label: {err}");
    }

    state.sync_feature_session_stop();

    Ok(record)
}

#[tauri::command]
pub fn get_session(
    state: State<'_, AppState>,
    session_id: String,
) -> Result<SessionRecord, String> {
    state
        .storage
        .lock()
        .get_session(&session_id)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn get_active_session(state: State<'_, AppState>) -> Result<Option<SessionRecord>, String> {
    state
        .storage
        .lock()
        .get_active_session()
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn submit_label(state: State<'_, AppState>, request: LabelRequest) -> Result<(), String> {
    submit_label_for_state(&state, request)
}

fn submit_label_for_state(state: &AppState, request: LabelRequest) -> Result<(), String> {
    let session_id = validate_required_text("Session ID", &request.session_id, 128)?;
    let notes = validate_optional_text("Label notes", request.notes, MAX_LABEL_NOTES_LEN)?;
    let source = crate::types::LabelSource::parse(request.source.as_deref());
    state
        .storage
        .lock()
        .save_label(&session_id, request.label, source, notes.as_deref())
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn get_session_recap(
    state: State<'_, AppState>,
    session_id: String,
) -> Result<SessionRecap, String> {
    state
        .storage
        .lock()
        .session_recap(&session_id)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn get_session_history(
    state: State<'_, AppState>,
    limit: Option<usize>,
) -> Result<Vec<SessionSummary>, String> {
    let limit = clamp_limit(limit, 20);
    let storage = state.storage.lock();
    let sessions = storage.list_recent_sessions(limit).map_err(|e| e.to_string())?;
    let mut summaries = Vec::with_capacity(sessions.len());
    for record in sessions {
        let recap = storage
            .session_recap(&record.session_id)
            .map_err(|e| e.to_string())?;
        summaries.push(SessionSummary { record, recap });
    }
    Ok(summaries)
}

#[tauri::command]
pub fn set_focus_mode(state: State<'_, AppState>, mode: String) -> Result<(), String> {
    let focus_mode = FocusMode::from_str(&mode);
    *state.focus_mode.lock() = focus_mode;
    state.classifier.lock().set_focus_mode(focus_mode);
    Ok(())
}

#[tauri::command]
pub fn dismiss_snapback(app: tauri::AppHandle, state: State<'_, AppState>) -> Result<(), String> {
    *state.snapback_dismiss_pending.lock() = true;
    if let Some(window) = app.get_webview_window("snapback") {
        let _ = window.hide();
    }
    Ok(())
}

#[tauri::command]
pub fn reload_classifier_model(app: tauri::AppHandle) -> Result<ClassifierStatus, String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    #[cfg(feature = "onnx")]
    if let Some(path) = crate::engine::onnx_model::resolve_model_path(&app_data_dir) {
        crate::engine::onnx_model::init(&path)?;
    }
    Ok(classifier_status(Some(&app_data_dir)))
}

#[tauri::command]
pub fn refresh_permissions(
    app: tauri::AppHandle,
    state: State<'_, AppState>,
) -> Result<crate::types::PermissionStatus, String> {
    let status = crate::capture::check_permissions();
    *state.permissions.lock() = status.clone();

    if status.capture_available && status.active_window_available {
        state.restart_capture_if_needed(&app)?;
    }

    Ok(status)
}

#[tauri::command]
pub fn get_app_rules(state: State<'_, AppState>) -> Result<Vec<AppRuleRecord>, String> {
    state
        .storage
        .lock()
        .list_app_rules()
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn upsert_app_rule(
    state: State<'_, AppState>,
    request: UpsertAppRuleRequest,
) -> Result<AppRuleRecord, String> {
    let pattern = validate_required_text(
        "App rule pattern",
        &request.pattern,
        MAX_APP_RULE_PATTERN_LEN,
    )?;
    let note = validate_optional_text("App rule note", request.note, MAX_APP_RULE_NOTE_LEN)?;
    let record = state
        .storage
        .lock()
        .upsert_app_rule(&pattern, request.rule_type, note.as_deref())
        .map_err(|e| e.to_string())?;
    state.reload_app_rules();
    Ok(record)
}

#[tauri::command]
pub fn delete_app_rule(state: State<'_, AppState>, id: i64) -> Result<(), String> {
    state
        .storage
        .lock()
        .delete_app_rule(id)
        .map_err(|e| e.to_string())?;
    state.reload_app_rules();
    Ok(())
}

#[tauri::command]
pub fn get_context_timeline(
    state: State<'_, AppState>,
    session_id: Option<String>,
    limit: Option<usize>,
) -> Result<Vec<ContextSnapshotDto>, String> {
    let session_id = match session_id {
        Some(id) => id,
        None => {
            let active = state
                .storage
                .lock()
                .get_active_session()
                .map_err(|e| e.to_string())?;
            match active {
                Some(session) => session.session_id,
                None => return Ok(Vec::new()),
            }
        }
    };

    state
        .storage
        .lock()
        .list_context_snapshots(&session_id, clamp_limit(limit, 20))
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn export_training_data(
    app: tauri::AppHandle,
    state: State<'_, AppState>,
    session_id: Option<String>,
) -> Result<ExportTrainingResult, String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let output_dir = app_data_dir.join("exports").join("training");
    state
        .storage
        .lock()
        .export_training_data(&output_dir, session_id.as_deref())
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn get_training_deploy_status(app: tauri::AppHandle) -> Result<TrainingDeployStatus, String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    Ok(crate::training_deploy::training_deploy_status(
        &app_data_dir,
    ))
}

#[tauri::command]
pub fn set_training_repo_path(app: tauri::AppHandle, repo_path: String) -> Result<(), String> {
    let repo_path = validate_required_text("Repo path", &repo_path, MAX_REPO_PATH_LEN)?;
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    crate::training_deploy::write_training_repo_path(
        &app_data_dir,
        std::path::Path::new(&repo_path),
    )
}

#[tauri::command]
pub fn train_from_export(app: tauri::AppHandle) -> Result<TrainFromExportResult, String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    crate::training_deploy::train_from_export(&app_data_dir)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::storage::Storage;

    fn temp_app_state() -> AppState {
        let dir =
            std::env::temp_dir().join(format!("snapback_commands_test_{}", uuid::Uuid::new_v4()));
        AppState::new(Storage::open(dir).unwrap())
    }

    #[test]
    fn clamp_limit_uses_default_and_caps_large_values() {
        assert_eq!(clamp_limit(None, 8), 8);
        assert_eq!(clamp_limit(Some(20), 8), 20);
        assert_eq!(clamp_limit(Some(10_000), 8), MAX_HISTORY_LIMIT);
    }

    #[test]
    fn validate_required_text_trims_and_rejects_empty() {
        assert_eq!(
            validate_required_text("Session goal", "  Ship it  ", MAX_SESSION_GOAL_LEN).unwrap(),
            "Ship it"
        );
        assert!(validate_required_text("Session goal", "   ", MAX_SESSION_GOAL_LEN).is_err());
    }

    #[test]
    fn validate_required_text_rejects_too_long_values() {
        let value = "a".repeat(MAX_SESSION_GOAL_LEN + 1);
        assert!(validate_required_text("Session goal", &value, MAX_SESSION_GOAL_LEN).is_err());
    }

    #[test]
    fn validate_required_text_accepts_exactly_max_len() {
        // Boundary check: a value of exactly max_len must pass, not be
        // rejected — the comparison is `> max_len`, so equality is the edge
        // where an accidental `>=` would silently start rejecting valid input.
        let value = "a".repeat(MAX_SESSION_GOAL_LEN);
        assert!(validate_required_text("Session goal", &value, MAX_SESSION_GOAL_LEN).is_ok());
    }

    #[test]
    fn validate_required_text_counts_unicode_chars_not_bytes() {
        // These helpers use `.chars().count()`, not `.len()` (byte length).
        // A multi-byte character like "é" (2 bytes in UTF-8) must count as
        // ONE character toward the limit, not two — otherwise non-ASCII
        // text would get rejected well before it actually hits the limit.
        let value = "é".repeat(MAX_SESSION_GOAL_LEN);
        assert_eq!(value.len(), MAX_SESSION_GOAL_LEN * 2, "sanity: é is 2 bytes in UTF-8");
        assert!(validate_required_text("Session goal", &value, MAX_SESSION_GOAL_LEN).is_ok());

        let too_long = "é".repeat(MAX_SESSION_GOAL_LEN + 1);
        assert!(validate_required_text("Session goal", &too_long, MAX_SESSION_GOAL_LEN).is_err());
    }

    #[test]
    fn validate_optional_text_trims_blank_and_rejects_too_long_values() {
        assert_eq!(
            validate_optional_text(
                "Label notes",
                Some("  note  ".to_string()),
                MAX_LABEL_NOTES_LEN
            )
            .unwrap(),
            Some("note".to_string())
        );
        assert_eq!(
            validate_optional_text("Label notes", Some("   ".to_string()), MAX_LABEL_NOTES_LEN)
                .unwrap(),
            None
        );
        let long = "n".repeat(MAX_LABEL_NOTES_LEN + 1);
        assert!(validate_optional_text("Label notes", Some(long), MAX_LABEL_NOTES_LEN).is_err());
    }

    #[test]
    fn validate_optional_text_passes_through_none() {
        // The `None` branch (field omitted entirely, as opposed to `Some("")`
        // or `Some("   ")`) was never directly exercised.
        assert_eq!(
            validate_optional_text("Label notes", None, MAX_LABEL_NOTES_LEN).unwrap(),
            None
        );
    }

    #[test]
    fn clamp_limit_passes_through_exactly_at_the_cap() {
        assert_eq!(clamp_limit(Some(MAX_HISTORY_LIMIT), 8), MAX_HISTORY_LIMIT);
    }

    #[test]
    fn start_session_command_core_trims_goal_sets_mode_and_completes_previous_active() {
        let state = temp_app_state();

        let first = start_session_for_state(&state, "  First goal  ", None).unwrap();
        assert_eq!(first.goal, "First goal");
        assert_eq!(first.status, "ACTIVE");
        assert_eq!(first.focus_mode, "normal");
        assert_eq!(*state.focus_mode.lock(), FocusMode::Normal);
        assert_eq!(*state.feature_session_epoch.lock(), 1);
        assert!(state.feature_session_start_ts.lock().is_some());

        let second = start_session_for_state(&state, "  Deep work  ", Some("deep")).unwrap();
        assert_eq!(second.goal, "Deep work");
        assert_eq!(second.status, "ACTIVE");
        assert_eq!(second.focus_mode, "deep");
        assert_eq!(*state.focus_mode.lock(), FocusMode::Deep);
        assert_eq!(*state.feature_session_epoch.lock(), 2);
        assert!(state.feature_session_start_ts.lock().is_some());

        let storage = state.storage.lock();
        let first_after = storage.get_session(&first.session_id).unwrap();
        assert_eq!(first_after.status, "COMPLETED");
        assert!(first_after.ended_at.is_some());

        let active = storage
            .get_active_session()
            .unwrap()
            .expect("new session should be active");
        assert_eq!(active.session_id, second.session_id);
    }

    #[test]
    fn start_session_command_core_rejects_blank_goal_without_changing_state() {
        let state = temp_app_state();

        let err = start_session_for_state(&state, "   ", Some("deep")).unwrap_err();

        assert_eq!(err, "Session goal is required.");
        assert_eq!(*state.focus_mode.lock(), FocusMode::Normal);
        assert_eq!(*state.feature_session_epoch.lock(), 0);
        assert!(state.feature_session_start_ts.lock().is_none());
        assert!(state.storage.lock().get_active_session().unwrap().is_none());
    }

    #[test]
    fn stop_session_command_core_completes_session_and_resets_feature_session() {
        let state = temp_app_state();
        let session = start_session_for_state(&state, "Wrap up", Some("recovery")).unwrap();
        assert!(state.feature_session_start_ts.lock().is_some());

        let stopped = stop_session_for_state(&state, &session.session_id).unwrap();

        assert_eq!(stopped.session_id, session.session_id);
        assert_eq!(stopped.status, "COMPLETED");
        assert!(stopped.ended_at.is_some());
        assert_eq!(*state.feature_session_epoch.lock(), 2);
        assert!(state.feature_session_start_ts.lock().is_none());
        assert!(state.storage.lock().get_active_session().unwrap().is_none());

        let out_dir = std::env::temp_dir().join(format!(
            "snapback_commands_export_{}",
            uuid::Uuid::new_v4()
        ));
        let export = state
            .storage
            .lock()
            .export_training_data(&out_dir, Some(&session.session_id))
            .unwrap();
        assert_eq!(export.label_count, 1);
        let labels = std::fs::read_to_string(export.labels_path).unwrap();
        assert!(labels.contains(",AUTO,"));
        assert!(labels.contains("inferred from session recap"));
    }

    #[test]
    fn submit_label_command_core_trims_session_id_and_notes() {
        let state = temp_app_state();
        let session = start_session_for_state(&state, "Label flow", None).unwrap();

        submit_label_for_state(
            &state,
            LabelRequest {
                session_id: format!("  {}  ", session.session_id),
                label: crate::types::FocusLabel::Productive,
                notes: Some("  steady focus  ".to_string()),
                source: Some("survey".to_string()),
            },
        )
        .unwrap();

        let out_dir = std::env::temp_dir().join(format!(
            "snapback_commands_labels_{}",
            uuid::Uuid::new_v4()
        ));
        let export = state
            .storage
            .lock()
            .export_training_data(&out_dir, Some(&session.session_id))
            .unwrap();
        assert_eq!(export.label_count, 1);
        let labels = std::fs::read_to_string(export.labels_path).unwrap();
        assert!(labels.contains(",1,SURVEY,"));
        assert!(labels.contains("steady focus"));
        assert!(!labels.contains("  steady focus  "));
    }

    #[test]
    fn start_session_command_core_rejects_too_long_goal_without_changing_state() {
        let state = temp_app_state();
        let goal = "a".repeat(MAX_SESSION_GOAL_LEN + 1);

        let err = start_session_for_state(&state, &goal, Some("deep")).unwrap_err();

        assert!(err.contains("at most"), "unexpected error: {err}");
        // A rejected command must leave session/feature state untouched.
        assert_eq!(*state.focus_mode.lock(), FocusMode::Normal);
        assert_eq!(*state.feature_session_epoch.lock(), 0);
        assert!(state.feature_session_start_ts.lock().is_none());
        assert!(state.storage.lock().get_active_session().unwrap().is_none());
    }

    #[test]
    fn stop_session_command_core_errors_on_unknown_session() {
        let state = temp_app_state();
        // No session started; stopping a bogus id must error, not panic or
        // silently succeed.
        assert!(stop_session_for_state(&state, "does-not-exist").is_err());
        assert!(state.storage.lock().get_active_session().unwrap().is_none());
    }

    #[test]
    fn submit_label_command_core_rejects_blank_session_id() {
        let state = temp_app_state();
        let err = submit_label_for_state(
            &state,
            LabelRequest {
                session_id: "   ".to_string(),
                label: crate::types::FocusLabel::Productive,
                notes: None,
                source: Some("manual".to_string()),
            },
        )
        .unwrap_err();
        assert_eq!(err, "Session ID is required.");
    }

    #[test]
    fn submit_label_command_core_rejects_too_long_notes() {
        let state = temp_app_state();
        let session = start_session_for_state(&state, "Notes bound", None).unwrap();
        let notes = "n".repeat(MAX_LABEL_NOTES_LEN + 1);

        let err = submit_label_for_state(
            &state,
            LabelRequest {
                session_id: session.session_id,
                label: crate::types::FocusLabel::Productive,
                notes: Some(notes),
                source: Some("manual".to_string()),
            },
        )
        .unwrap_err();
        assert!(err.contains("at most"), "unexpected error: {err}");
    }

    #[test]
    fn submit_label_command_core_attaches_label_to_intended_session_only() {
        let state = temp_app_state();
        let a = start_session_for_state(&state, "Session A", None).unwrap();
        // Starting B auto-completes A (no auto-label is written on auto-complete).
        let b = start_session_for_state(&state, "Session B", None).unwrap();

        submit_label_for_state(
            &state,
            LabelRequest {
                session_id: a.session_id.clone(),
                label: crate::types::FocusLabel::Productive,
                notes: None,
                source: Some("manual".to_string()),
            },
        )
        .unwrap();

        let storage = state.storage.lock();
        let export_a = storage
            .export_training_data(
                &std::env::temp_dir().join(format!("snapback_intended_a_{}", uuid::Uuid::new_v4())),
                Some(&a.session_id),
            )
            .unwrap();
        let export_b = storage
            .export_training_data(
                &std::env::temp_dir().join(format!("snapback_intended_b_{}", uuid::Uuid::new_v4())),
                Some(&b.session_id),
            )
            .unwrap();

        assert_eq!(export_a.label_count, 1, "label should attach to session A");
        assert_eq!(export_b.label_count, 0, "session B must not receive A's label");
    }
}
