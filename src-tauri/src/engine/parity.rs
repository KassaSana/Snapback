//! Replay shared JSON scenarios through the Rust feature extractor.

use std::collections::HashMap;
use std::fs;
use std::path::Path;

use serde::Deserialize;

use crate::engine::features::{FeatureExtractor, FeatureVector};
use crate::types::{AppRuleRecord, CaptureEvent, EventType};

#[derive(Debug, Deserialize)]
struct ScenarioFile {
    scenarios: Vec<Scenario>,
}

#[derive(Debug, Deserialize)]
struct Scenario {
    name: String,
    base_time: f64,
    events: Vec<ScenarioEvent>,
    #[serde(default)]
    expect: HashMap<String, serde_json::Value>,
}

#[derive(Debug, Deserialize)]
struct ScenarioEvent {
    offset_secs: f64,
    #[serde(rename = "type")]
    event_type: String,
    app: String,
    title: String,
    #[serde(default)]
    mouse_speed: u32,
    #[serde(default)]
    idle_duration_ms: u32,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct ScenarioResult {
    pub name: String,
    pub features: HashMap<String, f64>,
}

fn parse_event_type(raw: &str) -> Option<EventType> {
    match raw {
        "key_press" => Some(EventType::KeyPress),
        "key_release" => Some(EventType::KeyRelease),
        "mouse_move" => Some(EventType::MouseMove),
        "mouse_click" => Some(EventType::MouseClick),
        "window_focus_change" => Some(EventType::WindowFocusChange),
        "window_title_change" => Some(EventType::WindowTitleChange),
        "idle_start" => Some(EventType::IdleStart),
        "idle_end" => Some(EventType::IdleEnd),
        _ => None,
    }
}

pub fn training_column_values(features: &FeatureVector) -> HashMap<String, f64> {
    HashMap::from([
        ("timestamp".to_string(), features.timestamp),
        (
            "seconds_since_session_start".to_string(),
            features.seconds_since_session_start as f64,
        ),
        ("hour_of_day".to_string(), features.hour_of_day as f64),
        ("day_of_week".to_string(), features.day_of_week as f64),
        (
            "minutes_since_last_break".to_string(),
            features.minutes_since_last_break as f64,
        ),
        (
            "keystroke_count".to_string(),
            features.keystroke_count as f64,
        ),
        ("keystroke_rate".to_string(), features.keystroke_rate),
        (
            "keystroke_interval_mean".to_string(),
            features.keystroke_interval_mean,
        ),
        (
            "keystroke_interval_std".to_string(),
            features.keystroke_interval_std,
        ),
        (
            "keystroke_interval_trend".to_string(),
            features.keystroke_interval_trend,
        ),
        (
            "mouse_move_count".to_string(),
            features.mouse_move_count as f64,
        ),
        (
            "mouse_distance_pixels".to_string(),
            features.mouse_distance_pixels,
        ),
        ("mouse_speed_mean".to_string(), features.mouse_speed_mean),
        ("mouse_speed_std".to_string(), features.mouse_speed_std),
        (
            "mouse_acceleration_mean".to_string(),
            features.mouse_acceleration_mean,
        ),
        (
            "mouse_click_count".to_string(),
            features.mouse_click_count as f64,
        ),
        (
            "context_switches_30s".to_string(),
            features.context_switches_30s as f64,
        ),
        (
            "context_switches_5min".to_string(),
            features.context_switches_5min as f64,
        ),
        (
            "time_in_current_app".to_string(),
            features.time_in_current_app as f64,
        ),
        (
            "unique_apps_5min".to_string(),
            features.unique_apps_5min as f64,
        ),
        ("idle_time_30s".to_string(), features.idle_time_30s),
        (
            "idle_event_count_5min".to_string(),
            features.idle_event_count_5min as f64,
        ),
        (
            "longest_active_stretch_5min".to_string(),
            features.longest_active_stretch_5min as f64,
        ),
        (
            "window_title_length".to_string(),
            features.window_title_length as f64,
        ),
        (
            "window_title_changed_30s".to_string(),
            if features.window_title_changed_30s {
                1.0
            } else {
                0.0
            },
        ),
        (
            "is_browser".to_string(),
            if features.is_browser { 1.0 } else { 0.0 },
        ),
        (
            "is_ide".to_string(),
            if features.is_ide { 1.0 } else { 0.0 },
        ),
        (
            "is_communication".to_string(),
            if features.is_communication { 1.0 } else { 0.0 },
        ),
        (
            "is_entertainment".to_string(),
            if features.is_entertainment { 1.0 } else { 0.0 },
        ),
        (
            "is_productivity".to_string(),
            if features.is_productivity { 1.0 } else { 0.0 },
        ),
        ("focus_momentum".to_string(), features.focus_momentum),
        (
            "is_pseudo_productive".to_string(),
            if features.is_pseudo_productive {
                1.0
            } else {
                0.0
            },
        ),
    ])
}

pub fn replay_scenario(scenario: &Scenario, rules: &[AppRuleRecord]) -> FeatureVector {
    let mut extractor = FeatureExtractor::new();

    for event in &scenario.events {
        let Some(event_type) = parse_event_type(&event.event_type) else {
            continue;
        };
        let timestamp_secs = scenario.base_time + event.offset_secs;
        let capture = CaptureEvent {
            event_type,
            timestamp_secs,
            app_name: event.app.clone(),
            window_title: event.title.clone(),
            mouse_x: 0,
            mouse_y: 0,
            mouse_speed: event.mouse_speed,
            idle_duration_ms: event.idle_duration_ms,
        };
        extractor.update(&capture, rules);
    }

    let now = scenario
        .events
        .last()
        .map(|e| scenario.base_time + e.offset_secs)
        .unwrap_or(scenario.base_time);
    extractor.extract_features(now, rules)
}

pub fn run_scenarios(path: &Path, rules: &[AppRuleRecord]) -> Result<Vec<ScenarioResult>, String> {
    let raw = fs::read_to_string(path).map_err(|e| e.to_string())?;
    let file: ScenarioFile = serde_json::from_str(&raw).map_err(|e| e.to_string())?;

    Ok(file
        .scenarios
        .iter()
        .map(|scenario| {
            let features = replay_scenario(scenario, rules);
            ScenarioResult {
                name: scenario.name.clone(),
                features: training_column_values(&features),
            }
        })
        .collect())
}

pub fn check_expectations(
    scenario: &Scenario,
    features: &HashMap<String, f64>,
) -> Result<(), String> {
    for (key, expected) in &scenario.expect {
        if key.ends_with("_min") {
            let base = key.trim_end_matches("_min");
            let min = expected
                .as_f64()
                .ok_or_else(|| format!("{}: expected number for {key}", scenario.name))?;
            let actual = features
                .get(base)
                .ok_or_else(|| format!("missing feature column '{base}'"))?;
            if *actual < min {
                return Err(format!("{}.{base}: {actual} < min {min}", scenario.name));
            }
            continue;
        }

        let actual = features
            .get(key)
            .ok_or_else(|| format!("missing feature column '{key}'"))?;

        if let Some(min) = expected.get("min").and_then(|v| v.as_f64()) {
            if *actual < min {
                return Err(format!("{}.{key}: {actual} < min {min}", scenario.name));
            }
            continue;
        }

        let expected_num = expected
            .as_f64()
            .ok_or_else(|| format!("{}: expected numeric value for {key}", scenario.name))?;
        if (*actual - expected_num).abs() > 1e-6 {
            return Err(format!(
                "{}.{key}: expected {expected_num}, got {actual}",
                scenario.name
            ));
        }
    }
    Ok(())
}

pub fn run_feature_parity(path: &Path) -> i32 {
    let raw = match fs::read_to_string(path) {
        Ok(raw) => raw,
        Err(err) => {
            eprintln!("failed to read scenarios: {err}");
            return 1;
        }
    };

    let file: ScenarioFile = match serde_json::from_str(&raw) {
        Ok(file) => file,
        Err(err) => {
            eprintln!("invalid scenarios json: {err}");
            return 1;
        }
    };

    let rules: Vec<AppRuleRecord> = Vec::new();
    let mut failed = false;

    for scenario in &file.scenarios {
        let features = training_column_values(&replay_scenario(scenario, &rules));
        if let Err(err) = check_expectations(scenario, &features) {
            eprintln!("FAIL: {err}");
            failed = true;
            continue;
        }
        println!("PASS: {}", scenario.name);
    }

    if failed {
        1
    } else {
        0
    }
}

pub fn export_feature_parity_json(path: &Path, rules: &[AppRuleRecord]) -> Result<String, String> {
    let results = run_scenarios(path, rules)?;
    serde_json::to_string_pretty(&results).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn scenarios_path() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../fixtures/feature_parity/scenarios.json")
    }

    #[test]
    fn shared_scenarios_match_expectations() {
        let path = scenarios_path();
        assert!(path.exists(), "missing {}", path.display());

        let raw = fs::read_to_string(&path).unwrap();
        let file: ScenarioFile = serde_json::from_str(&raw).unwrap();
        let rules = Vec::new();

        for scenario in &file.scenarios {
            let features = training_column_values(&replay_scenario(scenario, &rules));
            check_expectations(scenario, &features)
                .unwrap_or_else(|err| panic!("scenario {} failed: {err}", scenario.name));
        }
    }

    fn scenario_with_expect(expect: HashMap<String, serde_json::Value>) -> Scenario {
        Scenario {
            name: "test-scenario".to_string(),
            base_time: 0.0,
            events: Vec::new(),
            expect,
        }
    }

    // check_expectations has three comparison modes (exact match, "_min"
    // suffix, and {"min": ...} object) plus several error paths. The only
    // existing test runs it against fixture scenarios that are curated to
    // pass, so none of the failure branches below had ever executed.

    #[test]
    fn exact_match_passes_within_tolerance() {
        let scenario = scenario_with_expect(HashMap::from([(
            "keystroke_rate".to_string(),
            serde_json::json!(5.0),
        )]));
        let features = HashMap::from([("keystroke_rate".to_string(), 5.0000001)]);
        assert!(check_expectations(&scenario, &features).is_ok());
    }

    #[test]
    fn exact_match_fails_outside_tolerance() {
        let scenario = scenario_with_expect(HashMap::from([(
            "keystroke_rate".to_string(),
            serde_json::json!(5.0),
        )]));
        let features = HashMap::from([("keystroke_rate".to_string(), 6.0)]);
        let err = check_expectations(&scenario, &features).unwrap_err();
        assert!(err.contains("expected 5"), "unexpected message: {err}");
        assert!(err.contains("got 6"), "unexpected message: {err}");
    }

    #[test]
    fn exact_match_fails_on_missing_column() {
        let scenario = scenario_with_expect(HashMap::from([(
            "keystroke_rate".to_string(),
            serde_json::json!(5.0),
        )]));
        let err = check_expectations(&scenario, &HashMap::new()).unwrap_err();
        assert!(err.contains("missing feature column 'keystroke_rate'"));
    }

    #[test]
    fn exact_match_fails_on_non_numeric_expected_value() {
        let scenario = scenario_with_expect(HashMap::from([(
            "keystroke_rate".to_string(),
            serde_json::json!("fast"),
        )]));
        let features = HashMap::from([("keystroke_rate".to_string(), 5.0)]);
        let err = check_expectations(&scenario, &features).unwrap_err();
        assert!(err.contains("expected numeric value"));
    }

    #[test]
    fn min_suffix_passes_at_and_above_threshold() {
        let scenario = scenario_with_expect(HashMap::from([(
            "mouse_speed_mean_min".to_string(),
            serde_json::json!(10.0),
        )]));
        // Exactly at the threshold must pass — the comparison is `< min`,
        // not `<= min`.
        let at_threshold = HashMap::from([("mouse_speed_mean".to_string(), 10.0)]);
        assert!(check_expectations(&scenario, &at_threshold).is_ok());

        let above_threshold = HashMap::from([("mouse_speed_mean".to_string(), 10.5)]);
        assert!(check_expectations(&scenario, &above_threshold).is_ok());
    }

    #[test]
    fn min_suffix_fails_below_threshold() {
        let scenario = scenario_with_expect(HashMap::from([(
            "mouse_speed_mean_min".to_string(),
            serde_json::json!(10.0),
        )]));
        let features = HashMap::from([("mouse_speed_mean".to_string(), 9.9)]);
        let err = check_expectations(&scenario, &features).unwrap_err();
        assert!(err.contains("9.9 < min 10"), "unexpected message: {err}");
    }

    #[test]
    fn min_suffix_fails_on_missing_base_column() {
        let scenario = scenario_with_expect(HashMap::from([(
            "mouse_speed_mean_min".to_string(),
            serde_json::json!(10.0),
        )]));
        let err = check_expectations(&scenario, &HashMap::new()).unwrap_err();
        assert!(err.contains("missing feature column 'mouse_speed_mean'"));
    }

    #[test]
    fn min_suffix_fails_on_non_numeric_expected_value() {
        let scenario = scenario_with_expect(HashMap::from([(
            "mouse_speed_mean_min".to_string(),
            serde_json::json!("fast"),
        )]));
        let features = HashMap::from([("mouse_speed_mean".to_string(), 10.0)]);
        let err = check_expectations(&scenario, &features).unwrap_err();
        assert!(err.contains("expected number for mouse_speed_mean_min"));
    }

    #[test]
    fn min_object_shape_passes_at_threshold_and_fails_below_it() {
        let scenario = scenario_with_expect(HashMap::from([(
            "focus_momentum".to_string(),
            serde_json::json!({ "min": 0.5 }),
        )]));
        let passing = HashMap::from([("focus_momentum".to_string(), 0.5)]);
        assert!(check_expectations(&scenario, &passing).is_ok());

        let failing = HashMap::from([("focus_momentum".to_string(), 0.4)]);
        let err = check_expectations(&scenario, &failing).unwrap_err();
        assert!(err.contains("0.4 < min 0.5"), "unexpected message: {err}");
    }

    #[test]
    fn parse_event_type_recognizes_every_known_variant() {
        assert_eq!(parse_event_type("key_press"), Some(EventType::KeyPress));
        assert_eq!(parse_event_type("key_release"), Some(EventType::KeyRelease));
        assert_eq!(parse_event_type("mouse_move"), Some(EventType::MouseMove));
        assert_eq!(parse_event_type("mouse_click"), Some(EventType::MouseClick));
        assert_eq!(
            parse_event_type("window_focus_change"),
            Some(EventType::WindowFocusChange)
        );
        assert_eq!(
            parse_event_type("window_title_change"),
            Some(EventType::WindowTitleChange)
        );
        assert_eq!(parse_event_type("idle_start"), Some(EventType::IdleStart));
        assert_eq!(parse_event_type("idle_end"), Some(EventType::IdleEnd));
    }

    #[test]
    fn parse_event_type_returns_none_for_unknown_strings() {
        assert_eq!(parse_event_type("keypress"), None); // missing underscore typo
        assert_eq!(parse_event_type(""), None);
        assert_eq!(parse_event_type("KEY_PRESS"), None); // wrong case
    }

    #[test]
    fn replay_scenario_skips_unrecognized_event_types_instead_of_panicking() {
        // A typo'd or future/unknown event type in a scenario fixture must
        // be silently skipped by replay_scenario (via parse_event_type's
        // None fallback), not cause a panic — this is the only test that
        // actually feeds an unrecognized type through the full replay path.
        let scenario = Scenario {
            name: "unknown-event".to_string(),
            base_time: 0.0,
            events: vec![
                ScenarioEvent {
                    offset_secs: 0.0,
                    event_type: "keyboard_smash".to_string(),
                    app: "Code".to_string(),
                    title: "main.rs".to_string(),
                    mouse_speed: 0,
                    idle_duration_ms: 0,
                },
                ScenarioEvent {
                    offset_secs: 1.0,
                    event_type: "key_press".to_string(),
                    app: "Code".to_string(),
                    title: "main.rs".to_string(),
                    mouse_speed: 0,
                    idle_duration_ms: 0,
                },
            ],
            expect: HashMap::new(),
        };

        let features = replay_scenario(&scenario, &[]);
        // Only the recognized key_press event should have been counted.
        assert_eq!(features.keystroke_count, 1);
    }
}
