import unittest

from tools.benchmark_classifier_quality import evaluate_quality_gate


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


if __name__ == "__main__":
    unittest.main()
