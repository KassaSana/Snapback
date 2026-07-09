"""
`FocusLabel`, `LabelSource`, and `LabelRecord` are the production label
contract shared across the ML pipeline (training, dataset joins, exports) —
the real app records labels via Rust/SQLite (`storage/mod.rs`), and those
three types mirror that schema.

`Labeler` and `SessionMetadata` below are an in-memory stub predating the
SQLite-backed labeling flow; nothing in the live pipeline uses them, only
`tests/test_labeling.py`. Safe to delete if that test is retired too.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import List, Optional


class FocusLabel(IntEnum):
    DISTRACTED = -1
    PSEUDO_PRODUCTIVE = 0
    PRODUCTIVE = 1
    DEEP_FOCUS = 2


class LabelSource(IntEnum):
    HOTKEY = 1
    AUTO = 2
    TIMER = 2
    SURVEY = 3
    MANUAL = 4


@dataclass(frozen=True)
class LabelRecord:
    timestamp: float
    label: FocusLabel
    source: LabelSource
    session_id: str
    notes: str = ""


@dataclass
class SessionMetadata:
    session_id: str
    goal: str
    start_ts: Optional[float] = None
    end_ts: Optional[float] = None


class Labeler:
    def __init__(self, session_id: str, goal: str) -> None:
        self.session = SessionMetadata(session_id=session_id, goal=goal)
        self._labels: List[LabelRecord] = []

    @property
    def labels(self) -> List[LabelRecord]:
        return list(self._labels)

    def start_session(self, timestamp: float) -> None:
        self.session.start_ts = timestamp

    def end_session(self, timestamp: float) -> None:
        self.session.end_ts = timestamp

    def add_label(
        self,
        label: FocusLabel,
        source: LabelSource,
        timestamp: float,
        notes: str = "",
    ) -> None:
        record = LabelRecord(
            timestamp=timestamp,
            label=label,
            source=source,
            session_id=self.session.session_id,
            notes=notes,
        )
        self._labels.append(record)
