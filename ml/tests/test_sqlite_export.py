import os
import sqlite3
import tempfile
import unittest
from datetime import datetime, timezone

from ml.labeling import FocusLabel
from ml.sqlite_export import (
    export_features_csv,
    export_labels_csv,
    export_training_csvs,
)


def _create_test_db(path: str) -> None:
    conn = sqlite3.connect(path)
    conn.executescript(
        """
        CREATE TABLE sessions (
            session_id TEXT PRIMARY KEY,
            goal TEXT NOT NULL,
            status TEXT NOT NULL,
            focus_mode TEXT NOT NULL DEFAULT 'normal',
            started_at TEXT NOT NULL,
            ended_at TEXT
        );

        CREATE TABLE feature_snapshots (
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
            is_pseudo_productive INTEGER NOT NULL
        );

        CREATE TABLE labels (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            label INTEGER NOT NULL,
            source TEXT NOT NULL DEFAULT 'manual',
            notes TEXT,
            timestamp TEXT NOT NULL
        );

        INSERT INTO sessions VALUES ('s1', 'test', 'ACTIVE', 'normal', '2026-01-01T00:00:00Z', NULL);

        INSERT INTO feature_snapshots (
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
            's1', 1700000000.0,
            60, 14, 2, 5,
            8, 2.5, 0.1, 0.2,
            0.0, 3, 120.0, 1.5,
            0.4, 0.2, 1,
            1, 2, 90, 2,
            0.0, 0, 300,
            12, 0,
            0, 1, 0, 0, 0,
            0.75, 0
        );

        INSERT INTO labels (session_id, label, source, notes, timestamp)
        VALUES ('s1', -1, 'manual', 'youtube', '2026-01-01T00:00:10Z');
        """
    )
    conn.commit()
    conn.close()


class TestSqliteExport(unittest.TestCase):
    def test_export_features_and_labels(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            db_path = os.path.join(tmp, "focoflow.db")
            _create_test_db(db_path)

            features_path = os.path.join(tmp, "features.csv")
            labels_path = os.path.join(tmp, "labels.csv")

            feature_count = export_features_csv(db_path, features_path)
            label_count = export_labels_csv(db_path, labels_path)

            self.assertEqual(feature_count, 1)
            self.assertEqual(label_count, 1)

            with open(features_path, "r", encoding="utf-8") as handle:
                feature_lines = handle.read().splitlines()
            self.assertIn("keystroke_rate", feature_lines[0])
            self.assertIn("2.5", feature_lines[1])

            with open(labels_path, "r", encoding="utf-8") as handle:
                label_lines = handle.read().splitlines()
            self.assertTrue(label_lines[1].endswith(",youtube"))
            self.assertIn(str(int(FocusLabel.DISTRACTED)), label_lines[1])

    def test_export_training_csvs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            db_path = os.path.join(tmp, "focoflow.db")
            _create_test_db(db_path)
            out_dir = os.path.join(tmp, "export")
            counts = export_training_csvs(db_path, out_dir)
            self.assertEqual(counts.features, 1)
            self.assertEqual(counts.labels, 1)
            self.assertTrue(os.path.isfile(os.path.join(out_dir, "features.csv")))
            self.assertTrue(os.path.isfile(os.path.join(out_dir, "labels.csv")))

    def test_rfc3339_label_timestamp_is_unix_float(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            db_path = os.path.join(tmp, "focoflow.db")
            _create_test_db(db_path)
            labels_path = os.path.join(tmp, "labels.csv")
            export_labels_csv(db_path, labels_path)
            with open(labels_path, "r", encoding="utf-8") as handle:
                row = handle.read().splitlines()[1].split(",")
            ts = float(row[0])
            expected = datetime(2026, 1, 1, 0, 0, 10, tzinfo=timezone.utc).timestamp()
            self.assertAlmostEqual(ts, expected, places=3)


if __name__ == "__main__":
    unittest.main()
