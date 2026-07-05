"""Replay shared feature-parity scenarios through the Python extractor."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, List

from .event_schema import EventRecord, EventType
from .features import FeatureExtractor, FeatureVector, IDLE_STRUCT, MOUSE_MOVE_STRUCT

EVENT_TYPE_BY_NAME = {
    "key_press": EventType.KEY_PRESS,
    "key_release": EventType.KEY_RELEASE,
    "mouse_move": EventType.MOUSE_MOVE,
    "mouse_click": EventType.MOUSE_CLICK,
    "window_focus_change": EventType.WINDOW_FOCUS_CHANGE,
    "window_title_change": EventType.WINDOW_TITLE_CHANGE,
    "idle_start": EventType.IDLE_START,
    "idle_end": EventType.IDLE_END,
}


def training_column_values(features: FeatureVector) -> Dict[str, float]:
    return {
        "timestamp": features.timestamp,
        "seconds_since_session_start": float(features.seconds_since_session_start),
        "hour_of_day": float(features.hour_of_day),
        "day_of_week": float(features.day_of_week),
        "minutes_since_last_break": float(features.minutes_since_last_break),
        "keystroke_count": float(features.keystroke_count),
        "keystroke_rate": features.keystroke_rate,
        "keystroke_interval_mean": features.keystroke_interval_mean,
        "keystroke_interval_std": features.keystroke_interval_std,
        "keystroke_interval_trend": features.keystroke_interval_trend,
        "mouse_move_count": float(features.mouse_move_count),
        "mouse_distance_pixels": features.mouse_distance_pixels,
        "mouse_speed_mean": features.mouse_speed_mean,
        "mouse_speed_std": features.mouse_speed_std,
        "mouse_acceleration_mean": features.mouse_acceleration_mean,
        "mouse_click_count": float(features.mouse_click_count),
        "context_switches_30s": float(features.context_switches_30s),
        "context_switches_5min": float(features.context_switches_5min),
        "time_in_current_app": float(features.time_in_current_app),
        "unique_apps_5min": float(features.unique_apps_5min),
        "idle_time_30s": features.idle_time_30s,
        "idle_event_count_5min": float(features.idle_event_count_5min),
        "longest_active_stretch_5min": float(features.longest_active_stretch_5min),
        "window_title_length": float(features.window_title_length),
        "window_title_changed_30s": float(int(features.window_title_changed_30s)),
        "is_browser": float(int(features.is_browser)),
        "is_ide": float(int(features.is_ide)),
        "is_communication": float(int(features.is_communication)),
        "is_entertainment": float(int(features.is_entertainment)),
        "is_productivity": float(int(features.is_productivity)),
        "focus_momentum": features.focus_momentum,
        "is_pseudo_productive": float(int(features.is_pseudo_productive)),
    }


def scenario_event_to_record(base_time: float, event: dict) -> EventRecord:
    event_type = EVENT_TYPE_BY_NAME.get(str(event.get("type", "")))
    if event_type is None:
        raise ValueError(f"unsupported scenario event type: {event.get('type')}")

    timestamp_us = int((base_time + float(event["offset_secs"])) * 1_000_000)
    data_raw = b"\x00" * 16
    if event_type == EventType.MOUSE_MOVE:
        speed = int(event.get("mouse_speed", 0))
        data_raw = MOUSE_MOVE_STRUCT.pack(0, 0, speed)
    elif event_type in (EventType.IDLE_START, EventType.IDLE_END):
        idle_ms = int(event.get("idle_duration_ms", 0))
        data_raw = IDLE_STRUCT.pack(idle_ms)

    return EventRecord(
        timestamp_us=timestamp_us,
        event_type=event_type,
        process_id=0,
        app_name=str(event.get("app", "")),
        window_handle=0,
        data_raw=data_raw.ljust(16, b"\x00")[:16],
        reserved=0,
        window_title=str(event.get("title", "")),
    )


def replay_scenario(scenario: dict) -> FeatureVector:
    extractor = FeatureExtractor()
    base_time = float(scenario["base_time"])

    for raw_event in scenario.get("events", []):
        event = scenario_event_to_record(base_time, raw_event)
        extractor.update(event)

    if not scenario.get("events"):
        return extractor.extract_features(base_time)

    last_offset = float(scenario["events"][-1]["offset_secs"])
    return extractor.extract_features(base_time + last_offset)


def run_python_scenarios(scenarios_path: Path) -> List[dict]:
    with open(scenarios_path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)

    results: List[dict] = []
    for scenario in payload.get("scenarios", []):
        features = training_column_values(replay_scenario(scenario))
        results.append({"name": scenario["name"], "features": features})
    return results
