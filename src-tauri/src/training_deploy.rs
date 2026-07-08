//! In-app training deploy: status, repo path, and Python pipeline spawn.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;

use serde::{Deserialize, Serialize};

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
    let candidates: [(&str, &[&str]); 3] = [
        ("py", &["-3"]),
        ("python3", &[]),
        ("python", &[]),
    ];

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
    pub has_export: bool,
    pub model_onnx_exists: bool,
    pub metrics_exists: bool,
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
    let has_export = feature_count > 0 && label_count > 0;
    let model_onnx_exists = export.join("model.onnx").is_file();
    let metrics_exists = export.join("metrics.json").is_file();
    let repo_path = read_training_repo_path(app_data_dir);

    TrainingDeployStatus {
        export_dir: export.display().to_string(),
        feature_count,
        label_count,
        has_export,
        model_onnx_exists,
        metrics_exists,
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
        "Training failed. Install deps: pip install xgboost onnxmltools onnx (see log)."
            .to_string()
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

    let output = cmd
        .output()
        .map_err(|err| format!("Failed to run training: {err}"))?;

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
        return Ok(build_train_from_export_result(
            false,
            onnx_exported,
            metrics,
            log_tail,
        ));
    }

    Ok(build_train_from_export_result(
        true,
        onnx_exported,
        metrics,
        log_tail,
    ))
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
}
