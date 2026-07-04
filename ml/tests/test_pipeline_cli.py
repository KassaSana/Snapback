import os
import sqlite3
import tempfile
import unittest

from ml.pipeline_cli import run_pipeline


def _create_aligned_test_db(path: str) -> None:
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

        INSERT INTO sessions VALUES ('s1', 'test', 'ACTIVE', 'normal', '2023-11-14T22:13:20Z', NULL);

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
        VALUES ('s1', -1, 'manual', 'youtube', '2023-11-14T22:13:25Z');
        """
    )
    conn.commit()
    conn.close()


class TestPipelineCliSkipExport(unittest.TestCase):
    def test_skip_export_trains_from_existing_csvs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            db_path = os.path.join(tmp, "focoflow.db")
            export_dir = os.path.join(tmp, "export")
            _create_aligned_test_db(db_path)

            from ml.sqlite_export import export_training_csvs

            export_training_csvs(db_path, export_dir)

            exit_code = run_pipeline(
                db_path=db_path,
                output_dir=export_dir,
                session_id=None,
                backend="majority",
                label_window_seconds=300,
                n_splits=2,
                skip_train=False,
                skip_export=True,
            )

            self.assertEqual(exit_code, 0)
            self.assertTrue(os.path.isfile(os.path.join(export_dir, "model.json")))
            self.assertTrue(os.path.isfile(os.path.join(export_dir, "metrics.json")))

    def test_skip_export_fails_when_csvs_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            exit_code = run_pipeline(
                db_path=os.path.join(tmp, "missing.db"),
                output_dir=tmp,
                session_id=None,
                backend="majority",
                label_window_seconds=300,
                n_splits=2,
                skip_train=False,
                skip_export=True,
            )
            self.assertEqual(exit_code, 1)


if __name__ == "__main__":
    unittest.main()
