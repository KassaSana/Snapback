import json
import tempfile
import unittest
from pathlib import Path

from ml.feature_parity_cli import (
    check_expectations,
    compare_exported_rust_results,
    compare_python_rust_results,
    default_scenarios_path,
    run_python_expectations,
)
from ml.parity import training_column_values
from ml.features import FeatureVector


class TestFeatureParity(unittest.TestCase):
    def test_check_expectations_exact_and_min(self) -> None:
        scenario = {
            "name": "demo",
            "expect": {
                "keystroke_count": 4,
                "time_in_current_app_min": 2,
            },
        }
        features = {
            "keystroke_count": 4.0,
            "time_in_current_app": 2.5,
        }
        self.assertEqual(check_expectations(scenario, features), [])

        bad = dict(features)
        bad["keystroke_count"] = 3.0
        errors = check_expectations(scenario, bad)
        self.assertTrue(any("keystroke_count" in err for err in errors))

    def test_compare_exported_rust_results(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "scenarios.json"
            path.write_text(
                json.dumps(
                    {
                        "scenarios": [
                            {
                                "name": "stable",
                                "expect": {"keystroke_count": 2},
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            failures = compare_exported_rust_results(
                path,
                [{"name": "stable", "features": {"keystroke_count": 2.0}}],
            )
            self.assertEqual(failures, [])

    def test_shared_scenarios_match_python_expectations(self) -> None:
        path = default_scenarios_path()
        if not path.is_file():
            self.skipTest(f"missing {path}")
        failures = run_python_expectations(path)
        self.assertEqual(failures, [], msg="\n".join(f.message for f in failures))

    def test_compare_python_rust_results_flags_drift(self) -> None:
        python_results = [{"name": "stable", "features": {"keystroke_count": 2.0}}]
        rust_results = [{"name": "stable", "features": {"keystroke_count": 3.0}}]
        failures = compare_python_rust_results(python_results, rust_results)
        self.assertTrue(any("keystroke_count" in failure.message for failure in failures))

    def test_training_column_values_matches_feature_vector(self) -> None:
        features = FeatureVector(
            timestamp=1700000001.0,
            seconds_since_session_start=1,
            hour_of_day=12,
            day_of_week=2,
            minutes_since_last_break=0,
            keystroke_count=4,
            keystroke_rate=0.25,
            keystroke_interval_mean=0.5,
            keystroke_interval_std=0.1,
            keystroke_interval_trend=0.0,
            mouse_move_count=1,
            mouse_distance_pixels=10.0,
            mouse_speed_mean=100.0,
            mouse_speed_std=0.0,
            mouse_acceleration_mean=0.0,
            mouse_click_count=0,
            context_switches_30s=0,
            context_switches_5min=0,
            time_in_current_app=1,
            unique_apps_5min=1,
            idle_time_30s=0.0,
            idle_event_count_5min=0,
            longest_active_stretch_5min=30,
            window_title_length=8,
            window_title_changed_30s=False,
            is_browser=False,
            is_ide=True,
            is_communication=False,
            is_entertainment=False,
            is_productivity=False,
            focus_momentum=0.0,
            productivity_category="Building",
            is_pseudo_productive=False,
            recent_event_sequence=None,
        )
        values = training_column_values(features)
        self.assertEqual(values["keystroke_count"], 4.0)
        self.assertEqual(values["is_ide"], 1.0)


if __name__ == "__main__":
    unittest.main()
