"""Evaluate ONNX classifier quality from labeled CSV (Python/onnxruntime)."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

from .training_pipeline import (
    load_dataset,
    precision_at_k,
    recall_for_class,
)


STATE_LABELS = ["DISTRACTED", "PSEUDO_PRODUCTIVE", "PRODUCTIVE", "DEEP_FOCUS"]


@dataclass
class ClassifierEval:
    backend: str
    samples: int
    accuracy: float
    precision_at_10pct_distracted: float
    recall_distracted: float


def _predictions_to_indices(probas: List[List[float]]) -> List[int]:
    return [max(range(len(row)), key=row.__getitem__) for row in probas]


def _extract_probas_from_onnx_outputs(session, outputs_raw) -> List[List[float]]:
    import numpy as np

    for meta, raw in zip(session.get_outputs(), outputs_raw):
        if meta.name == "probabilities":
            return [list(map(float, row)) for row in raw]
        if isinstance(raw, np.ndarray) and raw.ndim == 2 and raw.shape[1] == 4:
            return [list(map(float, row)) for row in raw]

    for raw in outputs_raw:
        if hasattr(raw, "ndim") and raw.ndim == 2 and raw.shape[1] == 4:
            return [list(map(float, row)) for row in raw]

    raise RuntimeError("ONNX model did not return a (N, 4) probability tensor")


def evaluate_onnx_model(labeled_csv: str, model_onnx: str) -> ClassifierEval:
    try:
        import onnxruntime as ort
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("Install onnxruntime: pip install onnxruntime") from exc

    dataset = load_dataset(labeled_csv)
    if not dataset.features:
        raise RuntimeError(f"no labeled rows in {labeled_csv}")

    session = ort.InferenceSession(model_onnx, providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    outputs = session.run(None, {input_name: dataset.features})
    probas = _extract_probas_from_onnx_outputs(session, outputs)

    predictions = _predictions_to_indices(probas)
    labels = dataset.labels
    accuracy = sum(1 for pred, label in zip(predictions, labels) if pred == label) / len(labels)

    return ClassifierEval(
        backend="onnx_python",
        samples=len(labels),
        accuracy=accuracy,
        precision_at_10pct_distracted=precision_at_k(probas, labels, target_index=0, k_fraction=0.1),
        recall_distracted=recall_for_class(probas, labels, target_index=0, threshold=0.7),
    )


def evaluate_xgboost_model(labeled_csv: str, model_json: str) -> ClassifierEval:
    try:
        import xgboost as xgb
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("Install xgboost") from exc

    dataset = load_dataset(labeled_csv)
    booster = xgb.Booster()
    booster.load_model(model_json)
    import numpy as np

    matrix = np.asarray(dataset.features, dtype=np.float32)
    probas_raw = booster.predict(xgb.DMatrix(matrix))
    if probas_raw.ndim == 1:
        # binary edge case
        probas = [[1.0 - float(p), float(p)] for p in probas_raw]
    else:
        probas = [list(map(float, row)) for row in probas_raw]

    predictions = _predictions_to_indices(probas)
    labels = dataset.labels
    accuracy = sum(1 for pred, label in zip(predictions, labels) if pred == label) / len(labels)

    return ClassifierEval(
        backend="xgboost",
        samples=len(labels),
        accuracy=accuracy,
        precision_at_10pct_distracted=precision_at_k(probas, labels, target_index=0, k_fraction=0.1),
        recall_distracted=recall_for_class(probas, labels, target_index=0, threshold=0.7),
    )


def eval_to_dict(eval_result: ClassifierEval) -> Dict[str, float | int | str]:
    return {
        "backend": eval_result.backend,
        "samples": eval_result.samples,
        "accuracy": round(eval_result.accuracy, 4),
        "precision_at_10pct_distracted": round(eval_result.precision_at_10pct_distracted, 4),
        "recall_distracted": round(eval_result.recall_distracted, 4),
    }
