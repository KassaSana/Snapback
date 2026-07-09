import csv
import os
import tempfile
import unittest

from ml.dataset_builder import join_features_with_labels, read_features_csv, write_labeled_csv
from ml.features import FeatureVector, write_features_csv
from ml.labeling import FocusLabel, LabelRecord, LabelSource
from ml.train_cli import run_training


def make_feature(timestamp: float, keystrokes: int) -> FeatureVector:
    return FeatureVector(
        timestamp=timestamp,
        seconds_since_session_start=0,
        hour_of_day=9,
        day_of_week=1,
        minutes_since_last_break=0,
        keystroke_count=keystrokes,
        keystroke_rate=1.0,
        keystroke_interval_mean=0.5,
        keystroke_interval_std=0.1,
        keystroke_interval_trend=0.0,
        mouse_move_count=0,
        mouse_distance_pixels=0.0,
        mouse_speed_mean=0.0,
        mouse_speed_std=0.0,
        mouse_acceleration_mean=0.0,
        mouse_click_count=0,
        context_switches_30s=0,
        context_switches_5min=0,
        time_in_current_app=0,
        unique_apps_5min=1,
        idle_time_30s=0.0,
        idle_event_count_5min=0,
        longest_active_stretch_5min=10,
        window_title_length=10,
        window_title_changed_30s=False,
        is_browser=False,
        is_ide=True,
        is_communication=False,
        is_entertainment=False,
        is_productivity=False,
        focus_momentum=0.0,
        productivity_category="Building",
        is_pseudo_productive=False,
        recent_event_sequence=[1, 1, 1],
    )



# training_pipeline.MIN_TRAINING_SAMPLES requires at least 8 labeled rows
# (and at least 2 samples per label class) before it will train at all, so
# fixtures below use 8 feature/label pairs alternating between two classes.
def make_alternating_features_and_labels() -> tuple[list[FeatureVector], list[LabelRecord]]:
    features = [make_feature(10.0 * (i + 1), 5 + i) for i in range(8)]
    labels = [
        LabelRecord(
            10.0 * (i + 1) + 5.0,
            FocusLabel.PRODUCTIVE if i % 2 == 0 else FocusLabel.DISTRACTED,
            LabelSource.HOTKEY,
            "s1",
        )
        for i in range(8)
    ]
    return features, labels


class TestTrainCli(unittest.TestCase):
    def test_run_training_with_dataset(self) -> None:
        features, labels = make_alternating_features_and_labels()

        with tempfile.TemporaryDirectory() as tmp:
            feature_path = os.path.join(tmp, "features.csv")
            dataset_path = os.path.join(tmp, "dataset.csv")

            write_features_csv(feature_path, features)
            feature_rows = read_features_csv(feature_path)
            labeled_rows = join_features_with_labels(feature_rows, labels, label_window_seconds=20)
            write_labeled_csv(dataset_path, labeled_rows)

            model_path = os.path.join(tmp, "model.json")
            metrics_path = os.path.join(tmp, "metrics.json")
            metrics = run_training(
                dataset_path=dataset_path,
                features_path=None,
                labels_path=None,
                output_dataset_path=None,
                output_model_path=model_path,
                output_metrics_path=metrics_path,
                label_window_seconds=20,
                backend="majority",
                n_splits=2,
                feature_columns=None,
                label_column="label",
            )

            self.assertIn("precision_at_10pct", metrics)
            self.assertIn("recall_distracted", metrics)
            self.assertTrue(os.path.exists(model_path))
            self.assertTrue(os.path.exists(metrics_path))

    def test_run_training_with_features_and_labels(self) -> None:
        features, labels = make_alternating_features_and_labels()

        with tempfile.TemporaryDirectory() as tmp:
            feature_path = os.path.join(tmp, "features.csv")
            labels_path = os.path.join(tmp, "labels.csv")

            write_features_csv(feature_path, features)
            with open(labels_path, "w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["timestamp", "label", "source", "session_id", "notes"])
                for label in labels:
                    writer.writerow(
                        [label.timestamp, int(label.label), label.source.name, label.session_id, ""]
                    )

            model_path = os.path.join(tmp, "model.json")
            metrics_path = os.path.join(tmp, "metrics.json")
            metrics = run_training(
                dataset_path=None,
                features_path=feature_path,
                labels_path=labels_path,
                output_dataset_path=None,
                output_model_path=model_path,
                output_metrics_path=metrics_path,
                label_window_seconds=20,
                backend="majority",
                n_splits=2,
                feature_columns=None,
                label_column="label",
            )

            self.assertIn("precision_at_10pct", metrics)
            self.assertIn("recall_distracted", metrics)
            self.assertTrue(os.path.exists(model_path))
            self.assertTrue(os.path.exists(metrics_path))


if __name__ == "__main__":
    unittest.main()
