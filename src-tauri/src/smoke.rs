use std::path::{Path, PathBuf};
use std::process::Command;

use chrono::{Duration, Utc};
use serde::Serialize;

use crate::engine::features::FeatureVector;
use crate::state::classifier_status;
use crate::storage::Storage;
use crate::training_deploy::{self, training_deploy_status, TrainFromExportResult};
use crate::types::{ContextSnapshotDto, FocusLabel, LabelSource, PredictionRecord};

const SMOKE_SESSION_COUNT: usize = 4;
const FEATURES_PER_SESSION: usize = 6;
const LABELS_PER_SESSION: usize = 3;
const SESSION_GAP_SECONDS: i64 = 900;
const SESSION_BASE_OFFSET_SECONDS: i64 = 5400;
const MIN_EXPECTED_FEATURE_ROWS: usize = SMOKE_SESSION_COUNT * FEATURES_PER_SESSION;
const MIN_EXPECTED_LABEL_ROWS: usize = SMOKE_SESSION_COUNT * (LABELS_PER_SESSION + 1);

#[derive(Debug, Clone, Serialize)]
struct SmokeCheck {
    stage: &'static str,
    ok: bool,
    detail: String,
}

#[derive(Debug, Clone, Serialize)]
struct SmokeReport {
    version: u8,
    ok: bool,
    summary: String,
    app_data_dir: String,
    export_dir: String,
    repo_path: Option<String>,
    classifier_backend: Option<String>,
    model_path: Option<String>,
    checks: Vec<SmokeCheck>,
}

impl SmokeReport {
    fn new(app_data_dir: &Path) -> Self {
        let export_dir = training_deploy::export_dir(app_data_dir);
        Self {
            version: 1,
            ok: false,
            summary: String::new(),
            app_data_dir: app_data_dir.display().to_string(),
            export_dir: export_dir.display().to_string(),
            repo_path: None,
            classifier_backend: None,
            model_path: None,
            checks: Vec::new(),
        }
    }

    fn pass(&mut self, stage: &'static str, detail: impl Into<String>) {
        self.checks.push(SmokeCheck {
            stage,
            ok: true,
            detail: detail.into(),
        });
    }

    fn fail(&mut self, stage: &'static str, detail: impl Into<String>) {
        self.checks.push(SmokeCheck {
            stage,
            ok: false,
            detail: detail.into(),
        });
    }
}

#[derive(Debug, Clone)]
struct SmokeFailure {
    stage: &'static str,
    detail: String,
}

impl SmokeFailure {
    fn new(stage: &'static str, detail: impl Into<String>) -> Self {
        Self {
            stage,
            detail: detail.into(),
        }
    }
}

#[derive(Debug, Clone, Copy)]
struct SessionArchetype {
    goal: &'static str,
    app_name: &'static str,
    window_title: &'static str,
    focus_state: &'static str,
    label: FocusLabel,
    focus_score: f64,
    distraction_risk: f64,
    thrash_score: f64,
    drift_score: f64,
    goal_alignment: f64,
    keystroke_count: usize,
    keystroke_rate: f64,
    context_switches: usize,
    idle_time_30s: f64,
    focus_momentum: f64,
    is_browser: bool,
    is_ide: bool,
    is_communication: bool,
    is_entertainment: bool,
    is_productivity: bool,
    is_pseudo_productive: bool,
}

#[derive(Debug, Clone, Copy)]
struct SeedStats {
    session_count: usize,
    feature_count: usize,
    label_count: usize,
}

pub fn run_smoke_cli(_args: &[String]) -> i32 {
    let app_data_dir =
        std::env::temp_dir().join(format!("snapback-smoke-{}", uuid::Uuid::new_v4()));
    let mut report = SmokeReport::new(&app_data_dir);

    let result = run_smoke(&app_data_dir, &mut report);
    match result {
        Ok(()) => {
            report.ok = true;
            if report.summary.is_empty() {
                report.summary = "Smoke harness completed successfully.".to_string();
            }
            print_report(&report);
            0
        }
        Err(err) => {
            report.ok = false;
            report.summary = err.detail.clone();
            report.fail(err.stage, err.detail);
            print_report(&report);
            1
        }
    }
}

fn run_smoke(app_data_dir: &Path, report: &mut SmokeReport) -> Result<(), SmokeFailure> {
    emit_stage("repo", "resolving training repo");
    let repo_path = resolve_repo_path()
        .ok_or_else(|| SmokeFailure::new("repo", "Could not resolve the Snapback repo path."))?;
    report.repo_path = Some(repo_path.display().to_string());
    training_deploy::write_training_repo_path(app_data_dir, &repo_path)
        .map_err(|err| SmokeFailure::new("repo", err))?;
    report.pass(
        "repo",
        format!("Using training repo at {}", repo_path.display()),
    );

    let storage = Storage::open(app_data_dir.to_path_buf())
        .map_err(|err| SmokeFailure::new("storage", err.to_string()))?;
    emit_stage("session_gate", "verifying persistence guardrails");
    verify_session_gate(&storage, report)?;
    emit_stage("seed", "seeding deterministic smoke sessions");
    let seed = seed_smoke_sessions(&storage).map_err(|err| SmokeFailure::new("seed", err))?;
    report.pass(
        "seed",
        format!(
            "Seeded {} sessions with {} feature snapshots and {} labels.",
            seed.session_count, seed.feature_count, seed.label_count
        ),
    );

    let export_dir = training_deploy::export_dir(app_data_dir);
    emit_stage("export", "exporting training csvs");
    let export = storage
        .export_training_data(&export_dir, None)
        .map_err(|err| SmokeFailure::new("export", err.to_string()))?;
    check_export_thresholds(export.feature_count, export.label_count)?;
    let deploy_status = training_deploy_status(app_data_dir);
    if !deploy_status.has_export {
        return Err(SmokeFailure::new(
            "export",
            "Training deploy status did not detect exported features.csv and labels.csv.",
        ));
    }
    report.pass(
        "export",
        format!(
            "Exported {} feature rows and {} label rows to {}.",
            export.feature_count, export.label_count, export.output_dir
        ),
    );

    emit_stage("train", "running train_from_export");
    let train = training_deploy::train_from_export(app_data_dir)
        .map_err(|err| SmokeFailure::new("train", err))?;
    let post_train_status = training_deploy_status(app_data_dir);
    check_train_outcome(
        &train,
        post_train_status.model_onnx_exists,
        post_train_status.metrics_exists,
    )?;
    report.pass(
        "train",
        format!(
            "Training produced a deployable ONNX model. {}",
            train.message
        ),
    );

    emit_stage("onnx", "loading trained model into runtime");
    let classifier = load_onnx_backend(app_data_dir)?;
    report.classifier_backend = Some(classifier.backend.clone());
    report.model_path = classifier.model_path.clone();
    report.pass(
        "onnx",
        format!(
            "Loaded ONNX backend from {}.",
            classifier
                .model_path
                .as_deref()
                .unwrap_or("<unknown model path>")
        ),
    );

    report.summary = format!(
        "Validated session lifecycle, export, training, and ONNX reload in {}.",
        app_data_dir.display()
    );
    Ok(())
}

/// Exported CSVs must carry at least the deterministic minimums the seed
/// produces. A shortfall means export dropped rows or the seed changed.
fn check_export_thresholds(feature_count: usize, label_count: usize) -> Result<(), SmokeFailure> {
    if feature_count < MIN_EXPECTED_FEATURE_ROWS {
        return Err(SmokeFailure::new(
            "export",
            format!(
                "Expected at least {MIN_EXPECTED_FEATURE_ROWS} exported feature rows, found {feature_count}."
            ),
        ));
    }
    if label_count < MIN_EXPECTED_LABEL_ROWS {
        return Err(SmokeFailure::new(
            "export",
            format!(
                "Expected at least {MIN_EXPECTED_LABEL_ROWS} exported label rows, found {label_count}."
            ),
        ));
    }
    Ok(())
}

/// A release-grade training run must succeed, be deploy-ready with an exported
/// ONNX model, and leave both `model.onnx` and `metrics.json` on disk. Each
/// failure names exactly which guarantee broke so a failing smoke run is easy
/// to diagnose.
fn check_train_outcome(
    train: &TrainFromExportResult,
    model_onnx_exists: bool,
    metrics_exists: bool,
) -> Result<(), SmokeFailure> {
    if !train.training_succeeded {
        return Err(SmokeFailure::new(
            "train",
            format!("Training did not succeed: {} Log tail:\n{}", train.message, train.log_tail),
        ));
    }
    if !train.deploy_ready || !train.onnx_exported {
        return Err(SmokeFailure::new(
            "train",
            format!(
                "Training was not deploy-ready (deploy_ready={}, onnx_exported={}): {} Log tail:\n{}",
                train.deploy_ready, train.onnx_exported, train.message, train.log_tail
            ),
        ));
    }
    if !model_onnx_exists {
        return Err(SmokeFailure::new(
            "train",
            "Training reported success, but export_dir/model.onnx was missing afterward.",
        ));
    }
    if !metrics_exists {
        return Err(SmokeFailure::new(
            "train",
            "Training reported success, but metrics.json was missing afterward.",
        ));
    }
    Ok(())
}

fn verify_session_gate(storage: &Storage, report: &mut SmokeReport) -> Result<(), SmokeFailure> {
    let before_predictions = storage
        .prediction_count()
        .map_err(|err| SmokeFailure::new("session_gate", err.to_string()))?;
    let before_features = storage
        .feature_snapshot_count()
        .map_err(|err| SmokeFailure::new("session_gate", err.to_string()))?;
    let feature = FeatureVector::empty(unix_timestamp(Utc::now()));
    let record = PredictionRecord {
        session_id: "idle".to_string(),
        focus_score: 50.0,
        distraction_risk: 0.5,
        focus_state: "PRODUCTIVE".to_string(),
        thrash_score: 0.1,
        drift_score: 0.1,
        goal_alignment: 0.5,
        timestamp: Utc::now().to_rfc3339(),
    };
    if storage.save_prediction(&record).is_ok() {
        return Err(SmokeFailure::new(
            "session_gate",
            "Expected save_prediction to reject a non-active session.",
        ));
    }
    if storage.save_feature_snapshot("idle", &feature).is_ok() {
        return Err(SmokeFailure::new(
            "session_gate",
            "Expected save_feature_snapshot to reject a non-active session.",
        ));
    }

    let after_predictions = storage
        .prediction_count()
        .map_err(|err| SmokeFailure::new("session_gate", err.to_string()))?;
    let after_features = storage
        .feature_snapshot_count()
        .map_err(|err| SmokeFailure::new("session_gate", err.to_string()))?;
    if before_predictions != after_predictions || before_features != after_features {
        return Err(SmokeFailure::new(
            "session_gate",
            "Prediction or feature counts changed without an active session.",
        ));
    }

    report.pass(
        "session_gate",
        "Prediction and feature writes stay blocked until a session is active.",
    );
    Ok(())
}

fn seed_smoke_sessions(storage: &Storage) -> Result<SeedStats, String> {
    let base_time = Utc::now() - Duration::seconds(SESSION_BASE_OFFSET_SECONDS);
    let mut feature_count = 0;
    let mut label_count = 0;

    for (session_index, archetype) in session_archetypes().iter().enumerate() {
        let session = storage
            .start_session(archetype.goal, "normal")
            .map_err(|err| err.to_string())?;
        let session_base =
            base_time + Duration::seconds(session_index as i64 * SESSION_GAP_SECONDS);

        for sample_index in 0..FEATURES_PER_SESSION {
            let timestamp = session_base + Duration::seconds(sample_index as i64 * 60);
            let feature = build_feature(archetype, unix_timestamp(timestamp), sample_index);
            storage
                .save_feature_snapshot(&session.session_id, &feature)
                .map_err(|err| err.to_string())?;
            storage
                .save_context_snapshot(
                    &session.session_id,
                    &ContextSnapshotDto {
                        app_name: archetype.app_name.to_string(),
                        window_title: format!("{} #{sample_index}", archetype.window_title),
                        file_hint: "smoke.rs".to_string(),
                        project_hint: "Snapback".to_string(),
                        summary: format!(
                            "{} smoke sample {}",
                            archetype.focus_state.to_lowercase(),
                            sample_index + 1
                        ),
                        timestamp: timestamp.to_rfc3339(),
                    },
                )
                .map_err(|err| err.to_string())?;
            storage
                .save_prediction(&PredictionRecord {
                    session_id: session.session_id.clone(),
                    focus_score: archetype.focus_score,
                    distraction_risk: archetype.distraction_risk,
                    focus_state: archetype.focus_state.to_string(),
                    thrash_score: archetype.thrash_score,
                    drift_score: archetype.drift_score,
                    goal_alignment: archetype.goal_alignment,
                    timestamp: timestamp.to_rfc3339(),
                })
                .map_err(|err| err.to_string())?;
            feature_count += 1;
        }

        for (label_index, source) in [
            LabelSource::Manual,
            LabelSource::Hotkey,
            LabelSource::Survey,
        ]
        .into_iter()
        .enumerate()
        {
            let label_ts = session_base + Duration::seconds(label_timestamp_offset(label_index));
            storage
                .save_label_at(
                    &session.session_id,
                    archetype.label,
                    source,
                    Some(archetype.focus_state),
                    &label_ts.to_rfc3339(),
                )
                .map_err(|err| err.to_string())?;
            label_count += 1;
        }

        storage
            .stop_session(&session.session_id)
            .map_err(|err| err.to_string())?;
        storage
            .save_auto_session_label(&session.session_id)
            .map_err(|err| err.to_string())?;
        label_count += 1;
    }

    if storage
        .get_active_session()
        .map_err(|err| err.to_string())?
        .is_some()
    {
        return Err("Expected smoke seeding to leave no active session.".to_string());
    }

    Ok(SeedStats {
        session_count: SMOKE_SESSION_COUNT,
        feature_count,
        label_count,
    })
}

fn build_feature(
    archetype: &SessionArchetype,
    timestamp: f64,
    sample_index: usize,
) -> FeatureVector {
    let mut feature = FeatureVector::empty(timestamp);
    feature.seconds_since_session_start = (sample_index * 60) as i64;
    feature.minutes_since_last_break = (sample_index / 2) as i64;
    feature.keystroke_count = archetype.keystroke_count + sample_index;
    feature.keystroke_rate = archetype.keystroke_rate;
    feature.keystroke_interval_mean = if archetype.keystroke_rate > 0.0 {
        (1.0 / archetype.keystroke_rate).max(0.05)
    } else {
        0.0
    };
    feature.keystroke_interval_std = if archetype.distraction_risk > 0.6 {
        0.6
    } else {
        0.18
    };
    feature.mouse_move_count = if archetype.is_entertainment { 6 } else { 2 };
    feature.mouse_distance_pixels = if archetype.is_entertainment {
        320.0
    } else {
        80.0
    };
    feature.mouse_speed_mean = if archetype.is_entertainment { 1.4 } else { 0.3 };
    feature.mouse_speed_std = if archetype.is_entertainment { 0.8 } else { 0.2 };
    feature.mouse_acceleration_mean = if archetype.is_entertainment { 0.5 } else { 0.1 };
    feature.mouse_click_count = if archetype.is_browser { 2 } else { 0 };
    feature.context_switches_30s = archetype.context_switches;
    feature.context_switches_5min = archetype.context_switches + sample_index;
    feature.time_in_current_app = if archetype.context_switches == 0 {
        180
    } else {
        45
    };
    feature.unique_apps_5min = if archetype.context_switches == 0 {
        1
    } else {
        3
    };
    feature.idle_time_30s = archetype.idle_time_30s;
    feature.idle_event_count_5min = usize::from(archetype.idle_time_30s > 0.0);
    feature.longest_active_stretch_5min = if archetype.label == FocusLabel::DeepFocus {
        300
    } else {
        90
    };
    feature.window_title_length = archetype.window_title.len();
    feature.window_title_changed_30s = archetype.context_switches > 0 && sample_index % 2 == 1;
    feature.app_name = archetype.app_name.to_string();
    feature.window_title = format!("{} #{sample_index}", archetype.window_title);
    feature.is_browser = archetype.is_browser;
    feature.is_ide = archetype.is_ide;
    feature.is_communication = archetype.is_communication;
    feature.is_entertainment = archetype.is_entertainment;
    feature.is_productivity = archetype.is_productivity;
    feature.focus_momentum = archetype.focus_momentum;
    feature.productivity_category = if archetype.is_ide {
        "Building".to_string()
    } else if archetype.is_productivity {
        "Organizing".to_string()
    } else if archetype.is_entertainment {
        "Entertainment".to_string()
    } else {
        "Browsing".to_string()
    };
    feature.is_pseudo_productive = archetype.is_pseudo_productive;
    feature
}

fn load_onnx_backend(app_data_dir: &Path) -> Result<crate::types::ClassifierStatus, SmokeFailure> {
    ensure_ort_dylib_path()?;
    let current = classifier_status(Some(app_data_dir));
    if !current.onnx_runtime_enabled {
        return Err(SmokeFailure::new(
            "onnx",
            "ONNX support is not enabled. Rebuild with `--features onnx` before running `--smoke`.",
        ));
    }
    let model_path =
        crate::engine::onnx_model::resolve_model_path(app_data_dir).ok_or_else(|| {
            SmokeFailure::new(
                "onnx",
                "Could not resolve model.onnx in the smoke app-data directory.",
            )
        })?;
    crate::engine::onnx_model::init(&model_path).map_err(|err| SmokeFailure::new("onnx", err))?;
    let classifier = classifier_status(Some(app_data_dir));
    if classifier.backend != "onnx" {
        return Err(SmokeFailure::new(
            "onnx",
            format!(
                "Expected classifier backend to be `onnx` after reload, found `{}`.",
                classifier.backend
            ),
        ));
    }
    Ok(classifier)
}

fn resolve_repo_path() -> Option<PathBuf> {
    if let Ok(value) = std::env::var("SNAPBACK_REPO") {
        let path = PathBuf::from(value);
        if training_deploy::is_training_repo(&path) {
            return Some(path);
        }
    }

    let manifest_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    if let Some(repo_root) = manifest_root.parent() {
        if training_deploy::is_training_repo(repo_root) {
            return Some(repo_root.to_path_buf());
        }
    }

    std::env::current_dir()
        .ok()
        .filter(|path| training_deploy::is_training_repo(path))
}

fn label_timestamp_offset(label_index: usize) -> i64 {
    match label_index {
        0 => 90,
        1 => 210,
        _ => 330,
    }
}

fn session_archetypes() -> [SessionArchetype; SMOKE_SESSION_COUNT] {
    [
        SessionArchetype {
            goal: "Ship the focus classifier",
            app_name: "Cursor",
            window_title: "smoke.rs - Snapback",
            focus_state: "DEEP_FOCUS",
            label: FocusLabel::DeepFocus,
            focus_score: 92.0,
            distraction_risk: 0.12,
            thrash_score: 0.05,
            drift_score: 0.08,
            goal_alignment: 0.95,
            keystroke_count: 18,
            keystroke_rate: 2.4,
            context_switches: 0,
            idle_time_30s: 0.0,
            focus_momentum: 0.92,
            is_browser: false,
            is_ide: true,
            is_communication: false,
            is_entertainment: false,
            is_productivity: true,
            is_pseudo_productive: false,
        },
        SessionArchetype {
            goal: "Write the weekly recap",
            app_name: "Google Chrome",
            window_title: "news video social",
            focus_state: "DISTRACTED",
            label: FocusLabel::Distracted,
            focus_score: 24.0,
            distraction_risk: 0.88,
            thrash_score: 0.82,
            drift_score: 0.74,
            goal_alignment: 0.18,
            keystroke_count: 1,
            keystroke_rate: 0.05,
            context_switches: 4,
            idle_time_30s: 8.0,
            focus_momentum: 0.12,
            is_browser: true,
            is_ide: false,
            is_communication: true,
            is_entertainment: true,
            is_productivity: false,
            is_pseudo_productive: false,
        },
        SessionArchetype {
            goal: "Research design patterns",
            app_name: "Google Chrome",
            window_title: "docs tabs pricing",
            focus_state: "PSEUDO_PRODUCTIVE",
            label: FocusLabel::PseudoProductive,
            focus_score: 48.0,
            distraction_risk: 0.46,
            thrash_score: 0.34,
            drift_score: 0.61,
            goal_alignment: 0.42,
            keystroke_count: 4,
            keystroke_rate: 0.3,
            context_switches: 2,
            idle_time_30s: 1.8,
            focus_momentum: 0.4,
            is_browser: true,
            is_ide: false,
            is_communication: false,
            is_entertainment: false,
            is_productivity: true,
            is_pseudo_productive: true,
        },
        SessionArchetype {
            goal: "Draft the product spec",
            app_name: "Notion",
            window_title: "product spec draft",
            focus_state: "PRODUCTIVE",
            label: FocusLabel::Productive,
            focus_score: 74.0,
            distraction_risk: 0.26,
            thrash_score: 0.12,
            drift_score: 0.18,
            goal_alignment: 0.78,
            keystroke_count: 10,
            keystroke_rate: 1.2,
            context_switches: 1,
            idle_time_30s: 0.5,
            focus_momentum: 0.69,
            is_browser: false,
            is_ide: false,
            is_communication: false,
            is_entertainment: false,
            is_productivity: true,
            is_pseudo_productive: false,
        },
    ]
}

fn unix_timestamp(value: chrono::DateTime<Utc>) -> f64 {
    value.timestamp() as f64 + value.timestamp_subsec_nanos() as f64 / 1_000_000_000.0
}

fn print_report(report: &SmokeReport) {
    println!("SNAPBACK_SMOKE v1");
    match serde_json::to_string_pretty(report) {
        Ok(json) => println!("{json}"),
        Err(err) => {
            println!(
                "{{\"version\":1,\"ok\":false,\"summary\":\"failed to serialize smoke report: {err}\"}}"
            );
        }
    }
}

fn emit_stage(stage: &str, detail: &str) {
    println!("SNAPBACK_SMOKE_STAGE {stage}: {detail}");
}

fn ensure_ort_dylib_path() -> Result<(), SmokeFailure> {
    if !cfg!(windows) {
        return Ok(());
    }

    if std::env::var_os("ORT_DYLIB_PATH")
        .map(PathBuf::from)
        .is_some_and(|path| path.is_file())
    {
        return Ok(());
    }

    for (program, prefix_args) in ort_python_candidates() {
        let mut cmd = Command::new(program);
        cmd.args(prefix_args).arg("-c").arg(
            "import onnxruntime, pathlib; print(pathlib.Path(onnxruntime.__file__).parent / 'capi' / 'onnxruntime.dll')",
        );
        let Ok(output) = cmd.output() else {
            continue;
        };
        if !output.status.success() {
            continue;
        }
        let dll_path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if dll_path.is_empty() || !Path::new(&dll_path).is_file() {
            continue;
        }
        std::env::set_var("ORT_DYLIB_PATH", &dll_path);
        return Ok(());
    }

    Err(SmokeFailure::new(
        "onnx",
        "onnxruntime.dll not found. Install with: pip install onnxruntime, or set ORT_DYLIB_PATH.",
    ))
}

fn ort_python_candidates() -> [(&'static str, &'static [&'static str]); 3] {
    if cfg!(windows) {
        [("py", &["-3"]), ("python3", &[]), ("python", &[])]
    } else {
        [("python3", &[]), ("python", &[]), ("py", &["-3"])]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn seed_smoke_sessions_exports_enough_rows() {
        let dir =
            std::env::temp_dir().join(format!("snapback-smoke-test-{}", uuid::Uuid::new_v4()));
        let storage = Storage::open(dir.clone()).unwrap();

        let stats = seed_smoke_sessions(&storage).unwrap();
        assert_eq!(stats.session_count, SMOKE_SESSION_COUNT);
        assert_eq!(stats.feature_count, MIN_EXPECTED_FEATURE_ROWS);
        assert_eq!(stats.label_count, MIN_EXPECTED_LABEL_ROWS);

        let export = storage
            .export_training_data(&training_deploy::export_dir(&dir), None)
            .unwrap();
        assert!(export.feature_count >= MIN_EXPECTED_FEATURE_ROWS);
        assert!(export.label_count >= MIN_EXPECTED_LABEL_ROWS);
    }

    fn train_result(succeeded: bool, deploy_ready: bool, onnx_exported: bool) -> TrainFromExportResult {
        TrainFromExportResult {
            success: succeeded,
            training_succeeded: succeeded,
            deploy_ready,
            message: "smoke".to_string(),
            onnx_exported,
            metrics: None,
            log_tail: String::new(),
        }
    }

    #[test]
    fn check_export_thresholds_passes_at_minimums_and_fails_below() {
        assert!(check_export_thresholds(MIN_EXPECTED_FEATURE_ROWS, MIN_EXPECTED_LABEL_ROWS).is_ok());

        let feat_err =
            check_export_thresholds(MIN_EXPECTED_FEATURE_ROWS - 1, MIN_EXPECTED_LABEL_ROWS)
                .unwrap_err();
        assert_eq!(feat_err.stage, "export");
        assert!(feat_err.detail.contains("feature rows"), "detail: {}", feat_err.detail);

        let label_err =
            check_export_thresholds(MIN_EXPECTED_FEATURE_ROWS, MIN_EXPECTED_LABEL_ROWS - 1)
                .unwrap_err();
        assert!(label_err.detail.contains("label rows"), "detail: {}", label_err.detail);
    }

    #[test]
    fn check_train_outcome_passes_only_when_fully_deployable() {
        assert!(check_train_outcome(&train_result(true, true, true), true, true).is_ok());
    }

    #[test]
    fn check_train_outcome_names_each_broken_guarantee() {
        let not_trained =
            check_train_outcome(&train_result(false, false, false), true, true).unwrap_err();
        assert!(not_trained.detail.contains("did not succeed"), "detail: {}", not_trained.detail);

        let not_deployable =
            check_train_outcome(&train_result(true, false, false), true, true).unwrap_err();
        assert!(not_deployable.detail.contains("deploy-ready"), "detail: {}", not_deployable.detail);

        let no_model =
            check_train_outcome(&train_result(true, true, true), false, true).unwrap_err();
        assert!(no_model.detail.contains("model.onnx"), "detail: {}", no_model.detail);

        let no_metrics =
            check_train_outcome(&train_result(true, true, true), true, false).unwrap_err();
        assert!(no_metrics.detail.contains("metrics.json"), "detail: {}", no_metrics.detail);
    }

    #[test]
    fn verify_session_gate_passes_and_records_a_check_on_fresh_storage() {
        let dir =
            std::env::temp_dir().join(format!("snapback-smoke-gate-{}", uuid::Uuid::new_v4()));
        let storage = Storage::open(dir.clone()).unwrap();
        let mut report = SmokeReport::new(&dir);

        verify_session_gate(&storage, &mut report).expect("gate should pass on fresh storage");

        assert!(
            report.checks.iter().any(|c| c.stage == "session_gate" && c.ok),
            "expected a passing session_gate check to be recorded",
        );
    }
}
