use std::thread;

use tauri::{AppHandle, Emitter, Manager};

use crate::engine::{check_hyperfocus, Classifier, FeatureExtractor};
use crate::snapback::{show_snapback_overlay, ContextTracker};
use crate::storage::Storage;
use crate::types::{
    AppRuleRecord, CaptureEvent, CaptureFailurePayload, ContextSnapshotDto, EventType, FocusMode,
    PermissionStatus, PredictionRecord, SnapbackPayload,
};

pub struct AppState {
    pub storage: parking_lot::Mutex<Storage>,
    pub permissions: parking_lot::Mutex<PermissionStatus>,
    pub capture_running: parking_lot::Mutex<bool>,
    pub capture_failure_reason: parking_lot::Mutex<Option<String>>,
    pub focus_mode: parking_lot::Mutex<FocusMode>,
    pub classifier: parking_lot::Mutex<Classifier>,
    pub latest_prediction: parking_lot::Mutex<Option<PredictionRecord>>,
    pub app_rules: parking_lot::Mutex<Vec<AppRuleRecord>>,
    pub snapback_dismiss_pending: parking_lot::Mutex<bool>,
    pub feature_session_epoch: parking_lot::Mutex<u64>,
    pub feature_session_start_ts: parking_lot::Mutex<Option<f64>>,
    event_rx: parking_lot::Mutex<Option<std::sync::mpsc::Receiver<CaptureEvent>>>,
}

impl AppState {
    pub fn new(storage: Storage) -> Self {
        let permissions = crate::capture::check_permissions();
        let focus_mode = FocusMode::Normal;
        let app_rules = storage.list_app_rules().unwrap_or_default();
        Self {
            storage: parking_lot::Mutex::new(storage),
            permissions: parking_lot::Mutex::new(permissions),
            capture_running: parking_lot::Mutex::new(false),
            capture_failure_reason: parking_lot::Mutex::new(None),
            focus_mode: parking_lot::Mutex::new(focus_mode),
            classifier: parking_lot::Mutex::new(Classifier::new(focus_mode)),
            latest_prediction: parking_lot::Mutex::new(None),
            app_rules: parking_lot::Mutex::new(app_rules),
            snapback_dismiss_pending: parking_lot::Mutex::new(false),
            feature_session_epoch: parking_lot::Mutex::new(0),
            feature_session_start_ts: parking_lot::Mutex::new(None),
            event_rx: parking_lot::Mutex::new(None),
        }
    }

    pub fn reload_app_rules(&self) {
        if let Ok(rules) = self.storage.lock().list_app_rules() {
            *self.app_rules.lock() = rules;
        }
    }

    pub fn start_engine(&self, app: AppHandle) -> Result<(), String> {
        self.spawn_capture(&app)?;
        thread::spawn(move || run_engine_loop(app));
        Ok(())
    }

    fn spawn_capture(&self, app: &AppHandle) -> Result<(), String> {
        let (tx, rx) = std::sync::mpsc::channel();
        let (failure_tx, failure_rx) = std::sync::mpsc::channel();
        crate::capture::start_capture_thread(tx, failure_tx);
        *self.event_rx.lock() = Some(rx);
        *self.capture_running.lock() = true;
        *self.capture_failure_reason.lock() = None;
        spawn_capture_failure_watcher(app.clone(), failure_rx);
        Ok(())
    }

    pub fn build_health_status(&self, app_data_dir: Option<&std::path::Path>) -> crate::types::HealthStatus {
        let permissions = self.permissions.lock().clone();
        let capture_running = *self.capture_running.lock();
        let capture_failure_reason = self.capture_failure_reason.lock().clone();
        let capture_failed = capture_failure_reason.is_some();

        let status = if capture_failed {
            "capture_failed".to_string()
        } else if !permissions.capture_available || !permissions.active_window_available {
            "degraded".to_string()
        } else if capture_running {
            "online".to_string()
        } else {
            "offline".to_string()
        };

        let classifier = classifier_status(app_data_dir);

        crate::types::HealthStatus {
            status,
            capture_running,
            capture_failed,
            capture_failure_reason,
            permissions,
            classifier,
        }
    }

    pub fn restart_capture_if_needed(&self, app: &AppHandle) -> Result<(), String> {
        if *self.capture_running.lock() && self.capture_failure_reason.lock().is_none() {
            return Ok(());
        }
        self.spawn_capture(app)
    }

    pub fn sync_feature_session_start(&self, started_at: &str) {
        let start_ts = chrono::DateTime::parse_from_rfc3339(started_at)
            .map(|dt| dt.timestamp() as f64)
            .unwrap_or_else(|_| chrono::Utc::now().timestamp() as f64);
        *self.feature_session_start_ts.lock() = Some(start_ts);
        *self.feature_session_epoch.lock() += 1;
    }

    pub fn sync_feature_session_stop(&self) {
        *self.feature_session_start_ts.lock() = None;
        *self.feature_session_epoch.lock() += 1;
    }
}

pub fn classifier_status(app_data_dir: Option<&std::path::Path>) -> crate::types::ClassifierStatus {
    let model_path = app_data_dir.and_then(crate::engine::onnx_model::resolve_model_path);
    #[cfg(feature = "onnx")]
    let onnx_runtime_enabled = true;
    #[cfg(not(feature = "onnx"))]
    let onnx_runtime_enabled = false;

    let backend = if onnx_runtime_enabled && crate::engine::onnx_model::is_loaded() {
        "onnx".to_string()
    } else {
        "heuristic".to_string()
    };

    crate::types::ClassifierStatus {
        backend,
        onnx_runtime_enabled,
        model_path: model_path.map(|path| path.display().to_string()),
    }
}

fn spawn_capture_failure_watcher(app: AppHandle, failure_rx: std::sync::mpsc::Receiver<String>) {
    thread::spawn(move || {
        if let Ok(reason) = failure_rx.recv() {
            let Some(state) = app.try_state::<AppState>() else {
                return;
            };

            *state.capture_running.lock() = false;
            *state.capture_failure_reason.lock() = Some(reason.clone());
            let permissions = crate::capture::capture_failure_message(&reason);
            *state.permissions.lock() = permissions.clone();

            let payload = CaptureFailurePayload {
                reason,
                message: permissions.message.clone(),
                setup_steps: permissions.setup_steps.clone(),
            };
            let _ = app.emit("capture-failed", &payload);
        }
    });
}

fn run_engine_loop(app: AppHandle) {
    let mut extractor = FeatureExtractor::new();
    let mut tracker = ContextTracker::new();
    let mut last_prediction_at = 0.0_f64;
    let mut deep_focus_started: Option<std::time::Instant> = None;
    let mut last_hyperfocus_alert_secs = 0_u64;
    let mut last_feature_epoch = 0_u64;

    loop {
        let Some(state) = app.try_state::<AppState>() else {
            break;
        };

        let feature_epoch = *state.feature_session_epoch.lock();
        if feature_epoch != last_feature_epoch {
            let session_start_ts = *state.feature_session_start_ts.lock();
            extractor.reset_for_session(session_start_ts);
            tracker.reset();
            deep_focus_started = None;
            last_hyperfocus_alert_secs = 0;
            last_feature_epoch = feature_epoch;
        }

        let events: Vec<CaptureEvent> = {
            let guard = state.event_rx.lock();
            if let Some(rx) = guard.as_ref() {
                let mut batch = Vec::new();
                while let Ok(event) = rx.try_recv() {
                    batch.push(event);
                }
                batch
            } else {
                Vec::new()
            }
        };

        if *state.snapback_dismiss_pending.lock() {
            *state.snapback_dismiss_pending.lock() = false;
            tracker.dismiss_recovery();
        }

        for event in events {
            let app_rules = state.app_rules.lock().clone();
            tracker.set_app_rules(&app_rules);

            let snapshot_to_save = if matches!(
                event.event_type,
                EventType::WindowFocusChange | EventType::WindowTitleChange
            ) {
                tracker.on_window_change(&event.app_name, &event.window_title)
            } else {
                tracker.on_activity()
            };
            if let Some(snapshot) = snapshot_to_save {
                persist_context_snapshot(&state, snapshot);
            }

            let features = extractor.update(&event, &app_rules);
            let now = features.timestamp;
            if now - last_prediction_at >= 1.0 {
                let focus_mode = *state.focus_mode.lock();
                state.classifier.lock().set_focus_mode(focus_mode);

                let active_session = state.storage.lock().get_active_session().ok().flatten();
                let session_goal = active_session.as_ref().map(|s| s.goal.as_str());

                let scores = state
                    .classifier
                    .lock()
                    .predict(&features, session_goal, &app_rules);
                extractor.update_focus_score(scores.focus_score / 100.0, 0.2);

                let record = PredictionRecord {
                    session_id: active_session
                        .as_ref()
                        .map(|s| s.session_id.clone())
                        .unwrap_or_default(),
                    focus_score: scores.focus_score,
                    distraction_risk: scores.distraction_risk,
                    focus_state: scores.focus_state.clone(),
                    thrash_score: scores.thrash_score,
                    drift_score: scores.drift_score,
                    goal_alignment: scores.goal_alignment,
                    timestamp: chrono::Utc::now().to_rfc3339(),
                };

                *state.latest_prediction.lock() = Some(record.clone());
                let _ = app.emit("prediction", &record);
                tracker.on_prediction_feedback(&scores.focus_state, session_goal);
                last_prediction_at = now;

                if let Some(session) = active_session {
                    if let Err(err) = state.storage.lock().save_prediction(&record) {
                        log::warn!("failed to save prediction: {err}");
                    }
                    if let Err(err) = state
                        .storage
                        .lock()
                        .save_feature_snapshot(&session.session_id, &features)
                    {
                        log::warn!("failed to save feature snapshot: {err}");
                    }
                }

                if scores.focus_state == "DEEP_FOCUS" {
                    if deep_focus_started.is_none() {
                        deep_focus_started = Some(std::time::Instant::now());
                    }
                } else {
                    deep_focus_started = None;
                }

                if let Some(started) = deep_focus_started {
                    let deep_secs = started.elapsed().as_secs();
                    if let Some(alert) =
                        check_hyperfocus(focus_mode, deep_secs, last_hyperfocus_alert_secs)
                    {
                        let _ = app.emit("hyperfocus", &alert);
                        last_hyperfocus_alert_secs = deep_secs;
                    }
                }
            }
        }

        if let Some(snapback) = tracker.take_pending_snapback() {
            if let Some(session) = state
                .storage
                .lock()
                .get_active_session()
                .ok()
                .flatten()
            {
                if let Err(err) = state
                    .storage
                    .lock()
                    .record_snapback(&session.session_id, &snapback.summary)
                {
                    log::warn!("failed to record snapback: {err}");
                }

                let payload = SnapbackPayload {
                    summary: snapback.summary.clone(),
                    app_name: snapback.app_name,
                    window_title: snapback.window_title,
                    file_hint: snapback.file_hint,
                    distraction_duration_secs: snapback.distraction_duration_secs,
                };
                show_snapback_overlay(&app, &payload);
                let _ = app.emit("snapback", &payload);
            }
        }

        thread::sleep(std::time::Duration::from_millis(100));
    }
}

fn persist_context_snapshot(state: &AppState, snapshot: ContextSnapshotDto) {
    let Some(session) = state
        .storage
        .lock()
        .get_active_session()
        .ok()
        .flatten()
    else {
        return;
    };

    if let Err(err) = state
        .storage
        .lock()
        .save_context_snapshot(&session.session_id, &snapshot)
    {
        log::warn!("failed to save context snapshot: {err}");
    }
}
