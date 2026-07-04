"""Generate a synthetic focoflow.db and exported training CSVs.

This exists to validate the SQLite -> CSV -> train_cli -> analyze_model
pipeline end to end (schema compatibility, join logic, feature importance)
*without* real personal usage data. It is not a substitute for real labels
collected from actually using the app (see README's "Offline ML" section) --
treat any model trained on this data as a pipeline smoke test, not a usable
personal classifier.

Builds a SQLite database with the same schema Storage::init_schema creates in
src-tauri/src/storage/mod.rs (sessions, feature_snapshots, labels), fills it
with four archetypal focus sessions (deep focus, distracted, drift / pseudo
productive, productive), and exports features.csv + labels.csv the same way
ml/sqlite_export.py does for a real app database.

Usage (from repo root):

    python3 -m tools.generate_synthetic_training_data
    python3 -m tools.generate_synthetic_training_data --output-dir data --seed 7
"""

from __future__ import annotations

import argparse
import os
import random
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import List, Optional, Tuple

from ml.features import _classify_app
from ml.sqlite_export import export_training_csvs

DEFAULT_DB_PATH = os.path.join("data", "synthetic_focoflow.db")
DEFAULT_OUTPUT_DIR = "data"

# Matches FocusLabel in src-tauri/src/types.rs and ml/training_pipeline.py.
DISTRACTED, PSEUDO_PRODUCTIVE, PRODUCTIVE, DEEP_FOCUS = -1, 0, 1, 2

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
    session_id TEXT PRIMARY KEY,
    goal TEXT NOT NULL,
    status TEXT NOT NULL,
    focus_mode TEXT NOT NULL DEFAULT 'normal',
    started_at TEXT NOT NULL,
    ended_at TEXT
);

CREATE TABLE IF NOT EXISTS labels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    label INTEGER NOT NULL,
    source TEXT NOT NULL DEFAULT 'manual',
    notes TEXT,
    timestamp TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS feature_snapshots (
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
"""


@dataclass(frozen=True)
class SessionArchetype:
    session_id: str
    goal: str
    label: int
    duration_seconds: int
    app_sequence: List[Tuple[int, str]]  # (switch_at_second, app_name)
    keystroke_rate_range: Tuple[float, float]
    keystroke_interval_std_range: Tuple[float, float]
    idle_time_30s_range: Tuple[float, float]
    focus_momentum_range: Tuple[float, float]
    context_switch_bonus_30s: int  # extra churn layered on top of app_sequence
    warmup_label: Optional[int] = None  # label used for the first ~20% of the session


ARCHETYPES: List[SessionArchetype] = [
    SessionArchetype(
        session_id="synthetic-deep-focus",
        goal="Ship the feature parity fix",
        label=DEEP_FOCUS,
        duration_seconds=300,
        app_sequence=[(0, "Cursor")],
        keystroke_rate_range=(0.25, 0.55),
        keystroke_interval_std_range=(0.05, 0.20),
        idle_time_30s_range=(0.0, 0.0),
        focus_momentum_range=(0.75, 0.97),
        context_switch_bonus_30s=0,
        warmup_label=PRODUCTIVE,
    ),
    SessionArchetype(
        session_id="synthetic-distracted",
        goal="Write the quarterly report",
        label=DISTRACTED,
        duration_seconds=300,
        app_sequence=[
            (0, "Google Chrome"),
            (20, "Spotify"),
            (45, "Slack"),
            (70, "Google Chrome"),
            (95, "Spotify"),
            (130, "Cursor"),
            (150, "Google Chrome"),
            (180, "Slack"),
            (210, "Spotify"),
            (240, "Google Chrome"),
            (270, "Slack"),
        ],
        keystroke_rate_range=(0.0, 0.08),
        keystroke_interval_std_range=(0.4, 0.9),
        idle_time_30s_range=(2.0, 14.0),
        focus_momentum_range=(0.02, 0.22),
        context_switch_bonus_30s=1,
    ),
    SessionArchetype(
        session_id="synthetic-drift",
        goal="Research competitor pricing",
        label=PSEUDO_PRODUCTIVE,
        duration_seconds=300,
        app_sequence=[
            (0, "Google Chrome"),
            (110, "Notion"),
            (220, "Google Chrome"),
        ],
        keystroke_rate_range=(0.05, 0.18),
        keystroke_interval_std_range=(0.5, 1.1),
        idle_time_30s_range=(0.0, 3.0),
        focus_momentum_range=(0.28, 0.52),
        context_switch_bonus_30s=0,
    ),
    SessionArchetype(
        session_id="synthetic-productive",
        goal="Draft the design doc",
        label=PRODUCTIVE,
        duration_seconds=300,
        app_sequence=[(0, "Notion"), (150, "Figma"), (240, "Notion")],
        keystroke_rate_range=(0.15, 0.35),
        keystroke_interval_std_range=(0.15, 0.4),
        idle_time_30s_range=(0.0, 1.5),
        focus_momentum_range=(0.5, 0.72),
        context_switch_bonus_30s=0,
    ),
]


def _app_at(sequence: List[Tuple[int, str]], second: int) -> str:
    current = sequence[0][1]
    for switch_at, app in sequence:
        if second >= switch_at:
            current = app
        else:
            break
    return current


def _recent_switch_count(sequence: List[Tuple[int, str]], second: int, window: int) -> int:
    return sum(1 for switch_at, _ in sequence if switch_at != 0 and second - window < switch_at <= second)


def _rand_in(rng: random.Random, bounds: Tuple[float, float]) -> float:
    lo, hi = bounds
    if lo == hi:
        return lo
    return rng.uniform(lo, hi)


def build_feature_rows(
    archetype: SessionArchetype,
    start_ts: float,
    rng: random.Random,
) -> List[Tuple]:
    rows: List[Tuple] = []
    current_app_since = 0

    for second in range(0, archetype.duration_seconds, 5):
        app = _app_at(archetype.app_sequence, second)
        if any(switch_at == second for switch_at, _ in archetype.app_sequence):
            current_app_since = second

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app(app)
        timestamp = start_ts + second
        dt = datetime.fromtimestamp(timestamp, tz=timezone.utc)

        context_switches_30s = _recent_switch_count(
            archetype.app_sequence, second, 30
        ) + archetype.context_switch_bonus_30s
        context_switches_5min = _recent_switch_count(archetype.app_sequence, second, 300)
        unique_apps_5min = len(
            {a for switch_at, a in archetype.app_sequence if switch_at <= second}
        )

        keystroke_rate = max(0.0, _rand_in(rng, archetype.keystroke_rate_range))
        keystroke_count = round(keystroke_rate * 30)
        keystroke_interval_std = max(0.0, _rand_in(rng, archetype.keystroke_interval_std_range))
        idle_time_30s = max(0.0, _rand_in(rng, archetype.idle_time_30s_range))

        # Momentum ramps in over the session rather than starting maxed out.
        ramp = min(1.0, (second + 5) / max(60, archetype.duration_seconds * 0.3))
        lo, hi = archetype.focus_momentum_range
        focus_momentum = lo + (hi - lo) * ramp + rng.uniform(-0.03, 0.03)
        focus_momentum = max(0.0, min(1.0, focus_momentum))

        rows.append(
            (
                archetype.session_id,
                timestamp,
                second,
                dt.hour,
                dt.weekday(),
                second // 60,
                keystroke_count,
                round(keystroke_rate, 4),
                round(max(0.05, 1.0 / max(keystroke_rate, 0.05)), 4) if keystroke_rate > 0 else 0.0,
                round(keystroke_interval_std, 4),
                0.0,
                0,
                0.0,
                0.0,
                0.0,
                0.0,
                0,
                context_switches_30s,
                context_switches_5min,
                second - current_app_since,
                max(unique_apps_5min, 1),
                round(idle_time_30s, 2),
                1 if idle_time_30s > 0 else 0,
                min(300, second - current_app_since + 30),
                0,
                1 if second == 0 or any(sw == second for sw, _ in archetype.app_sequence) else 0,
                int(is_browser),
                int(is_ide),
                int(is_comm),
                int(is_ent),
                int(is_prod),
                round(focus_momentum, 4),
                0,
            )
        )

    return rows


def build_label_rows(
    archetype: SessionArchetype,
    start_ts: float,
    rng: random.Random,
) -> List[Tuple[str, int, str, str]]:
    """Simulate a user tapping a feedback hotkey every 60-90s."""
    rows = []
    second = 30
    warmup_end = archetype.duration_seconds * 0.2 if archetype.warmup_label is not None else 0
    while second < archetype.duration_seconds:
        label = archetype.label
        if second < warmup_end:
            label = archetype.warmup_label
        ts = datetime.fromtimestamp(start_ts + second, tz=timezone.utc).isoformat()
        rows.append((ts, label, "hotkey", archetype.session_id, ""))
        second += rng.randint(60, 90)
    return rows


def generate(db_path: str, seed: int = 7) -> Tuple[int, int]:
    rng = random.Random(seed)
    os.makedirs(os.path.dirname(db_path) or ".", exist_ok=True)
    if os.path.exists(db_path):
        os.remove(db_path)

    conn = sqlite3.connect(db_path)
    try:
        conn.executescript(SCHEMA)

        # Fixed anchor (not time.time()) so every run is reproducible and all
        # sessions land in the same hour_of_day/day_of_week bucket. Real capture
        # timestamps vary naturally; if synthetic sessions were spread across
        # different days/hours instead, day_of_week/hour_of_day would become a
        # perfect proxy for session identity and the model would "cheat" by
        # memorizing sessions instead of learning from behavioral features.
        anchor = datetime(2024, 3, 6, 10, 0, 0, tzinfo=timezone.utc)  # a Wednesday
        session_gap_seconds = max(a.duration_seconds for a in ARCHETYPES)
        session_start = anchor.timestamp()

        feature_count = 0
        label_count = 0
        for archetype in ARCHETYPES:
            started_at = datetime.fromtimestamp(session_start, tz=timezone.utc)
            ended_at = started_at + timedelta(seconds=archetype.duration_seconds)
            conn.execute(
                "INSERT INTO sessions (session_id, goal, status, focus_mode, started_at, ended_at) "
                "VALUES (?, ?, 'COMPLETED', 'normal', ?, ?)",
                (
                    archetype.session_id,
                    archetype.goal,
                    started_at.isoformat(),
                    ended_at.isoformat(),
                ),
            )

            feature_rows = build_feature_rows(archetype, session_start, rng)
            conn.executemany(
                """
                INSERT INTO feature_snapshots (
                    session_id, timestamp, seconds_since_session_start, hour_of_day, day_of_week,
                    minutes_since_last_break, keystroke_count, keystroke_rate, keystroke_interval_mean,
                    keystroke_interval_std, keystroke_interval_trend, mouse_move_count,
                    mouse_distance_pixels, mouse_speed_mean, mouse_speed_std, mouse_acceleration_mean,
                    mouse_click_count, context_switches_30s, context_switches_5min, time_in_current_app,
                    unique_apps_5min, idle_time_30s, idle_event_count_5min, longest_active_stretch_5min,
                    window_title_length, window_title_changed_30s, is_browser, is_ide, is_communication,
                    is_entertainment, is_productivity, focus_momentum, is_pseudo_productive
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                feature_rows,
            )
            feature_count += len(feature_rows)

            label_rows = build_label_rows(archetype, session_start, rng)
            conn.executemany(
                "INSERT INTO labels (timestamp, label, source, session_id, notes) VALUES (?, ?, ?, ?, ?)",
                [(ts, label, source, sid, notes) for ts, label, source, sid, notes in label_rows],
            )
            label_count += len(label_rows)

            session_start += session_gap_seconds

        conn.commit()
    finally:
        conn.close()

    return feature_count, label_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--db-path", default=DEFAULT_DB_PATH, help=f"Where to write the synthetic SQLite db (default: {DEFAULT_DB_PATH})")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, help=f"Where to write features.csv/labels.csv (default: {DEFAULT_OUTPUT_DIR})")
    parser.add_argument("--seed", type=int, default=7, help="Random seed for reproducibility")
    args = parser.parse_args()

    feature_count, label_count = generate(args.db_path, seed=args.seed)
    print(f"Wrote {feature_count} feature snapshots and {label_count} labels to {args.db_path}")

    counts = export_training_csvs(args.db_path, args.output_dir)
    print(
        f"Exported {counts.features} feature rows and {counts.labels} label rows to "
        f"{args.output_dir}/features.csv and {args.output_dir}/labels.csv"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
