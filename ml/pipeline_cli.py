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
) -> int:
    db = os.path.expanduser(db_path)
    if not os.path.isfile(db):
        print(f"Database not found: {db}", file=sys.stderr)
        print("Use the app for a session first, or pass --db-path explicitly.", file=sys.stderr)
        return 1

    os.makedirs(output_dir, exist_ok=True)
    counts = export_training_csvs(db, output_dir, session_id=session_id)
    print(f"Exported {counts.features} feature rows and {counts.labels} labels to {output_dir}")

    if counts.features == 0:
        print("No feature snapshots found. Start a session in the desktop app and try again.")
        return 1

    if counts.labels == 0:
        print("No labels found. Tap feedback buttons in the app, then re-run export.")
        return 1

    if skip_train:
        return 0

    features_path = os.path.join(output_dir, "features.csv")
    labels_path = os.path.join(output_dir, "labels.csv")
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
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    return run_pipeline(
        db_path=args.db_path,
        output_dir=args.output_dir,
        session_id=args.session_id,
        backend=args.backend,
        label_window_seconds=args.label_window_seconds,
        n_splits=args.splits,
        skip_train=args.export_only,
    )


if __name__ == "__main__":
    raise SystemExit(main())
