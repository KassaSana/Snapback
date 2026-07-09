"""Generate fixtures/model.onnx for Rust ONNX integration tests.

Creates a tiny linear model: 31 features -> 4 class logits.
Output biases favor PRODUCTIVE (index 2) so tests can assert stable behavior.

Usage (from repo root):

    pip install onnx numpy
    python tools/generate_onnx_fixture.py
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

try:
    import onnx
    from onnx import TensorProto, helper
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Install onnx and numpy: pip install onnx numpy") from exc

N_FEATURES = 31
N_CLASSES = 4


def build_fixture_model() -> onnx.ModelProto:
    input_info = helper.make_tensor_value_info(
        "input",
        TensorProto.FLOAT,
        [None, N_FEATURES],
    )
    output_info = helper.make_tensor_value_info(
        "output",
        TensorProto.FLOAT,
        [None, N_CLASSES],
    )

    weights = np.zeros((N_FEATURES, N_CLASSES), dtype=np.float32)
    weights[4, 2] = 0.5  # keystroke_count nudges productive logit
    bias = np.array([0.1, 0.15, 0.55, 0.2], dtype=np.float32)

    weight_tensor = helper.make_tensor(
        "weights",
        TensorProto.FLOAT,
        list(weights.shape),
        weights.flatten().tolist(),
    )
    bias_tensor = helper.make_tensor(
        "bias",
        TensorProto.FLOAT,
        list(bias.shape),
        bias.flatten().tolist(),
    )

    gemm = helper.make_node(
        "Gemm",
        inputs=["input", "weights", "bias"],
        outputs=["output"],
        alpha=1.0,
        beta=1.0,
        transB=0,
    )

    graph = helper.make_graph(
        [gemm],
        "snapback_fixture",
        [input_info],
        [output_info],
        [weight_tensor, bias_tensor],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    onnx.checker.check_model(model)
    return model


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate ONNX fixture for Rust tests")
    parser.add_argument(
        "--output",
        default="fixtures/model.onnx",
        help="Output path for model.onnx",
    )
    args = parser.parse_args()

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    model = build_fixture_model()
    onnx.save(model, output)
    print(f"wrote {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
