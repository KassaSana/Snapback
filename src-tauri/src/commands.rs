use tauri::{Manager, State};

use crate::state::{classifier_status, AppState};
use crate::training_deploy::{TrainFromExportResult, TrainingDeployStatus};
use crate::types::{
    AppRuleRecord, ClassifierStatus, ContextSnapshotDto, ExportTrainingResult, FocusMode,
    HealthStatus, LabelRequest, PredictionRecord, SessionRecap, SessionRecord, UpsertAppRuleRequest,
};

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
        .recent_predictions(limit.unwrap_or(8))
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn start_session(
    state: State<'_, AppState>,
    goal: String,
    focus_mode: Option<String>,
) -> Result<SessionRecord, String> {
    let mode = focus_mode
        .as_deref()
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
pub fn stop_session(state: State<'_, AppState>, session_id: String) -> Result<SessionRecord, String> {
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
pub fn get_session(state: State<'_, AppState>, session_id: String) -> Result<SessionRecord, String> {
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
    let source = crate::types::LabelSource::parse(request.source.as_deref());
    state
        .storage
        .lock()
        .save_label(
            &request.session_id,
            request.label,
            source,
            request.notes.as_deref(),
        )
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
    let record = state
        .storage
        .lock()
        .upsert_app_rule(
            &request.pattern,
            request.rule_type,
            request.note.as_deref(),
        )
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
        .list_context_snapshots(&session_id, limit.unwrap_or(20))
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn export_training_data(
    app: tauri::AppHandle,
    state: State<'_, AppState>,
    session_id: Option<String>,
) -> Result<ExportTrainingResult, String> {
    let app_data_dir = app
        .path()
        .app_data_dir()
        .map_err(|e| e.to_string())?;
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
    Ok(crate::training_deploy::training_deploy_status(&app_data_dir))
}

#[tauri::command]
pub fn set_training_repo_path(app: tauri::AppHandle, repo_path: String) -> Result<(), String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    crate::training_deploy::write_training_repo_path(&app_data_dir, std::path::Path::new(&repo_path))
}

#[tauri::command]
pub fn train_from_export(app: tauri::AppHandle) -> Result<TrainFromExportResult, String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    crate::training_deploy::train_from_export(&app_data_dir)
}
