import json
import tempfile
import unittest
from pathlib import Path

from ml.feature_parity_cli import check_expectations, compare_exported_rust_results


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


if __name__ == "__main__":
    unittest.main()
