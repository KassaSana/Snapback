"""
Inspect a trained baseline model — feature importances for heuristic tuning.
"""

from __future__ import annotations

import json
from typing import Dict, List, Optional

from .training_pipeline import default_feature_columns


def load_metrics(path: str) -> Dict[str, float]:
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    return {str(k): float(v) for k, v in payload.items()}


def feature_importance(model_path: str) -> List[tuple[str, float]]:
    columns = default_feature_columns()

    try:
        with open(model_path, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
        if isinstance(payload, dict) and payload.get("type") == "majority":
            return []
    except (OSError, json.JSONDecodeError):
        pass

    try:
        import xgboost as xgb
    except ImportError as exc:
        raise RuntimeError("Install xgboost to analyze model importances.") from exc

    model = xgb.XGBClassifier()
    model.load_model(model_path)
    scores = model.feature_importances_
    pairs = list(zip(columns, scores))
    pairs.sort(key=lambda item: item[1], reverse=True)
    return pairs


def print_analysis(
    model_path: Optional[str] = None,
    metrics_path: Optional[str] = None,
    top_n: int = 10,
) -> None:
    if metrics_path:
        metrics = load_metrics(metrics_path)
        print("Training metrics:")
        for key, value in metrics.items():
            print(f"  {key}: {value:.4f}")
        print()

    if not model_path:
        return

    pairs = feature_importance(model_path)
    if not pairs:
        print("Feature importances unavailable for this model type.")
        return
    print(f"Top {top_n} feature importances:")
    for name, score in pairs[:top_n]:
        print(f"  {name}: {score:.4f}")

    print("\nHeuristic tuning hints:")
    for name, score in pairs[:5]:
        if name.startswith("context_switch") or name == "unique_apps_5min":
            print(f"  - thrash_score: {name} ranked high ({score:.3f})")
        elif name.startswith("keystroke_interval"):
            print(f"  - drift_score: {name} ranked high ({score:.3f})")
        elif name == "time_in_current_app":
            print(f"  - deep_work_score: {name} ranked high ({score:.3f})")
        elif name.startswith("is_"):
            print(f"  - app_context / classifier: {name} ranked high ({score:.3f})")
