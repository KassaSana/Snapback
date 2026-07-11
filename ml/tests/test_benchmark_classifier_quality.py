import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from subprocess import TimeoutExpired
from unittest.mock import patch

from tools.benchmark_classifier_quality import (
    evaluate_quality_gate,
    onnx_eval_is_production_aligned,
    run_command,
)


class TestBenchmarkClassifierQuality(unittest.TestCase):
    def test_quality_gate_accepts_report_above_floors(self) -> None:
        report = {
            "classifier_quality": {
                "heuristic_rust": {"recall_distracted": 0.0},
                "onnx": {
                    "accuracy": 0.61,
                    "precision_at_10pct_distracted": 0.53,
                    "recall_distracted": 0.40,
                },
                "xgboost_training_metrics": {
                    "cv_accuracy": 0.10,
                    "precision_at_10pct": 0.53,
                    "recall_distracted": 0.40,
                },
            }
        }

        failures = evaluate_quality_gate(
            report,
            min_cv_accuracy=0.55,
            min_cv_precision_at_10pct=0.45,
            min_cv_recall_distracted=0.30,
            min_recall_lift=0.20,
        )

        self.assertEqual(failures, [])

    def test_quality_gate_reports_regressions(self) -> None:
        report = {
            "classifier_quality": {
                "heuristic_rust": {"recall_distracted": 0.15},
                "onnx": {
                    "accuracy": 0.50,
                    "precision_at_10pct_distracted": 0.40,
                    "recall_distracted": 0.25,
                },
                "xgboost_training_metrics": {
                    "cv_accuracy": 0.90,
                    "precision_at_10pct": 0.40,
                    "recall_distracted": 0.25,
                },
            }
        }

        failures = evaluate_quality_gate(
            report,
            min_cv_accuracy=0.55,
            min_cv_precision_at_10pct=0.45,
            min_cv_recall_distracted=0.30,
            min_recall_lift=0.20,
        )

        self.assertEqual(len(failures), 4)
        self.assertTrue(any("runtime_accuracy" in failure for failure in failures))
        self.assertTrue(any("runtime_precision_at_10pct" in failure for failure in failures))
        self.assertTrue(any("runtime_recall_distracted" in failure for failure in failures))
        self.assertTrue(any("runtime recall lift vs heuristic" in failure for failure in failures))


    def test_production_aligned_flag_true_for_rust_onnx_eval(self) -> None:
        report = {"classifier_quality": {"onnx": {"production_aligned": True}}}
        self.assertTrue(onnx_eval_is_production_aligned(report))

    def test_production_aligned_flag_false_for_raw_model_fallback(self) -> None:
        report = {"classifier_quality": {"onnx": {"production_aligned": False}}}
        self.assertFalse(onnx_eval_is_production_aligned(report))

    def test_production_aligned_flag_defaults_false_when_absent(self) -> None:
        # A report missing the flag (e.g. an older run) must not be mistaken
        # for production-aligned.
        report = {"classifier_quality": {"onnx": {"accuracy": 0.9}}}
        self.assertFalse(onnx_eval_is_production_aligned(report))

    @patch("tools.benchmark_classifier_quality.subprocess.run")
    def test_run_command_turns_timeout_into_runtime_error(self, run_mock) -> None:
        run_mock.side_effect = TimeoutExpired(
            cmd=["cargo", "run"],
            timeout=5,
            output="partial stdout",
            stderr="partial stderr",
        )

        stdout = StringIO()
        stderr = StringIO()

        with redirect_stdout(stdout), redirect_stderr(stderr):
            with self.assertRaisesRegex(RuntimeError, "command timed out after 5s"):
                run_command(["cargo", "run"], cwd=Path("."), timeout_secs=5)

        self.assertEqual(stdout.getvalue(), "partial stdout")
        self.assertEqual(stderr.getvalue(), "partial stderr")


if __name__ == "__main__":
    unittest.main()
