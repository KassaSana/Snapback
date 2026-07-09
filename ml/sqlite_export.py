"""
Export training CSVs from the Snapback local SQLite database (focoflow.db).

The desktop app stores feature snapshots (~1/sec during sessions) and manual
labels. This module reads those tables and writes CSV files compatible with
ml.train_cli and ml.dataset_builder.
"""

from __future__ import annotations

import csv
import os
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Optional, Sequence

from .training_pipeline import default_feature_columns

FEATURE_COLUMNS = ["timestamp", *default_feature_columns()]
LABEL_HEADERS = ["timestamp", "label", "source", "session_id", "notes"]


@dataclass(frozen=True)
class ExportCounts:
    features: int
    labels: int


def default_app_db_path() -> Path:
    """Best-effort path to focoflow.db for the installed Snapback app."""
    home = Path.home()
    if os.name == "nt":
        base = os.environ.get("APPDATA", home / "AppData" / "Roaming")
        return Path(base) / "com.snapback.app" / "focoflow.db"
    if os.name == "darwin":
        return home / "Library" / "Application Support" / "com.snapback.app" / "focoflow.db"
    return home / ".local" / "share" / "com.snapback.app" / "focoflow.db"


def _rfc3339_to_unix(value: str) -> float:
    normalized = value.strip().replace("Z", "+00:00")
    dt = datetime.fromisoformat(normalized)
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.timestamp()


def _table_exists(conn: sqlite3.Connection, name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?",
        (name,),
    ).fetchone()
    return row is not None


def export_features_csv(
    db_path: str,
    output_path: str,
    session_id: Optional[str] = None,
) -> int:
    conn = sqlite3.connect(db_path)
    try:
        if not _table_exists(conn, "feature_snapshots"):
            raise RuntimeError(
                "feature_snapshots table not found. Run a session on a build that "
                "persists feature vectors, then retry."
            )

        query = f"""
            SELECT {", ".join(FEATURE_COLUMNS)}
            FROM feature_snapshots
        """
        params: Sequence[str] = ()
        if session_id:
            query += " WHERE session_id = ?"
            params = (session_id,)
        query += " ORDER BY timestamp ASC"

        rows = conn.execute(query, params).fetchall()
    finally:
        conn.close()

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(FEATURE_COLUMNS)
        for row in rows:
            writer.writerow(row)
    return len(rows)


def export_labels_csv(
    db_path: str,
    output_path: str,
    session_id: Optional[str] = None,
) -> int:
    conn = sqlite3.connect(db_path)
    try:
        if not _table_exists(conn, "labels"):
            raise RuntimeError("labels table not found in database.")

        query = """
            SELECT timestamp, label, source, session_id, notes
            FROM labels
        """
        params: Sequence[str] = ()
        if session_id:
            query += " WHERE session_id = ?"
            params = (session_id,)
        query += " ORDER BY timestamp ASC"

        raw_rows = conn.execute(query, params).fetchall()
    finally:
        conn.close()

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(LABEL_HEADERS)
        count = 0
        for timestamp_raw, label, source, sid, notes in raw_rows:
            writer.writerow(
                [
                    f"{_rfc3339_to_unix(str(timestamp_raw)):.6f}",
                    int(label),
                    str(source or "manual").upper(),
                    sid,
                    notes or "",
                ]
            )
            count += 1
    return count


def export_training_csvs(
    db_path: str,
    output_dir: str,
    session_id: Optional[str] = None,
) -> ExportCounts:
    os.makedirs(output_dir, exist_ok=True)
    features_path = os.path.join(output_dir, "features.csv")
    labels_path = os.path.join(output_dir, "labels.csv")
    feature_count = export_features_csv(db_path, features_path, session_id=session_id)
    label_count = export_labels_csv(db_path, labels_path, session_id=session_id)
    return ExportCounts(features=feature_count, labels=label_count)
