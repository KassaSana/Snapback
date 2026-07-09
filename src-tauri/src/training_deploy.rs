//! In-app training deploy: status, repo path, and Python pipeline spawn.

use std::collections::HashMap;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitStatus, Stdio};
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};

/// Training runs an external Python pipeline we don't control; cap how long
/// we'll wait so a stalled `pip install` or a runaway script can't block the
/// app forever. 10 minutes comfortably covers a real training run on the
/// synthetic/dev-sized datasets this app produces.
const TRAINING_TIMEOUT: Duration = Duration::from_secs(600);

#[derive(Debug)]
struct TrainingOutput {
    status: ExitStatus,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
}

/// Run `cmd` to completion, or kill it and return an error once `timeout`
/// elapses. Stdout/stderr are drained on background threads so a chatty
/// process can't deadlock on a full pipe buffer while we're polling.
fn run_with_timeout(mut cmd: Command, timeout: Duration) -> Result<TrainingOutput, String> {
    cmd.stdout(Stdio::piped()).stderr(Stdio::piped());
    let mut child = cmd
        .spawn()
        .map_err(|err| format!("Failed to run training: {err}"))?;

    let mut stdout_pipe = child.stdout.take().expect("stdout was piped");
    let mut stderr_pipe = child.stderr.take().expect("stderr was piped");
    let stdout_handle = std::thread::spawn(move || {
        let mut buf = Vec::new();
        let _ = stdout_pipe.read_to_end(&mut buf);
        buf
    });
    let stderr_handle = std::thread::spawn(move || {
        let mut buf = Vec::new();
        let _ = stderr_pipe.read_to_end(&mut buf);
        buf
    });

    let deadline = Instant::now() + timeout;
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break status,
            Ok(None) => {
                if Instant::now() >= deadline {
                    let _ = child.kill();
                    let _ = child.wait();
                    return Err(format!(
                        "Training timed out after {}s and was stopped.",
                        timeout.as_secs()
                    ));
                }
                std::thread::sleep(Duration::from_millis(200));
            }
            Err(err) => return Err(format!("Failed to poll training process: {err}")),
        }
    };

    Ok(TrainingOutput {
        status,
        stdout: stdout_handle.join().unwrap_or_default(),
        stderr: stderr_handle.join().unwrap_or_default(),
    })
}

pub fn export_dir(app_data_dir: &Path) -> PathBuf {
    app_data_dir.join("exports").join("training")
}

pub fn is_training_repo(path: &Path) -> bool {
    path.join("ml").join("pipeline_cli.py").is_file()
}

pub fn read_training_repo_path(app_data_dir: &Path) -> Option<PathBuf> {
    if let Ok(env_path) = std::env::var("SNAPBACK_REPO") {
        let path = PathBuf::from(env_path);
        if is_training_repo(&path) {
            return Some(path);
        }
    }

    let config_path = app_data_dir.join("training_repo.txt");
    if let Ok(content) = std::fs::read_to_string(&config_path) {
        let trimmed = content.trim();
        if !trimmed.is_empty() {
            let path = PathBuf::from(trimmed);
            if is_training_repo(&path) {
                return Some(path);
            }
        }
    }

    None
}

pub fn write_training_repo_path(app_data_dir: &Path, repo_path: &Path) -> Result<(), String> {
    if !is_training_repo(repo_path) {
        return Err(format!(
            "Not a Snapback repo (missing ml/pipeline_cli.py): {}",
            repo_path.display()
        ));
    }
    std::fs::create_dir_all(app_data_dir).map_err(|e| e.to_string())?;
    std::fs::write(
        app_data_dir.join("training_repo.txt"),
        repo_path.display().to_string(),
    )
    .map_err(|e| e.to_string())
}

struct PythonCommand {
    program: String,
    prefix_args: Vec<String>,
}

fn find_python() -> Option<PythonCommand> {
    let candidates: [(&str, &[&str]); 3] = [("py", &["-3"]), ("python3", &[]), ("python", &[])];

    for (program, prefix) in candidates {
        let mut cmd = Command::new(program);
        cmd.args(prefix).arg("--version");
        if cmd
            .output()
            .ok()
            .filter(|output| output.status.success())
            .is_some()
        {
            return Some(PythonCommand {
                program: program.to_string(),
                prefix_args: prefix.iter().map(|arg| (*arg).to_string()).collect(),
            });
        }
    }
    None
}

fn count_csv_rows(path: &Path) -> usize {
    let Ok(content) = std::fs::read_to_string(path) else {
        return 0;
    };
    content.lines().count().saturating_sub(1)
}

fn count_label_breakdown(path: &Path) -> HashMap<String, usize> {
    let mut counts = HashMap::new();
    let Ok(mut reader) = csv::Reader::from_path(path) else {
        return counts;
    };

    for row in reader.deserialize::<HashMap<String, String>>().flatten() {
        let Some(label_raw) = row.get("label").map(|value| value.trim()) else {
            continue;
        };
        let label_name = match label_raw {
            "-1" | "DISTRACTED" => "DISTRACTED",
            "0" | "PSEUDO_PRODUCTIVE" => "PSEUDO_PRODUCTIVE",
            "1" | "PRODUCTIVE" => "PRODUCTIVE",
            "2" | "DEEP_FOCUS" => "DEEP_FOCUS",
            _ => continue,
        };
        *counts.entry(label_name.to_string()).or_insert(0) += 1;
    }

    counts
}

fn sync_trained_model_to_app_dir(app_data_dir: &Path, export_dir: &Path) -> Result<bool, String> {
    let export_model = export_dir.join("model.onnx");
    if !export_model.is_file() {
        return Ok(false);
    }

    std::fs::create_dir_all(app_data_dir).map_err(|err| {
        format!(
            "Could not prepare app data directory {}: {err}",
            app_data_dir.display()
        )
    })?;

    let live_model = app_data_dir.join("model.onnx");
    std::fs::copy(&export_model, &live_model).map_err(|err| {
        format!(
            "Could not copy trained model from {} to {}: {err}",
            export_model.display(),
            live_model.display()
        )
    })?;
    Ok(true)
}

pub fn build_pipeline_command(output_dir: &Path) -> String {
    let quoted = format!("\"{}\"", output_dir.display());
    let python = if cfg!(windows) { "py -3" } else { "python3" };
    format!(
        "# Run from your Snapback repo root:\n{python} -m ml.pipeline_cli \\\n  --output-dir {quoted} \\\n  --skip-export"
    )
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrainingDeployStatus {
    pub export_dir: String,
    pub feature_count: usize,
    pub label_count: usize,
    pub label_breakdown: HashMap<String, usize>,
    pub has_export: bool,
    pub model_onnx_exists: bool,
    pub metrics_exists: bool,
    pub metrics: Option<HashMap<String, f64>>,
    pub python_available: bool,
    pub repo_path: Option<String>,
    pub repo_configured: bool,
    pub pipeline_command: String,
}

pub fn training_deploy_status(app_data_dir: &Path) -> TrainingDeployStatus {
    let export = export_dir(app_data_dir);
    let features_path = export.join("features.csv");
    let labels_path = export.join("labels.csv");
    let feature_count = count_csv_rows(&features_path);
    let label_count = count_csv_rows(&labels_path);
    let label_breakdown = count_label_breakdown(&labels_path);
    let has_export = feature_count > 0 && label_count > 0;
    let model_onnx_exists = export.join("model.onnx").is_file();
    let metrics_path = export.join("metrics.json");
    let metrics_exists = metrics_path.is_file();
    let metrics = metrics_exists
        .then(|| parse_metrics_json(&metrics_path))
        .flatten();
    let repo_path = read_training_repo_path(app_data_dir);

    TrainingDeployStatus {
        export_dir: export.display().to_string(),
        feature_count,
        label_count,
        label_breakdown,
        has_export,
        model_onnx_exists,
        metrics_exists,
        metrics,
        python_available: find_python().is_some(),
        repo_path: repo_path.as_ref().map(|path| path.display().to_string()),
        repo_configured: repo_path.is_some(),
        pipeline_command: build_pipeline_command(&export),
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrainFromExportResult {
    pub success: bool,
    pub training_succeeded: bool,
    pub deploy_ready: bool,
    pub message: String,
    pub onnx_exported: bool,
    pub metrics: Option<HashMap<String, f64>>,
    pub log_tail: String,
}

fn build_train_from_export_result(
    training_succeeded: bool,
    onnx_exported: bool,
    metrics: Option<HashMap<String, f64>>,
    log_tail: String,
) -> TrainFromExportResult {
    let deploy_ready = training_succeeded && onnx_exported;
    let message = if !training_succeeded {
        "Training failed. Install deps: pip install xgboost onnxmltools onnx (see log).".to_string()
    } else if deploy_ready {
        "Training complete — model.onnx is ready. Reload model to activate.".to_string()
    } else {
        "Training finished but ONNX export was skipped (majority stub or missing export deps)."
            .to_string()
    };

    TrainFromExportResult {
        success: deploy_ready,
        training_succeeded,
        deploy_ready,
        message,
        onnx_exported,
        metrics,
        log_tail,
    }
}

fn build_training_failure_message(exit_code: Option<i32>, log_tail: &str) -> String {
    let normalized = log_tail.to_lowercase();
    if exit_code == Some(2)
        || normalized.contains("majority-classifier stub")
        || normalized.contains("majority stub")
    {
        return "Training stopped because the current data only produced a majority-classifier stub. Capture more labeled sessions, then train again.".to_string();
    }
    if normalized.contains("xgboost is not installed")
        || normalized.contains("install onnx export deps")
        || normalized.contains("python not found")
    {
        return "Training failed. Install deps: pip install xgboost onnxmltools onnx (see log)."
            .to_string();
    }
    "Training failed. Check the training log for details.".to_string()
}

pub fn train_from_export(app_data_dir: &Path) -> Result<TrainFromExportResult, String> {
    let status = training_deploy_status(app_data_dir);
    if !status.has_export {
        return Err(
            "Export training data first (need features.csv and labels.csv in your export folder)."
                .to_string(),
        );
    }

    let repo_path = read_training_repo_path(app_data_dir).ok_or_else(|| {
        "Snapback repo path not set. Enter your repo folder below or set SNAPBACK_REPO.".to_string()
    })?;

    let python = find_python().ok_or_else(|| {
        "Python not found. Install Python 3 and: pip install xgboost onnxmltools onnx".to_string()
    })?;

    let export = export_dir(app_data_dir);
    let mut cmd = Command::new(&python.program);
    cmd.current_dir(&repo_path)
        .args(&python.prefix_args)
        .arg("-m")
        .arg("ml.pipeline_cli")
        .arg("--output-dir")
        .arg(&export)
        .arg("--skip-export");

    let output = run_with_timeout(cmd, TRAINING_TIMEOUT)?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    let combined = format!("{stdout}\n{stderr}");
    let log_tail: String = combined
        .lines()
        .rev()
        .take(12)
        .collect::<Vec<_>>()
        .into_iter()
        .rev()
        .collect::<Vec<_>>()
        .join("\n");

    let onnx_exported = export.join("model.onnx").is_file();
    let metrics_path = export.join("metrics.json");
    let metrics = metrics_path
        .is_file()
        .then(|| parse_metrics_json(&metrics_path))
        .flatten();

    if !output.status.success() {
        let mut result = build_train_from_export_result(false, onnx_exported, metrics, log_tail);
        result.message = build_training_failure_message(output.status.code(), &result.log_tail);
        return Ok(result);
    }

    let model_sync_error = if onnx_exported {
        sync_trained_model_to_app_dir(app_data_dir, &export).err()
    } else {
        None
    };

    let mut result = build_train_from_export_result(true, onnx_exported, metrics, log_tail);
    if let Some(err) = model_sync_error {
        result.message = format!("{} Warning: {err}", result.message);
    }

    Ok(result)
}

fn parse_metrics_json(path: &Path) -> Option<HashMap<String, f64>> {
    let content = std::fs::read_to_string(path).ok()?;
    let parsed: serde_json::Value = serde_json::from_str(&content).ok()?;
    let object = parsed.as_object()?;
    let mut metrics = HashMap::new();
    for (key, value) in object {
        if let Some(number) = value.as_f64() {
            metrics.insert(key.clone(), number);
        }
    }
    Some(metrics)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn run_with_timeout_captures_output_of_completed_process() {
        let mut cmd = if cfg!(windows) {
            let mut c = Command::new("cmd");
            c.args(["/C", "echo hello"]);
            c
        } else {
            let mut c = Command::new("sh");
            c.args(["-c", "echo hello"]);
            c
        };
        let output = run_with_timeout(cmd, Duration::from_secs(5)).unwrap();
        assert!(output.status.success());
        assert!(String::from_utf8_lossy(&output.stdout).contains("hello"));
    }

    #[test]
    fn run_with_timeout_kills_process_that_outlives_deadline() {
        // A portable "sleep 6s" that doesn't need a console/stdin, unlike
        // Windows' `timeout` command.
        let mut cmd = if cfg!(windows) {
            let mut c = Command::new("ping");
            c.args(["-n", "7", "127.0.0.1"]);
            c
        } else {
            let mut c = Command::new("sleep");
            c.arg("6");
            c
        };
        let result = run_with_timeout(cmd, Duration::from_millis(200));
        let err = result.expect_err("expected a timeout error");
        assert!(err.contains("timed out"), "unexpected error: {err}");
    }

    #[test]
    fn is_training_repo_checks_pipeline_cli() {
        let temp = std::env::temp_dir().join(format!("snapback-repo-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&temp);
        fs::create_dir_all(temp.join("ml")).unwrap();
        assert!(!is_training_repo(&temp));
        fs::write(temp.join("ml").join("pipeline_cli.py"), "# stub\n").unwrap();
        assert!(is_training_repo(&temp));
        let _ = fs::remove_dir_all(&temp);
    }

    #[test]
    fn count_csv_rows_skips_header() {
        let temp = std::env::temp_dir().join(format!("snapback-csv-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&temp);
        fs::create_dir_all(&temp).unwrap();
        let path = temp.join("rows.csv");
        fs::write(&path, "a,b\n1,2\n3,4\n").unwrap();
        assert_eq!(count_csv_rows(&path), 2);
        let _ = fs::remove_dir_all(&temp);
    }

    #[test]
    fn count_label_breakdown_reads_exported_labels() {
        let temp =
            std::env::temp_dir().join(format!("snapback-label-breakdown-{}", uuid::Uuid::new_v4()));
        let _ = fs::remove_dir_all(&temp);
        fs::create_dir_all(&temp).unwrap();
        let path = temp.join("labels.csv");
        fs::write(
            &path,
            "timestamp,label,source,session_id,notes\n\
1,-1,manual,s1,\n\
2,1,manual,s1,\n\
3,1,manual,s1,\n\
4,2,manual,s1,\n",
        )
        .unwrap();

        let counts = count_label_breakdown(&path);
        assert_eq!(counts.get("DISTRACTED"), Some(&1));
        assert_eq!(counts.get("PRODUCTIVE"), Some(&2));
        assert_eq!(counts.get("DEEP_FOCUS"), Some(&1));
        let _ = fs::remove_dir_all(&temp);
    }

    #[test]
    fn build_pipeline_command_quotes_output_dir() {
        let command = build_pipeline_command(Path::new(r"C:\app data\exports\training"));
        assert!(command.contains("--output-dir"));
        assert!(command.contains(r#""C:\app data\exports\training""#));
    }

    #[test]
    fn build_train_result_marks_deploy_ready_only_when_model_exists() {
        let result = build_train_from_export_result(true, false, None, String::new());
        assert!(result.training_succeeded);
        assert!(!result.deploy_ready);
        assert!(!result.success);
        assert_eq!(
            result.message,
            "Training finished but ONNX export was skipped (majority stub or missing export deps)."
        );
    }

    #[test]
    fn build_train_result_reports_success_only_for_deployable_model() {
        let result = build_train_from_export_result(true, true, None, String::new());
        assert!(result.training_succeeded);
        assert!(result.deploy_ready);
        assert!(result.success);
        assert_eq!(
            result.message,
            "Training complete — model.onnx is ready. Reload model to activate."
        );
    }

    #[test]
    fn build_training_failure_message_detects_majority_stub_exit() {
        let message = build_training_failure_message(
            Some(2),
            "Training stopped: the dataset only produced a majority-classifier stub.",
        );
        assert!(message.contains("majority-classifier stub"));
        assert!(message.contains("Capture more labeled sessions"));
    }

    #[test]
    fn build_training_failure_message_detects_missing_deps() {
        let message = build_training_failure_message(
            Some(1),
            "Install ONNX export deps: pip install xgboost onnx onnxmltools",
        );
        assert_eq!(
            message,
            "Training failed. Install deps: pip install xgboost onnxmltools onnx (see log)."
        );
    }

    #[test]
    fn sync_trained_model_copies_export_into_app_data_dir() {
        let temp =
            std::env::temp_dir().join(format!("snapback-model-sync-test-{}", uuid::Uuid::new_v4()));
        let export = temp.join("exports").join("training");
        let app_data = temp.join("app-data");
        fs::create_dir_all(&export).unwrap();
        fs::write(export.join("model.onnx"), b"onnx-bytes").unwrap();

        let copied = sync_trained_model_to_app_dir(&app_data, &export).unwrap();

        assert!(copied);
        assert_eq!(
            fs::read(app_data.join("model.onnx")).unwrap(),
            b"onnx-bytes"
        );
        let _ = fs::remove_dir_all(&temp);
    }

    #[test]
    fn sync_trained_model_skips_when_export_missing() {
        let temp = std::env::temp_dir().join(format!(
            "snapback-model-sync-missing-{}",
            uuid::Uuid::new_v4()
        ));
        let export = temp.join("exports").join("training");
        let app_data = temp.join("app-data");
        fs::create_dir_all(&export).unwrap();

        let copied = sync_trained_model_to_app_dir(&app_data, &export).unwrap();

        assert!(!copied);
        assert!(!app_data.join("model.onnx").exists());
        let _ = fs::remove_dir_all(&temp);
    }
}
