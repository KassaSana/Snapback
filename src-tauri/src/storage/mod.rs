use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};

use rusqlite::{params, Connection};
use thiserror::Error;
use uuid::Uuid;

use crate::engine::features::FeatureVector;
use crate::types::{
    AppRuleKind, AppRuleRecord, ContextSnapshotDto, ExportTrainingResult, FocusLabel, LabelSource,
    PredictionRecord, SessionRecap, SessionRecord,
};

const FEATURE_EXPORT_COLUMNS: &[&str] = &[
    "timestamp",
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

#[derive(Debug, Error)]
pub enum StorageError {
    #[error("database error: {0}")]
    Sqlite(#[from] rusqlite::Error),
    #[error("session not found")]
    SessionNotFound,
    #[error("app rule not found")]
    AppRuleNotFound,
    #[error("invalid app rule: {0}")]
    InvalidAppRule(String),
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}

pub struct Storage {
    conn: Connection,
}

impl Storage {
    pub fn open(app_data_dir: PathBuf) -> Result<Self, StorageError> {
        std::fs::create_dir_all(&app_data_dir).ok();
        let db_path = app_data_dir.join("focoflow.db");
        let conn = Connection::open(db_path)?;
        let storage = Self { conn };
        storage.init_schema()?;
        Ok(storage)
    }

    fn init_schema(&self) -> Result<(), StorageError> {
        self.conn.execute_batch(
            "
            CREATE TABLE IF NOT EXISTS sessions (
                session_id TEXT PRIMARY KEY,
                goal TEXT NOT NULL,
                status TEXT NOT NULL,
                focus_mode TEXT NOT NULL DEFAULT 'normal',
                started_at TEXT NOT NULL,
                ended_at TEXT
            );

            CREATE TABLE IF NOT EXISTS predictions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                focus_score REAL NOT NULL,
                distraction_risk REAL NOT NULL,
                focus_state TEXT NOT NULL,
                thrash_score REAL NOT NULL DEFAULT 0.0,
                drift_score REAL NOT NULL DEFAULT 0.0,
                goal_alignment REAL NOT NULL DEFAULT 0.5,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_predictions_session_ts
                ON predictions(session_id, timestamp DESC);

            CREATE TABLE IF NOT EXISTS context_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                app_name TEXT NOT NULL,
                window_title TEXT NOT NULL,
                file_hint TEXT NOT NULL,
                project_hint TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS labels (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                label INTEGER NOT NULL,
                source TEXT NOT NULL DEFAULT 'manual',
                notes TEXT,
                timestamp TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS snapback_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS app_rules (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                pattern TEXT NOT NULL COLLATE NOCASE UNIQUE,
                rule_type TEXT NOT NULL CHECK (rule_type IN ('allow', 'block')),
                note TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_app_rules_pattern
                ON app_rules(pattern);

            CREATE TABLE IF NOT EXISTS feature_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                timestamp REAL NOT NULL,
                seconds_since_session_start INTEGER NOT NULL,
                hour_of_day INTEGER NOT NULL,
                day_of_week INTEGER NOT NULL,
                minutes_since_last_break INTEGER NOT NULL,
                keystroke_count INTEGER NOT NULL,
                keystroke_rate REAL NOT NULL,
                keystroke_interval_mean REAL NOT NULL,
                keystroke_interval_std REAL NOT NULL,
                keystroke_interval_trend REAL NOT NULL,
                mouse_move_count INTEGER NOT NULL,
                mouse_distance_pixels REAL NOT NULL,
                mouse_speed_mean REAL NOT NULL,
                mouse_speed_std REAL NOT NULL,
                mouse_acceleration_mean REAL NOT NULL,
                mouse_click_count INTEGER NOT NULL,
                context_switches_30s INTEGER NOT NULL,
                context_switches_5min INTEGER NOT NULL,
                time_in_current_app INTEGER NOT NULL,
                unique_apps_5min INTEGER NOT NULL,
                idle_time_30s REAL NOT NULL,
                idle_event_count_5min INTEGER NOT NULL,
                longest_active_stretch_5min INTEGER NOT NULL,
                window_title_length INTEGER NOT NULL,
                window_title_changed_30s INTEGER NOT NULL,
                is_browser INTEGER NOT NULL,
                is_ide INTEGER NOT NULL,
                is_communication INTEGER NOT NULL,
                is_entertainment INTEGER NOT NULL,
                is_productivity INTEGER NOT NULL,
                focus_momentum REAL NOT NULL,
                is_pseudo_productive INTEGER NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_feature_snapshots_session_ts
                ON feature_snapshots(session_id, timestamp DESC);
            ",
        )?;

        // Migration: older databases may have the original `predictions` schema without
        // thrash/drift/goal_alignment. We add columns if missing.
        for sql in [
            "ALTER TABLE predictions ADD COLUMN thrash_score REAL NOT NULL DEFAULT 0.0",
            "ALTER TABLE predictions ADD COLUMN drift_score REAL NOT NULL DEFAULT 0.0",
            "ALTER TABLE predictions ADD COLUMN goal_alignment REAL NOT NULL DEFAULT 0.5",
        ] {
            if let Err(err) = self.conn.execute(sql, []) {
                // SQLite error message for existing column varies, so we match substrings.
                // Safe to ignore "duplicate column name", but fail for anything else.
                if !err.to_string().to_lowercase().contains("duplicate column") {
                    return Err(StorageError::Sqlite(err));
                }
            }
        }

        Ok(())
    }

    fn normalize_app_rule_pattern(pattern: &str) -> Result<String, StorageError> {
        let normalized = pattern.trim().to_lowercase();
        if normalized.is_empty() {
            return Err(StorageError::InvalidAppRule(
                "pattern cannot be empty".to_string(),
            ));
        }
        Ok(normalized)
    }

    fn map_app_rule_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<AppRuleRecord> {
        let rule_type_raw: String = row.get(2)?;
        let rule_type = AppRuleKind::from_str(&rule_type_raw).ok_or_else(|| {
            rusqlite::Error::InvalidColumnType(
                2,
                "rule_type".to_string(),
                rusqlite::types::Type::Text,
            )
        })?;
        Ok(AppRuleRecord {
            id: row.get(0)?,
            pattern: row.get(1)?,
            rule_type,
            note: row.get(3)?,
            created_at: row.get(4)?,
            updated_at: row.get(5)?,
        })
    }

    pub fn list_app_rules(&self) -> Result<Vec<AppRuleRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, pattern, rule_type, note, created_at, updated_at
             FROM app_rules
             ORDER BY pattern ASC",
        )?;
        let rows = stmt.query_map([], Self::map_app_rule_row)?;
        Ok(rows.filter_map(Result::ok).collect())
    }

    pub fn upsert_app_rule(
        &self,
        pattern: &str,
        rule_type: AppRuleKind,
        note: Option<&str>,
    ) -> Result<AppRuleRecord, StorageError> {
        let pattern = Self::normalize_app_rule_pattern(pattern)?;
        let now = chrono::Utc::now().to_rfc3339();

        self.conn.execute(
            "INSERT INTO app_rules (pattern, rule_type, note, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?4)
             ON CONFLICT(pattern) DO UPDATE SET
                rule_type = excluded.rule_type,
                note = excluded.note,
                updated_at = excluded.updated_at",
            params![pattern, rule_type.as_str(), note, now],
        )?;

        self.get_app_rule_by_pattern(&pattern)?
            .ok_or(StorageError::AppRuleNotFound)
    }

    pub fn get_app_rule(&self, id: i64) -> Result<AppRuleRecord, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, pattern, rule_type, note, created_at, updated_at
             FROM app_rules WHERE id = ?1",
        )?;
        stmt.query_row(params![id], Self::map_app_rule_row)
            .map_err(|err| match err {
                rusqlite::Error::QueryReturnedNoRows => StorageError::AppRuleNotFound,
                other => StorageError::Sqlite(other),
            })
    }

    pub fn get_app_rule_by_pattern(
        &self,
        pattern: &str,
    ) -> Result<Option<AppRuleRecord>, StorageError> {
        let pattern = Self::normalize_app_rule_pattern(pattern)?;
        let mut stmt = self.conn.prepare(
            "SELECT id, pattern, rule_type, note, created_at, updated_at
             FROM app_rules WHERE pattern = ?1 COLLATE NOCASE",
        )?;
        let mut rows = stmt.query(params![pattern])?;
        if let Some(row) = rows.next()? {
            Ok(Some(Self::map_app_rule_row(row)?))
        } else {
            Ok(None)
        }
    }

    pub fn delete_app_rule(&self, id: i64) -> Result<(), StorageError> {
        let deleted = self
            .conn
            .execute("DELETE FROM app_rules WHERE id = ?1", params![id])?;
        if deleted == 0 {
            return Err(StorageError::AppRuleNotFound);
        }
        Ok(())
    }

    pub fn start_session(
        &self,
        goal: &str,
        focus_mode: &str,
    ) -> Result<SessionRecord, StorageError> {
        let session_id = Uuid::new_v4().to_string();
        let started_at = chrono::Utc::now().to_rfc3339();
        self.conn.execute(
            "INSERT INTO sessions (session_id, goal, status, focus_mode, started_at) VALUES (?1, ?2, 'ACTIVE', ?3, ?4)",
            params![session_id, goal, focus_mode, started_at],
        )?;
        Ok(SessionRecord {
            session_id,
            goal: goal.to_string(),
            status: "ACTIVE".to_string(),
            focus_mode: focus_mode.to_string(),
            started_at: Some(started_at),
            ended_at: None,
        })
    }

    pub fn stop_session(&self, session_id: &str) -> Result<SessionRecord, StorageError> {
        if let Ok(existing) = self.get_session(session_id) {
            if existing.status == "COMPLETED" {
                return Ok(existing);
            }
        }

        let ended_at = chrono::Utc::now().to_rfc3339();
        let updated = self.conn.execute(
            "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 WHERE session_id = ?2 AND status = 'ACTIVE'",
            params![ended_at, session_id],
        )?;
        if updated == 0 {
            return Err(StorageError::SessionNotFound);
        }
        self.get_session(session_id)
    }

    pub fn get_session(&self, session_id: &str) -> Result<SessionRecord, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, goal, status, focus_mode, started_at, ended_at FROM sessions WHERE session_id = ?1",
        )?;
        let row = stmt.query_row(params![session_id], |row| {
            Ok(SessionRecord {
                session_id: row.get(0)?,
                goal: row.get(1)?,
                status: row.get(2)?,
                focus_mode: row.get(3)?,
                started_at: row.get(4)?,
                ended_at: row.get(5)?,
            })
        })?;
        Ok(row)
    }

    pub fn get_active_session(&self) -> Result<Option<SessionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, goal, status, focus_mode, started_at, ended_at FROM sessions WHERE status = 'ACTIVE' ORDER BY started_at DESC LIMIT 1",
        )?;
        let mut rows = stmt.query([])?;
        if let Some(row) = rows.next()? {
            Ok(Some(SessionRecord {
                session_id: row.get(0)?,
                goal: row.get(1)?,
                status: row.get(2)?,
                focus_mode: row.get(3)?,
                started_at: row.get(4)?,
                ended_at: row.get(5)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn save_prediction(&self, record: &PredictionRecord) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO predictions (session_id, focus_score, distraction_risk, focus_state, thrash_score, drift_score, goal_alignment, timestamp)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
            params![
                record.session_id,
                record.focus_score,
                record.distraction_risk,
                record.focus_state,
                record.thrash_score,
                record.drift_score,
                record.goal_alignment,
                record.timestamp,
            ],
        )?;
        Ok(())
    }

    pub fn latest_prediction(&self) -> Result<Option<PredictionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, focus_score, distraction_risk, focus_state, thrash_score, drift_score, goal_alignment, timestamp
             FROM predictions ORDER BY timestamp DESC LIMIT 1",
        )?;
        let mut rows = stmt.query([])?;
        if let Some(row) = rows.next()? {
            Ok(Some(PredictionRecord {
                session_id: row.get(0)?,
                focus_score: row.get(1)?,
                distraction_risk: row.get(2)?,
                focus_state: row.get(3)?,
                thrash_score: row.get(4)?,
                drift_score: row.get(5)?,
                goal_alignment: row.get(6)?,
                timestamp: row.get(7)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn recent_predictions(&self, limit: usize) -> Result<Vec<PredictionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, focus_score, distraction_risk, focus_state, thrash_score, drift_score, goal_alignment, timestamp
             FROM predictions ORDER BY timestamp DESC LIMIT ?1",
        )?;
        let rows = stmt.query_map(params![limit as i64], |row| {
            Ok(PredictionRecord {
                session_id: row.get(0)?,
                focus_score: row.get(1)?,
                distraction_risk: row.get(2)?,
                focus_state: row.get(3)?,
                thrash_score: row.get(4)?,
                drift_score: row.get(5)?,
                goal_alignment: row.get(6)?,
                timestamp: row.get(7)?,
            })
        })?;
        Ok(rows.filter_map(Result::ok).collect())
    }

    pub fn save_context_snapshot(
        &self,
        session_id: &str,
        snapshot: &ContextSnapshotDto,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO context_snapshots (session_id, app_name, window_title, file_hint, project_hint, summary, timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                session_id,
                snapshot.app_name,
                snapshot.window_title,
                snapshot.file_hint,
                snapshot.project_hint,
                snapshot.summary,
                snapshot.timestamp,
            ],
        )?;
        Ok(())
    }

    pub fn list_context_snapshots(
        &self,
        session_id: &str,
        limit: usize,
    ) -> Result<Vec<ContextSnapshotDto>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT app_name, window_title, file_hint, project_hint, summary, timestamp
             FROM context_snapshots
             WHERE session_id = ?1
             ORDER BY timestamp ASC
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(params![session_id, limit as i64], |row| {
            Ok(ContextSnapshotDto {
                app_name: row.get(0)?,
                window_title: row.get(1)?,
                file_hint: row.get(2)?,
                project_hint: row.get(3)?,
                summary: row.get(4)?,
                timestamp: row.get(5)?,
            })
        })?;
        Ok(rows.filter_map(Result::ok).collect())
    }

    pub fn save_feature_snapshot(
        &self,
        session_id: &str,
        features: &FeatureVector,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO feature_snapshots (
                session_id, timestamp,
                seconds_since_session_start, hour_of_day, day_of_week, minutes_since_last_break,
                keystroke_count, keystroke_rate, keystroke_interval_mean, keystroke_interval_std,
                keystroke_interval_trend, mouse_move_count, mouse_distance_pixels, mouse_speed_mean,
                mouse_speed_std, mouse_acceleration_mean, mouse_click_count,
                context_switches_30s, context_switches_5min, time_in_current_app, unique_apps_5min,
                idle_time_30s, idle_event_count_5min, longest_active_stretch_5min,
                window_title_length, window_title_changed_30s,
                is_browser, is_ide, is_communication, is_entertainment, is_productivity,
                focus_momentum, is_pseudo_productive
            ) VALUES (
                ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17,
                ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33
            )",
            params![
                session_id,
                features.timestamp,
                features.seconds_since_session_start,
                features.hour_of_day,
                features.day_of_week,
                features.minutes_since_last_break,
                features.keystroke_count as i64,
                features.keystroke_rate,
                features.keystroke_interval_mean,
                features.keystroke_interval_std,
                features.keystroke_interval_trend,
                features.mouse_move_count as i64,
                features.mouse_distance_pixels,
                features.mouse_speed_mean,
                features.mouse_speed_std,
                features.mouse_acceleration_mean,
                features.mouse_click_count as i64,
                features.context_switches_30s as i64,
                features.context_switches_5min as i64,
                features.time_in_current_app,
                features.unique_apps_5min as i64,
                features.idle_time_30s,
                features.idle_event_count_5min as i64,
                features.longest_active_stretch_5min,
                features.window_title_length as i64,
                i64::from(features.window_title_changed_30s),
                i64::from(features.is_browser),
                i64::from(features.is_ide),
                i64::from(features.is_communication),
                i64::from(features.is_entertainment),
                i64::from(features.is_productivity),
                features.focus_momentum,
                i64::from(features.is_pseudo_productive),
            ],
        )?;
        Ok(())
    }

    pub fn save_label(
        &self,
        session_id: &str,
        label: FocusLabel,
        source: LabelSource,
        notes: Option<&str>,
    ) -> Result<(), StorageError> {
        let timestamp = chrono::Utc::now().to_rfc3339();
        self.conn.execute(
            "INSERT INTO labels (session_id, label, source, notes, timestamp) VALUES (?1, ?2, ?3, ?4, ?5)",
            params![
                session_id,
                label as i32,
                source.as_str(),
                notes,
                timestamp
            ],
        )?;
        Ok(())
    }

    pub fn infer_session_label(recap: &SessionRecap) -> FocusLabel {
        if recap.deep_focus_pct >= 50.0 && recap.avg_distraction_risk < 0.35 {
            FocusLabel::DeepFocus
        } else if recap.avg_distraction_risk >= 0.6 || recap.thrash_spikes >= 3 {
            FocusLabel::Distracted
        } else if recap.deep_focus_pct < 25.0 && recap.thrash_spikes >= 1 {
            FocusLabel::PseudoProductive
        } else {
            FocusLabel::Productive
        }
    }

    pub fn save_auto_session_label(&self, session_id: &str) -> Result<FocusLabel, StorageError> {
        let recap = self.session_recap(session_id)?;
        let label = Self::infer_session_label(&recap);
        self.save_label(
            session_id,
            label,
            LabelSource::Auto,
            Some("inferred from session recap"),
        )?;
        Ok(label)
    }

    pub fn record_snapback(&self, session_id: &str, summary: &str) -> Result<(), StorageError> {
        let timestamp = chrono::Utc::now().to_rfc3339();
        self.conn.execute(
            "INSERT INTO snapback_events (session_id, summary, timestamp) VALUES (?1, ?2, ?3)",
            params![session_id, summary, timestamp],
        )?;
        Ok(())
    }

    pub fn session_recap(&self, session_id: &str) -> Result<SessionRecap, StorageError> {
        let session = self.get_session(session_id)?;
        let started = session
            .started_at
            .as_deref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|d| d.with_timezone(&chrono::Utc));
        let ended = session
            .ended_at
            .as_deref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|d| d.with_timezone(&chrono::Utc))
            .unwrap_or_else(chrono::Utc::now);
        let duration_secs = started
            .map(|s| (ended - s).num_seconds().max(0) as u64)
            .unwrap_or(0);

        let (avg_focus, avg_risk, deep_pct): (f64, f64, f64) = self.conn.query_row(
            "SELECT COALESCE(AVG(focus_score), 0), COALESCE(AVG(distraction_risk), 0),
                    COALESCE(100.0 * SUM(CASE WHEN focus_state = 'DEEP_FOCUS' THEN 1 ELSE 0 END) / NULLIF(COUNT(*), 0), 0)
             FROM predictions WHERE session_id = ?1",
            params![session_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        )?;

        let snapback_count: u32 = self.conn.query_row(
            "SELECT COUNT(*) FROM snapback_events WHERE session_id = ?1",
            params![session_id],
            |row| row.get(0),
        )?;

        let thrash_spikes: u32 = self.conn.query_row(
            "SELECT COUNT(*) FROM predictions WHERE session_id = ?1 AND distraction_risk >= 0.7 AND focus_state = 'DISTRACTED'",
            params![session_id],
            |row| row.get(0),
        )?;

        Ok(SessionRecap {
            session_id: session_id.to_string(),
            goal: session.goal,
            duration_secs,
            avg_focus_score: avg_focus,
            avg_distraction_risk: avg_risk,
            snapback_count,
            thrash_spikes,
            deep_focus_pct: deep_pct,
        })
    }

    fn table_exists(&self, name: &str) -> Result<bool, StorageError> {
        let count: i64 = self.conn.query_row(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?1",
            params![name],
            |row| row.get(0),
        )?;
        Ok(count > 0)
    }

    fn rfc3339_to_unix(ts: &str) -> f64 {
        let normalized = ts.trim().replace('Z', "+00:00");
        chrono::DateTime::parse_from_rfc3339(&normalized)
            .map(|dt| dt.timestamp() as f64 + dt.timestamp_subsec_nanos() as f64 / 1_000_000_000.0)
            .unwrap_or(0.0)
    }

    pub fn export_training_data(
        &self,
        output_dir: &Path,
        session_id: Option<&str>,
    ) -> Result<ExportTrainingResult, StorageError> {
        fs::create_dir_all(output_dir)?;

        let features_path = output_dir.join("features.csv");
        let labels_path = output_dir.join("labels.csv");

        let feature_count = if self.table_exists("feature_snapshots")? {
            self.write_feature_export_csv(&features_path, session_id)?
        } else {
            Self::write_csv_header(&features_path, FEATURE_EXPORT_COLUMNS)?;
            0
        };

        let label_count = if self.table_exists("labels")? {
            self.write_label_export_csv(&labels_path, session_id)?
        } else {
            Self::write_csv_header(
                &labels_path,
                &["timestamp", "label", "source", "session_id", "notes"],
            )?;
            0
        };

        Ok(ExportTrainingResult {
            output_dir: output_dir.display().to_string(),
            features_path: features_path.display().to_string(),
            labels_path: labels_path.display().to_string(),
            feature_count,
            label_count,
        })
    }

    fn write_csv_header(path: &Path, headers: &[&str]) -> Result<(), StorageError> {
        let mut file = File::create(path)?;
        writeln!(file, "{}", headers.join(","))?;
        Ok(())
    }

    fn write_feature_export_csv(
        &self,
        path: &Path,
        session_id: Option<&str>,
    ) -> Result<usize, StorageError> {
        let columns = FEATURE_EXPORT_COLUMNS.join(", ");
        let (query, filter): (String, bool) = if session_id.is_some() {
            (
                format!(
                    "SELECT {columns} FROM feature_snapshots WHERE session_id = ?1 ORDER BY timestamp ASC"
                ),
                true,
            )
        } else {
            (
                format!("SELECT {columns} FROM feature_snapshots ORDER BY timestamp ASC"),
                false,
            )
        };

        let mut stmt = self.conn.prepare(&query)?;
        let mut rows = if filter {
            stmt.query(params![session_id])?
        } else {
            stmt.query([])?
        };

        let mut file = File::create(path)?;
        writeln!(file, "{}", FEATURE_EXPORT_COLUMNS.join(","))?;

        let col_count = FEATURE_EXPORT_COLUMNS.len();
        let mut count = 0;
        while let Some(row) = rows.next()? {
            let values: Vec<String> = (0..col_count)
                .map(|idx| match row.get_ref(idx)? {
                    rusqlite::types::ValueRef::Null => Ok(String::new()),
                    rusqlite::types::ValueRef::Integer(v) => Ok(v.to_string()),
                    rusqlite::types::ValueRef::Real(v) => Ok(v.to_string()),
                    rusqlite::types::ValueRef::Text(v) => {
                        Ok(String::from_utf8_lossy(v).into_owned())
                    }
                    rusqlite::types::ValueRef::Blob(v) => {
                        Ok(String::from_utf8_lossy(v).into_owned())
                    }
                })
                .collect::<Result<_, rusqlite::Error>>()?;
            writeln!(file, "{}", values.join(","))?;
            count += 1;
        }
        Ok(count)
    }

    fn write_label_export_csv(
        &self,
        path: &Path,
        session_id: Option<&str>,
    ) -> Result<usize, StorageError> {
        let (query, filter): (&str, bool) = if session_id.is_some() {
            (
                "SELECT timestamp, label, source, session_id, notes FROM labels WHERE session_id = ?1 ORDER BY timestamp ASC",
                true,
            )
        } else {
            (
                "SELECT timestamp, label, source, session_id, notes FROM labels ORDER BY timestamp ASC",
                false,
            )
        };

        let mut stmt = self.conn.prepare(query)?;
        let mut rows = if filter {
            stmt.query(params![session_id])?
        } else {
            stmt.query([])?
        };

        let mut file = File::create(path)?;
        writeln!(file, "timestamp,label,source,session_id,notes")?;

        let mut count = 0;
        while let Some(row) = rows.next()? {
            let ts_raw: String = row.get(0)?;
            let label: i32 = row.get(1)?;
            let source: String = row.get(2)?;
            let sid: String = row.get(3)?;
            let notes: Option<String> = row.get(4)?;
            let ts = Self::rfc3339_to_unix(&ts_raw);
            let notes_cell = notes.unwrap_or_default().replace(',', " ");
            writeln!(
                file,
                "{ts:.6},{label},{},{sid},{notes_cell}",
                source.to_uppercase()
            )?;
            count += 1;
        }
        Ok(count)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn session_lifecycle() {
        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir).unwrap();
        let session = storage.start_session("Ship snapback", "normal").unwrap();
        assert_eq!(session.status, "ACTIVE");
        let stopped = storage.stop_session(&session.session_id).unwrap();
        assert_eq!(stopped.status, "COMPLETED");
    }

    #[test]
    fn app_rules_crud() {
        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir).unwrap();

        let created = storage
            .upsert_app_rule("Discord", AppRuleKind::Allow, Some("study group"))
            .unwrap();
        assert_eq!(created.pattern, "discord");
        assert_eq!(created.rule_type, AppRuleKind::Allow);
        assert_eq!(created.note.as_deref(), Some("study group"));

        let listed = storage.list_app_rules().unwrap();
        assert_eq!(listed.len(), 1);

        let updated = storage
            .upsert_app_rule("discord", AppRuleKind::Block, None)
            .unwrap();
        assert_eq!(updated.id, created.id);
        assert_eq!(updated.rule_type, AppRuleKind::Block);
        assert!(updated.note.is_none());

        storage.delete_app_rule(created.id).unwrap();
        assert!(storage.list_app_rules().unwrap().is_empty());
    }

    #[test]
    fn feature_snapshot_round_trip() {
        use crate::engine::features::FeatureVector;

        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir).unwrap();
        let session = storage.start_session("Train model", "normal").unwrap();

        let features = FeatureVector {
            timestamp: 1_700_000_000.0,
            seconds_since_session_start: 120,
            hour_of_day: 14,
            day_of_week: 2,
            minutes_since_last_break: 5,
            keystroke_count: 8,
            keystroke_rate: 2.5,
            keystroke_interval_std: 0.2,
            context_switches_30s: 1,
            is_ide: true,
            focus_momentum: 0.75,
            ..FeatureVector::empty(1_700_000_000.0)
        };

        storage
            .save_feature_snapshot(&session.session_id, &features)
            .unwrap();

        let count: i64 = storage
            .conn
            .query_row(
                "SELECT COUNT(*) FROM feature_snapshots WHERE session_id = ?1",
                params![session.session_id],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(count, 1);
    }

    #[test]
    fn context_snapshot_round_trip() {
        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir).unwrap();
        let session = storage.start_session("Write docs", "normal").unwrap();

        let snapshot = ContextSnapshotDto {
            app_name: "Cursor".to_string(),
            window_title: "state.rs — Snapback".to_string(),
            file_hint: "state.rs".to_string(),
            project_hint: "Snapback".to_string(),
            summary: "Editing state.rs in Snapback".to_string(),
            timestamp: chrono::Utc::now().to_rfc3339(),
        };
        storage
            .save_context_snapshot(&session.session_id, &snapshot)
            .unwrap();

        let count: i64 = storage
            .conn
            .query_row(
                "SELECT COUNT(*) FROM context_snapshots WHERE session_id = ?1",
                params![session.session_id],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(count, 1);

        let listed = storage
            .list_context_snapshots(&session.session_id, 10)
            .unwrap();
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].app_name, "Cursor");
        assert_eq!(listed[0].file_hint, "state.rs");
    }

    #[test]
    fn infer_session_label_from_recap() {
        let deep = SessionRecap {
            session_id: "s".to_string(),
            goal: "focus".to_string(),
            duration_secs: 3600,
            avg_focus_score: 80.0,
            avg_distraction_risk: 0.2,
            snapback_count: 0,
            thrash_spikes: 0,
            deep_focus_pct: 60.0,
        };
        assert_eq!(Storage::infer_session_label(&deep), FocusLabel::DeepFocus);

        let distracted = SessionRecap {
            avg_distraction_risk: 0.75,
            thrash_spikes: 4,
            deep_focus_pct: 10.0,
            ..deep
        };
        assert_eq!(
            Storage::infer_session_label(&distracted),
            FocusLabel::Distracted
        );
    }

    #[test]
    fn export_training_data_writes_csvs() {
        use crate::engine::features::FeatureVector;

        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir.clone()).unwrap();
        let session = storage.start_session("Export test", "normal").unwrap();

        let features = FeatureVector {
            timestamp: 1_700_000_000.0,
            keystroke_rate: 2.5,
            is_ide: true,
            ..FeatureVector::empty(1_700_000_000.0)
        };
        storage
            .save_feature_snapshot(&session.session_id, &features)
            .unwrap();
        storage
            .save_label(&session.session_id, FocusLabel::Distracted, LabelSource::Manual, Some("youtube"))
            .unwrap();

        let out_dir = dir.join("exports");
        let result = storage
            .export_training_data(&out_dir, None)
            .unwrap();

        assert_eq!(result.feature_count, 1);
        assert_eq!(result.label_count, 1);
        assert!(std::path::Path::new(&result.features_path).exists());
        assert!(std::path::Path::new(&result.labels_path).exists());
    }

    #[test]
    fn app_rule_empty_pattern_is_rejected() {
        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir).unwrap();
        let err = storage
            .upsert_app_rule("   ", AppRuleKind::Allow, None)
            .unwrap_err();
        assert!(matches!(err, StorageError::InvalidAppRule(_)));
    }
}
