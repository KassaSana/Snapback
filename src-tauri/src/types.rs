use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum EventType {
    KeyPress = 1,
    KeyRelease = 2,
    MouseMove = 3,
    MouseClick = 4,
    WindowFocusChange = 5,
    WindowTitleChange = 6,
    IdleStart = 7,
    IdleEnd = 8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CaptureEvent {
    pub event_type: EventType,
    pub timestamp_secs: f64,
    pub app_name: String,
    pub window_title: String,
    pub mouse_x: i32,
    pub mouse_y: i32,
    pub mouse_speed: u32,
    pub idle_duration_ms: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PredictionRecord {
    pub session_id: String,
    pub focus_score: f64,
    pub distraction_risk: f64,
    pub focus_state: String,
    #[serde(default)]
    pub thrash_score: f64,
    #[serde(default)]
    pub drift_score: f64,
    #[serde(default = "default_goal_alignment")]
    pub goal_alignment: f64,
    pub timestamp: String,
}

fn default_goal_alignment() -> f64 {
    0.5
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionRecord {
    pub session_id: String,
    pub goal: String,
    pub status: String,
    pub focus_mode: String,
    pub started_at: Option<String>,
    pub ended_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ContextSnapshotDto {
    pub app_name: String,
    pub window_title: String,
    pub file_hint: String,
    pub project_hint: String,
    pub summary: String,
    pub timestamp: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SnapbackPayload {
    pub summary: String,
    pub app_name: String,
    pub window_title: String,
    pub file_hint: String,
    pub distraction_duration_secs: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PermissionStatus {
    pub capture_available: bool,
    pub capture_probe_confirmed: bool,
    pub active_window_available: bool,
    pub message: String,
    #[serde(default)]
    pub setup_steps: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CaptureFailurePayload {
    pub reason: String,
    pub message: String,
    pub setup_steps: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct OverlayFailurePayload {
    pub reason: String,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PersistenceFailurePayload {
    pub reason: String,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionRecap {
    pub session_id: String,
    pub goal: String,
    pub duration_secs: u64,
    pub avg_focus_score: f64,
    pub avg_distraction_risk: f64,
    pub snapback_count: u32,
    pub thrash_spikes: u32,
    pub deep_focus_pct: f64,
}

/// A past session paired with its computed recap, for the insights/history view.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionSummary {
    pub record: SessionRecord,
    pub recap: SessionRecap,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum FocusLabel {
    Distracted = -1,
    PseudoProductive = 0,
    Productive = 1,
    DeepFocus = 2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LabelSource {
    Manual,
    Hotkey,
    Survey,
    Auto,
}

impl LabelSource {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Manual => "manual",
            Self::Hotkey => "hotkey",
            Self::Survey => "survey",
            Self::Auto => "auto",
        }
    }

    pub fn parse(value: Option<&str>) -> Self {
        match value.unwrap_or("manual").to_lowercase().as_str() {
            "hotkey" => Self::Hotkey,
            "survey" => Self::Survey,
            "auto" => Self::Auto,
            _ => Self::Manual,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LabelRequest {
    pub session_id: String,
    pub label: FocusLabel,
    pub notes: Option<String>,
    #[serde(default)]
    pub source: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum FocusMode {
    Deep,
    Normal,
    Recovery,
}

impl FocusMode {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Deep => "deep",
            Self::Normal => "normal",
            Self::Recovery => "recovery",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s.to_lowercase().as_str() {
            "deep" => Self::Deep,
            "recovery" => Self::Recovery,
            _ => Self::Normal,
        }
    }

    pub fn risk_threshold(self) -> f64 {
        match self {
            Self::Deep => 0.55,
            Self::Normal => 0.7,
            Self::Recovery => 0.85,
        }
    }

    pub fn hyperfocus_minutes(self) -> u32 {
        match self {
            Self::Deep => 90,
            Self::Normal => 120,
            Self::Recovery => 45,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ClassifierStatus {
    pub backend: String,
    pub onnx_runtime_enabled: bool,
    pub model_path: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HealthStatus {
    pub status: String,
    pub capture_running: bool,
    pub capture_failed: bool,
    pub capture_failure_reason: Option<String>,
    pub overlay_failure_reason: Option<String>,
    pub persistence_failure_reason: Option<String>,
    /// Capture events dropped since capture last (re)started because the
    /// engine loop wasn't draining the bounded event channel fast enough.
    pub capture_events_dropped: u64,
    /// Capture claims to be running, but no input events have arrived within the
    /// startup grace period — the listener/poll pipeline is likely blocked even
    /// though permissions look fine.
    pub capture_stalled: bool,
    pub permissions: PermissionStatus,
    pub classifier: ClassifierStatus,
}

/// User override: treat apps/titles matching `pattern` as on-task or distracting.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum AppRuleKind {
    Allow,
    Block,
}

impl AppRuleKind {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Allow => "allow",
            Self::Block => "block",
        }
    }

    pub fn from_str(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "allow" => Some(Self::Allow),
            "block" => Some(Self::Block),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppRuleRecord {
    pub id: i64,
    /// Lowercase substring matched against app name and window title.
    pub pattern: String,
    pub rule_type: AppRuleKind,
    pub note: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UpsertAppRuleRequest {
    pub pattern: String,
    pub rule_type: AppRuleKind,
    pub note: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ExportTrainingResult {
    pub output_dir: String,
    pub features_path: String,
    pub labels_path: String,
    pub feature_count: usize,
    pub label_count: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn label_source_parse_recognizes_known_values_case_insensitively() {
        assert_eq!(LabelSource::parse(Some("hotkey")), LabelSource::Hotkey);
        assert_eq!(LabelSource::parse(Some("HOTKEY")), LabelSource::Hotkey);
        assert_eq!(LabelSource::parse(Some("Survey")), LabelSource::Survey);
        assert_eq!(LabelSource::parse(Some("AUTO")), LabelSource::Auto);
        assert_eq!(LabelSource::parse(Some("manual")), LabelSource::Manual);
    }

    #[test]
    fn label_source_parse_falls_back_to_manual_for_none_or_unknown() {
        // This is the branch that matters most in production: it's what
        // fires on a Rust/frontend field mismatch or a source string that
        // predates a schema change. It must never panic.
        assert_eq!(LabelSource::parse(None), LabelSource::Manual);
        assert_eq!(LabelSource::parse(Some("")), LabelSource::Manual);
        assert_eq!(LabelSource::parse(Some("bogus")), LabelSource::Manual);
    }

    #[test]
    fn label_source_as_str_round_trips_through_parse() {
        // LabelSource has no Serialize derive, so a serde-consistency check
        // (like the one used for FocusMode/AppRuleKind below) isn't
        // available here — round-tripping through parse() is the
        // equivalent guarantee: as_str() output must always be reparsed
        // back to the same variant.
        for source in [
            LabelSource::Manual,
            LabelSource::Hotkey,
            LabelSource::Survey,
            LabelSource::Auto,
        ] {
            assert_eq!(LabelSource::parse(Some(source.as_str())), source);
        }
    }

    #[test]
    fn focus_mode_from_str_recognizes_known_values_case_insensitively() {
        assert_eq!(FocusMode::from_str("deep"), FocusMode::Deep);
        assert_eq!(FocusMode::from_str("DEEP"), FocusMode::Deep);
        assert_eq!(FocusMode::from_str("recovery"), FocusMode::Recovery);
        assert_eq!(FocusMode::from_str("normal"), FocusMode::Normal);
    }

    #[test]
    fn focus_mode_from_str_falls_back_to_normal_for_unknown() {
        assert_eq!(FocusMode::from_str(""), FocusMode::Normal);
        assert_eq!(FocusMode::from_str("bogus"), FocusMode::Normal);
    }

    #[test]
    fn focus_mode_as_str_matches_serde_serialization() {
        // FocusMode derives Serialize with rename_all = "lowercase", so
        // as_str() and serde's wire format must never drift apart —
        // otherwise a value built in Rust and one deserialized from JSON
        // could compare equal but print differently on each side.
        for mode in [FocusMode::Deep, FocusMode::Normal, FocusMode::Recovery] {
            let serde_form = serde_json::to_value(mode)
                .expect("FocusMode serializes")
                .as_str()
                .expect("FocusMode serializes to a JSON string")
                .to_string();
            assert_eq!(mode.as_str(), serde_form);
        }
    }

    #[test]
    fn focus_mode_thresholds_and_hyperfocus_minutes_are_stable_per_variant() {
        assert_eq!(FocusMode::Deep.risk_threshold(), 0.55);
        assert_eq!(FocusMode::Normal.risk_threshold(), 0.7);
        assert_eq!(FocusMode::Recovery.risk_threshold(), 0.85);

        assert_eq!(FocusMode::Deep.hyperfocus_minutes(), 90);
        assert_eq!(FocusMode::Normal.hyperfocus_minutes(), 120);
        assert_eq!(FocusMode::Recovery.hyperfocus_minutes(), 45);
    }

    #[test]
    fn app_rule_kind_from_str_recognizes_known_values_case_insensitively() {
        assert_eq!(AppRuleKind::from_str("allow"), Some(AppRuleKind::Allow));
        assert_eq!(AppRuleKind::from_str("ALLOW"), Some(AppRuleKind::Allow));
        assert_eq!(AppRuleKind::from_str("block"), Some(AppRuleKind::Block));
    }

    #[test]
    fn app_rule_kind_from_str_returns_none_for_unknown() {
        // Note the asymmetry with LabelSource::parse/FocusMode::from_str
        // above: those two fall back to a default *variant* on unknown
        // input, but AppRuleKind::from_str returns None instead. Same
        // lowercase-match-then-fallback shape, different failure
        // semantics — an app rule with a garbled kind is treated as
        // "absent," not silently coerced to "allow" or "block."
        assert_eq!(AppRuleKind::from_str(""), None);
        assert_eq!(AppRuleKind::from_str("bogus"), None);
    }

    #[test]
    fn app_rule_kind_as_str_matches_serde_serialization() {
        for kind in [AppRuleKind::Allow, AppRuleKind::Block] {
            let serde_form = serde_json::to_value(kind)
                .expect("AppRuleKind serializes")
                .as_str()
                .expect("AppRuleKind serializes to a JSON string")
                .to_string();
            assert_eq!(kind.as_str(), serde_form);
        }
    }
}
