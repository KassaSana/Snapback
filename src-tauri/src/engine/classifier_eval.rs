//! Evaluate heuristic vs ONNX classifiers against a labeled training CSV.
//!
//! Uses [`Classifier::predict`] end-to-end — including ONNX swap-in (when loaded)
//! and post-model guardrails — so metrics match the live engine in `state.rs`.

use std::collections::HashMap;
use std::path::Path;

use crate::engine::classifier::{Classifier, STATE_LABELS};
use crate::engine::features::FeatureVector;
use crate::types::FocusMode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EvalBackend {
    Heuristic,
    Onnx,
}

#[derive(Debug, Clone)]
pub struct EvalMetrics {
    pub backend: String,
    pub samples: usize,
    pub accuracy: f64,
    pub precision_at_10pct_distracted: f64,
    pub recall_distracted: f64,
}

const TRAINING_COLUMNS: [&str; 31] = [
    "seconds_since_session_start",
    "hour_of_day",
    "day_of_week",
    "minutes_since_last_break",
    "keystroke_count",
    "keystroke_rate",
    "keystroke_interval_mean",
    "keystroke_interval_std",
    "keystroke_interval_trend",
    "mouse_move_count",
    "mouse_distance_pixels",
    "mouse_speed_mean",
    "mouse_speed_std",
    "mouse_acceleration_mean",
    "mouse_click_count",
    "context_switches_30s",
    "context_switches_5min",
    "time_in_current_app",
    "unique_apps_5min",
    "idle_time_30s",
    "idle_event_count_5min",
    "longest_active_stretch_5min",
    "window_title_length",
    "window_title_changed_30s",
    "is_browser",
    "is_ide",
    "is_communication",
    "is_entertainment",
    "is_productivity",
    "focus_momentum",
    "is_pseudo_productive",
];

fn parse_f64(value: &str) -> f64 {
    value.trim().parse().unwrap_or(0.0)
}

fn parse_usize(value: &str) -> usize {
    parse_f64(value).max(0.0) as usize
}

fn parse_bool01(value: &str) -> bool {
    parse_f64(value) >= 0.5
}

fn label_value_to_index(label: i32) -> Option<usize> {
    match label {
        -1 => Some(0),
        0 => Some(1),
        1 => Some(2),
        2 => Some(3),
        _ => None,
    }
}

fn focus_state_to_index(state: &str) -> Option<usize> {
    STATE_LABELS
        .iter()
        .position(|candidate| *candidate == state)
}

fn session_context(session_id: &str) -> (String, String, Option<String>, FocusMode) {
    match session_id {
        "synthetic-deep-focus" => (
            "Cursor".to_string(),
            "main.rs — Snapback".to_string(),
            Some("Ship the feature parity fix".to_string()),
            FocusMode::Deep,
        ),
        "synthetic-distracted" => (
            "Google Chrome".to_string(),
            "YouTube - Rick Astley".to_string(),
            Some("Write the quarterly report".to_string()),
            FocusMode::Normal,
        ),
        "synthetic-drift" => (
            "Google Chrome".to_string(),
            "competitor pricing".to_string(),
            Some("Research competitor pricing".to_string()),
            FocusMode::Normal,
        ),
        "synthetic-productive" => (
            "Notion".to_string(),
            "design doc".to_string(),
            Some("Draft the design doc".to_string()),
            FocusMode::Normal,
        ),
        _ => (String::new(), String::new(), None, FocusMode::Normal),
    }
}

fn row_string(row: &HashMap<String, String>, key: &str) -> Option<String> {
    row.get(key)
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
}

fn feature_from_row(
    row: &HashMap<String, String>,
) -> Option<(FeatureVector, i32, Option<String>, FocusMode)> {
    let label_raw = row.get("label")?.trim();
    if label_raw.is_empty() {
        return None;
    }
    let label: i32 = label_raw.parse().ok()?;
    label_value_to_index(label)?;

    let session_id = row
        .get("label_session_id")
        .or_else(|| row.get("session_id"))
        .map(|s| s.trim().to_string())
        .unwrap_or_default();
    let (fallback_app_name, fallback_window_title, fallback_goal, fallback_focus_mode) =
        session_context(&session_id);
    let app_name = row_string(row, "app_name").unwrap_or(fallback_app_name);
    let window_title = row_string(row, "window_title").unwrap_or(fallback_window_title);
    let goal = row_string(row, "session_goal").or(fallback_goal);
    let focus_mode = row_string(row, "focus_mode")
        .map(|value| FocusMode::from_str(&value))
        .unwrap_or(fallback_focus_mode);

    let timestamp = parse_f64(row.get("timestamp").map(String::as_str).unwrap_or("0"));

    let mut features = FeatureVector {
        timestamp,
        seconds_since_session_start: parse_f64(
            row.get("seconds_since_session_start")
                .map(String::as_str)
                .unwrap_or("0"),
        ) as i64,
        hour_of_day: parse_f64(row.get("hour_of_day").map(String::as_str).unwrap_or("0")) as u32,
        day_of_week: parse_f64(row.get("day_of_week").map(String::as_str).unwrap_or("0")) as u32,
        minutes_since_last_break: parse_f64(
            row.get("minutes_since_last_break")
                .map(String::as_str)
                .unwrap_or("0"),
        ) as i64,
        keystroke_count: parse_usize(
            row.get("keystroke_count")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        keystroke_rate: parse_f64(row.get("keystroke_rate").map(String::as_str).unwrap_or("0")),
        keystroke_interval_mean: parse_f64(
            row.get("keystroke_interval_mean")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        keystroke_interval_std: parse_f64(
            row.get("keystroke_interval_std")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        keystroke_interval_trend: parse_f64(
            row.get("keystroke_interval_trend")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        mouse_move_count: parse_usize(
            row.get("mouse_move_count")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        mouse_distance_pixels: parse_f64(
            row.get("mouse_distance_pixels")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        mouse_speed_mean: parse_f64(
            row.get("mouse_speed_mean")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        mouse_speed_std: parse_f64(
            row.get("mouse_speed_std")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        mouse_acceleration_mean: parse_f64(
            row.get("mouse_acceleration_mean")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        mouse_click_count: parse_usize(
            row.get("mouse_click_count")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        context_switches_30s: parse_usize(
            row.get("context_switches_30s")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        context_switches_5min: parse_usize(
            row.get("context_switches_5min")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        time_in_current_app: parse_f64(
            row.get("time_in_current_app")
                .map(String::as_str)
                .unwrap_or("0"),
        ) as i64,
        unique_apps_5min: parse_usize(
            row.get("unique_apps_5min")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        idle_time_30s: parse_f64(row.get("idle_time_30s").map(String::as_str).unwrap_or("0")),
        idle_event_count_5min: parse_usize(
            row.get("idle_event_count_5min")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        longest_active_stretch_5min: parse_f64(
            row.get("longest_active_stretch_5min")
                .map(String::as_str)
                .unwrap_or("0"),
        ) as i64,
        window_title_length: parse_usize(
            row.get("window_title_length")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        window_title_changed_30s: parse_bool01(
            row.get("window_title_changed_30s")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        app_name,
        window_title,
        is_browser: parse_bool01(row.get("is_browser").map(String::as_str).unwrap_or("0")),
        is_ide: parse_bool01(row.get("is_ide").map(String::as_str).unwrap_or("0")),
        is_communication: parse_bool01(
            row.get("is_communication")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        is_entertainment: parse_bool01(
            row.get("is_entertainment")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        is_productivity: parse_bool01(
            row.get("is_productivity")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
        focus_momentum: parse_f64(row.get("focus_momentum").map(String::as_str).unwrap_or("0")),
        productivity_category: String::new(),
        is_pseudo_productive: parse_bool01(
            row.get("is_pseudo_productive")
                .map(String::as_str)
                .unwrap_or("0"),
        ),
    };

    if features.app_name.is_empty() {
        if features.is_ide {
            features.app_name = "Cursor".to_string();
        } else if features.is_productivity {
            features.app_name = "Notion".to_string();
        } else if features.is_browser || features.is_entertainment {
            features.app_name = "Google Chrome".to_string();
        }
    }

    Some((features, label, goal, focus_mode))
}

fn parse_csv(path: &Path) -> Result<Vec<HashMap<String, String>>, String> {
    let mut reader = csv::ReaderBuilder::new()
        .flexible(true)
        .from_path(path)
        .map_err(|err| format!("failed to open {}: {err}", path.display()))?;
    let headers = reader
        .headers()
        .map_err(|err| format!("failed to read CSV headers from {}: {err}", path.display()))?
        .iter()
        .map(|value| value.to_string())
        .collect::<Vec<_>>();
    let mut rows = Vec::new();
    for record in reader.records() {
        let record = record
            .map_err(|err| format!("failed to parse CSV row in {}: {err}", path.display()))?;
        let mut row = HashMap::new();
        for (header, value) in headers.iter().zip(record.iter()) {
            row.insert(header.clone(), value.to_string());
        }
        rows.push(row);
    }
    Ok(rows)
}

fn precision_at_k_distraction(
    distraction_scores: &[f64],
    labels: &[usize],
    k_fraction: f64,
) -> f64 {
    if distraction_scores.is_empty() {
        return 0.0;
    }
    let k = ((distraction_scores.len() as f64) * k_fraction).ceil() as usize;
    let k = k.max(1).min(distraction_scores.len());
    let mut ranked: Vec<usize> = (0..distraction_scores.len()).collect();
    ranked.sort_by(|a, b| {
        distraction_scores[*a]
            .partial_cmp(&distraction_scores[*b])
            .unwrap_or(std::cmp::Ordering::Equal)
    });
    let top_k = &ranked[ranked.len() - k..];
    let hits = top_k.iter().filter(|&&idx| labels[idx] == 0).count();
    hits as f64 / top_k.len() as f64
}

fn recall_distracted_states(distraction_scores: &[f64], labels: &[usize], threshold: f64) -> f64 {
    let positives: Vec<usize> = labels
        .iter()
        .enumerate()
        .filter_map(|(idx, label)| if *label == 0 { Some(idx) } else { None })
        .collect();
    if positives.is_empty() {
        return 0.0;
    }
    let hits = positives
        .iter()
        .filter(|&&idx| distraction_scores[idx] >= threshold)
        .count();
    hits as f64 / positives.len() as f64
}

pub fn evaluate_labeled_csv(path: &Path, backend: EvalBackend) -> Result<EvalMetrics, String> {
    #[cfg(feature = "onnx")]
    if backend == EvalBackend::Heuristic {
        crate::engine::onnx_model::reset_model_for_tests();
    }

    let rows = parse_csv(path)?;
    let mut classifier = Classifier::new(FocusMode::Normal);
    let rules = Vec::new();

    let mut labels = Vec::new();
    let mut distraction_scores = Vec::new();
    let mut correct = 0usize;

    for row in rows {
        let Some((features, label_value, goal, focus_mode)) = feature_from_row(&row) else {
            continue;
        };
        let Some(expected) = label_value_to_index(label_value) else {
            continue;
        };

        classifier.set_focus_mode(focus_mode);
        let goal_ref = goal.as_deref();
        let scores = classifier.predict(&features, goal_ref, &rules);
        let Some(predicted) = focus_state_to_index(&scores.focus_state) else {
            continue;
        };

        distraction_scores.push(scores.distraction_risk);
        labels.push(expected);
        if predicted == expected {
            correct += 1;
        }
    }

    if labels.is_empty() {
        return Err(format!("no labeled rows found in {}", path.display()));
    }

    let precision_at_10pct_distracted =
        precision_at_k_distraction(&distraction_scores, &labels, 0.1);
    let recall_distracted = recall_distracted_states(&distraction_scores, &labels, 0.7);

    let backend_name = match backend {
        EvalBackend::Heuristic => "heuristic",
        EvalBackend::Onnx => "onnx",
    };

    Ok(EvalMetrics {
        backend: backend_name.to_string(),
        samples: labels.len(),
        accuracy: correct as f64 / labels.len() as f64,
        precision_at_10pct_distracted,
        recall_distracted,
    })
}

pub fn run_classifier_eval_cli(args: &[String]) -> i32 {
    let path = args
        .iter()
        .position(|a| a == "--classifier-eval")
        .and_then(|idx| args.get(idx + 1))
        .map(Path::new);

    let Some(path) = path else {
        eprintln!(
            "usage: --classifier-eval <labeled.csv> [--backend heuristic|onnx] [--model-onnx path]"
        );
        return 1;
    };

    let backend = args
        .iter()
        .position(|a| a == "--backend")
        .and_then(|idx| args.get(idx + 1))
        .map(|s| s.as_str())
        .unwrap_or("heuristic");

    let backend = match backend {
        "heuristic" => EvalBackend::Heuristic,
        "onnx" => EvalBackend::Onnx,
        other => {
            eprintln!("unknown backend: {other}");
            return 1;
        }
    };

    if backend == EvalBackend::Onnx {
        #[cfg(not(feature = "onnx"))]
        {
            eprintln!("ONNX backend requires --features onnx");
            return 1;
        }
        #[cfg(feature = "onnx")]
        {
            let model_path = args
                .iter()
                .position(|a| a == "--model-onnx")
                .and_then(|idx| args.get(idx + 1))
                .map(Path::new)
                .unwrap_or_else(|| Path::new("data/model.onnx"));

            #[cfg(feature = "onnx")]
            crate::engine::onnx_model::reset_model_for_tests();

            if let Err(err) = crate::engine::onnx_model::init(model_path) {
                eprintln!("failed to load {}: {err}", model_path.display());
                return 1;
            }
        }
    } else {
        #[cfg(feature = "onnx")]
        crate::engine::onnx_model::reset_model_for_tests();
    }

    match evaluate_labeled_csv(path, backend) {
        Ok(metrics) => {
            println!("SNAPBACK_CLASSIFIER_EVAL v1");
            println!("backend={}", metrics.backend);
            println!("samples={}", metrics.samples);
            println!("accuracy={:.4}", metrics.accuracy);
            println!(
                "precision_at_10pct_distracted={:.4}",
                metrics.precision_at_10pct_distracted
            );
            println!("recall_distracted={:.4}", metrics.recall_distracted);
            0
        }
        Err(err) => {
            eprintln!("{err}");
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn label_and_state_indices_align() {
        assert_eq!(label_value_to_index(-1), Some(0));
        assert_eq!(focus_state_to_index("DISTRACTED"), Some(0));
        assert_eq!(focus_state_to_index("DEEP_FOCUS"), Some(3));
    }

    #[test]
    fn feature_from_row_prefers_exported_context_and_focus_mode() {
        let row = HashMap::from([
            ("label".to_string(), "2".to_string()),
            ("timestamp".to_string(), "1700000000".to_string()),
            ("keystroke_count".to_string(), "8".to_string()),
            ("app_name".to_string(), "Cursor".to_string()),
            (
                "window_title".to_string(),
                "state.rs — Snapback".to_string(),
            ),
            ("session_goal".to_string(), "Ship eval parity".to_string()),
            ("focus_mode".to_string(), "deep".to_string()),
        ]);

        let (features, label, goal, focus_mode) = feature_from_row(&row).expect("row should parse");
        assert_eq!(label, 2);
        assert_eq!(features.app_name, "Cursor");
        assert_eq!(features.window_title, "state.rs — Snapback");
        assert_eq!(goal.as_deref(), Some("Ship eval parity"));
        assert_eq!(focus_mode, FocusMode::Deep);
    }

    #[test]
    fn parse_csv_handles_quoted_notes_and_context_columns() {
        let dir =
            std::env::temp_dir().join(format!("classifier_eval_csv_{}", uuid::Uuid::new_v4()));
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("labeled.csv");
        fs::write(
            &path,
            "timestamp,label,label_notes,app_name,window_title,session_goal,focus_mode\n\
1700000000,2,\"hello, \"\"world\"\"\nline2\",Cursor,\"state.rs, Snapback\",Ship eval parity,deep\n",
        )
        .unwrap();

        let rows = parse_csv(&path).expect("csv should parse");
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].get("app_name").map(String::as_str), Some("Cursor"));
        assert_eq!(
            rows[0].get("window_title").map(String::as_str),
            Some("state.rs, Snapback")
        );
        assert_eq!(
            rows[0].get("label_notes").map(String::as_str),
            Some("hello, \"world\"\nline2")
        );

        let _ = fs::remove_file(&path);
        let _ = fs::remove_dir(&dir);
    }
}
