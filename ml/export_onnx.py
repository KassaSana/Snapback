"""
Export a trained XGBoost model to ONNX for the Rust runtime (optional `onnx` feature).

Usage:
  python -m ml.export_onnx --model-path artifacts/model.json --output artifacts/model.onnx
"""

from __future__ import annotations

import argparse
import json
import os
import sys

from .training_pipeline import default_feature_columns


def is_majority_stub(model_path: str) -> bool:
    try:
        with open(model_path, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except (json.JSONDecodeError, OSError):
        return False
    return isinstance(payload, dict) and payload.get("type") == "majority"


def export_xgboost_to_onnx(model_path: str, output_path: str, n_features: int | None = None) -> None:
    try:
        import onnx
        import xgboost as xgb
        from onnxmltools.convert import convert_xgboost
        from onnxmltools.convert.common.data_types import FloatTensorType
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError(
            "Install ONNX export deps: pip install xgboost onnx onnxmltools"
        ) from exc

    feature_count = n_features or len(default_feature_columns())
    booster = xgb.Booster()
    booster.load_model(model_path)
    initial_type = [("input", FloatTensorType([None, feature_count]))]
    onnx_model = convert_xgboost(booster, initial_types=initial_type, target_opset=13)
    onnx.checker.check_model(onnx_model)

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    onnx.save(onnx_model, output_path)


def try_export_onnx(model_path: str, output_path: str) -> tuple[bool, str]:
    if not os.path.exists(model_path):
        return False, f"model not found: {model_path}"
    if is_majority_stub(model_path):
        return False, "skipped ONNX export (majority stub model)"
    try:
        export_xgboost_to_onnx(model_path, output_path)
    except RuntimeError as exc:
        return False, str(exc)
    except Exception as exc:  # pragma: no cover - library-specific failures
        return False, f"ONNX export failed: {exc}"
    return True, f"exported ONNX model to {output_path}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Export Snapback model to ONNX")
    parser.add_argument("--model-path", required=True)
    parser.add_argument("--output", default="artifacts/model.onnx")
    args = parser.parse_args()

    exported, message = try_export_onnx(args.model_path, args.output)
    if not exported:
        print(message, file=sys.stderr)
        sys.exit(2 if is_majority_stub(args.model_path) else 1)
    print(message)


if __name__ == "__main__":
    main()
