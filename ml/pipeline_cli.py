"""
End-to-end offline pipeline: export app SQLite → join → train → analyze.

Usage (from repo root):

  python -m ml.pipeline_cli --db-path ~/Library/Application\\ Support/com.snapback.app/focoflow.db

Or omit --db-path to use the default Snapback app data location.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import List, Optional

from .analyze_model import print_analysis
from .sqlite_export import default_app_db_path, export_training_csvs
from .train_cli import run_training


def run_pipeline(
    db_path: str,
    output_dir: str,
    session_id: Optional[str],
    backend: str,
    label_window_seconds: int,
    n_splits: int,
    skip_train: bool,
    skip_export: bool = False,
) -> int:
    os.makedirs(output_dir, exist_ok=True)
    features_path = os.path.join(output_dir, "features.csv")
    labels_path = os.path.join(output_dir, "labels.csv")

    if skip_export:
        if not os.path.isfile(features_path):
            print(f"features.csv not found in {output_dir}", file=sys.stderr)
            return 1
        if not os.path.isfile(labels_path):
            print(f"labels.csv not found in {output_dir}", file=sys.stderr)
            return 1
        feature_count = _count_csv_rows(features_path)
        label_count = _count_csv_rows(labels_path)
        print(
            f"Using exported CSVs in {output_dir} "
            f"({feature_count} features, {label_count} labels)"
        )
    else:
        db = os.path.expanduser(db_path)
        if not os.path.isfile(db):
            print(f"Database not found: {db}", file=sys.stderr)
            print("Use the app for a session first, or pass --db-path explicitly.", file=sys.stderr)
            return 1

        counts = export_training_csvs(db, output_dir, session_id=session_id)
        print(f"Exported {counts.features} feature rows and {counts.labels} labels to {output_dir}")
        feature_count = counts.features
        label_count = counts.labels

    if feature_count == 0:
        print("No feature snapshots found. Start a session in the desktop app and try again.")
        return 1

    if label_count == 0:
        print("No labels found. Tap feedback buttons in the app, then re-run export.")
        return 1

    if skip_train:
        return 0

    dataset_path = os.path.join(output_dir, "labeled.csv")
    model_path = os.path.join(output_dir, "model.json")
    metrics_path = os.path.join(output_dir, "metrics.json")

    metrics = run_training(
        dataset_path=None,
        features_path=features_path,
        labels_path=labels_path,
        output_dataset_path=dataset_path,
        output_model_path=model_path,
        output_metrics_path=metrics_path,
        label_window_seconds=label_window_seconds,
        backend=backend,
        n_splits=n_splits,
        feature_columns=None,
        label_column="label",
    )

    print("\nTraining metrics:")
    for key, value in metrics.items():
        print(f"  {key}: {value:.4f}")

    print_analysis(model_path=model_path, metrics_path=metrics_path)
    print(f"\nArtifacts written to {output_dir}")
    return 0


def _count_csv_rows(path: str) -> int:
    with open(path, "r", encoding="utf-8") as handle:
        total = sum(1 for _ in handle)
    return max(0, total - 1)


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export Snapback SQLite data and train a baseline focus model.",
    )
    parser.add_argument(
        "--db-path",
        default=str(default_app_db_path()),
        help="Path to focoflow.db (default: Snapback app data dir).",
    )
    parser.add_argument(
        "--output-dir",
        default="artifacts/training",
        help="Directory for exported CSVs and model artifacts.",
    )
    parser.add_argument("--session-id", default=None, help="Limit export to one session.")
    parser.add_argument("--backend", choices=["auto", "xgboost", "majority"], default="auto")
    parser.add_argument("--label-window-seconds", type=int, default=300)
    parser.add_argument("--splits", type=int, default=5)
    parser.add_argument(
        "--export-only",
        action="store_true",
        help="Export CSVs only; do not train.",
    )
    parser.add_argument(
        "--skip-export",
        action="store_true",
        help="Train from features.csv and labels.csv already in --output-dir.",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    if args.skip_export and args.export_only:
        print("Choose either --export-only or --skip-export, not both.", file=sys.stderr)
        return 1
    return run_pipeline(
        db_path=args.db_path,
        output_dir=args.output_dir,
        session_id=args.session_id,
        backend=args.backend,
        label_window_seconds=args.label_window_seconds,
        n_splits=args.splits,
        skip_train=args.export_only,
        skip_export=args.skip_export,
    )


if __name__ == "__main__":
    raise SystemExit(main())
