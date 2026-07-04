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


def export_xgboost_to_onnx(model_path: str, output_path: str) -> None:
    try:
        import xgboost as xgb
        from skl2onnx import convert_sklearn
        from skl2onnx.common.data_types import FloatTensorType
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError(
            "Install xgboost and skl2onnx to export ONNX: pip install xgboost skl2onnx onnx"
        ) from exc

    model = xgb.XGBClassifier()
    model.load_model(model_path)
    n_features = len(default_feature_columns())
    initial_type = [("input", FloatTensorType([None, n_features]))]
    onnx_model = convert_sklearn(model, initial_types=initial_type)
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as handle:
        handle.write(onnx_model.SerializeToString())


def try_export_onnx(model_path: str, output_path: str) -> tuple[bool, str]:
    if not os.path.exists(model_path):
        return False, f"model not found: {model_path}"
    if is_majority_stub(model_path):
        return False, "skipped ONNX export (majority stub model)"
    try:
        export_xgboost_to_onnx(model_path, output_path)
    except RuntimeError as exc:
        return False, str(exc)
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
