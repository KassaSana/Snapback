"""Train on synthetic data and benchmark heuristic vs ONNX classifier quality.

Usage (from repo root):

    py -m tools.benchmark_classifier_quality
    py -m tools.benchmark_classifier_quality --output-json data/benchmark_quality.json
"""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Optional

from ml.classifier_quality import evaluate_onnx_model, evaluate_xgboost_model, eval_to_dict

REPO_ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = REPO_ROOT / "data"
LABELED_CSV = DATA_DIR / "labeled.csv"
MODEL_ONNX = DATA_DIR / "model.onnx"
MODEL_JSON = DATA_DIR / "model.json"
METRICS_JSON = DATA_DIR / "metrics.json"
MANIFEST = REPO_ROOT / "src-tauri" / "Cargo.toml"
DEFAULT_MIN_CV_ACCURACY = 0.55
DEFAULT_MIN_CV_PRECISION_AT_10PCT = 0.45
DEFAULT_MIN_CV_RECALL_DISTRACTED = 0.30
DEFAULT_MIN_RECALL_LIFT = 0.20


@dataclass
class ClassifierEval:
    backend: str
    samples: int
    accuracy: float
    precision_at_10pct_distracted: float
    recall_distracted: float


def run_command(cmd: list[str], *, cwd: Path = REPO_ROOT) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        cmd,
        cwd=cwd,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(cmd)}")
    return completed


def parse_classifier_eval(output: str) -> ClassifierEval:
    fields: Dict[str, str] = {}
    for line in output.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip()

    return ClassifierEval(
        backend=fields.get("backend", "unknown"),
        samples=int(fields.get("samples", "0")),
        accuracy=float(fields.get("accuracy", "0")),
        precision_at_10pct_distracted=float(fields.get("precision_at_10pct_distracted", "0")),
        recall_distracted=float(fields.get("recall_distracted", "0")),
    )


def parse_bench_block(output: str, backend: str) -> Dict[str, str]:
    blocks = output.split("SNAPBACK_BENCH v1")
    for block in blocks[1:]:
        if f"classifier_backend={backend}" in block:
            fields: Dict[str, str] = {}
            for line in block.splitlines():
                if "=" in line:
                    key, value = line.split("=", 1)
                    fields[key.strip()] = value.strip()
            return fields
    return {}


def run_rust_classifier_eval(
    labeled_csv: Path,
    backend: str,
    model_onnx: Optional[Path] = None,
) -> ClassifierEval:
    cmd = ["cargo", "run", "--quiet"]
    if backend == "onnx":
        cmd.extend(["--features", "onnx"])
    cmd.extend(
        [
            "--manifest-path",
            str(MANIFEST),
            "--",
            "--classifier-eval",
            str(labeled_csv),
            "--backend",
            backend,
        ]
    )
    if backend == "onnx" and model_onnx is not None:
        cmd.extend(["--model-onnx", str(model_onnx)])

    completed = run_command(cmd)
    return parse_classifier_eval(completed.stdout)


def run_inference_benchmark(model_onnx: Path, runs: int = 5000, warmup: int = 500) -> Dict[str, Dict[str, str]]:
    cmd = [
        "cargo",
        "run",
        "--release",
        "--features",
        "onnx",
        "--manifest-path",
        str(MANIFEST),
        "--",
        "--benchmark",
        f"--runs={runs}",
        f"--warmup={warmup}",
        "--onnx-model",
        str(model_onnx),
    ]
    completed = run_command(cmd)
    return {
        "heuristic": parse_bench_block(completed.stdout, "heuristic"),
        "onnx": parse_bench_block(completed.stdout, "onnx"),
    }


def load_xgboost_metrics(path: Path) -> Dict[str, float]:
    if not path.is_file():
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    return {str(k): float(v) for k, v in payload.items()}


def generate_and_train(seed: int) -> None:
    run_command([sys.executable, "-m", "tools.generate_synthetic_training_data", "--seed", str(seed)])
    run_command(
        [
            sys.executable,
            "-m",
            "ml.pipeline_cli",
            "--db-path",
            str(DATA_DIR / "synthetic_focoflow.db"),
            "--output-dir",
            str(DATA_DIR),
            "--backend",
            "xgboost",
        ]
    )


def build_report(
    *,
    seed: int,
    heuristic: ClassifierEval,
    onnx: ClassifierEval,
    xgboost_direct: dict,
    xgboost_metrics: Dict[str, float],
    latency: Dict[str, Dict[str, str]],
    notes: list[str],
) -> dict:
    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "machine": platform.platform(),
        "seed": seed,
        "dataset": {
            "labeled_csv": str(LABELED_CSV),
            "samples": heuristic.samples,
        },
        "classifier_quality": {
            "heuristic_rust": heuristic.__dict__,
            "onnx": onnx.__dict__,
            "xgboost_direct": xgboost_direct,
            "xgboost_training_metrics": xgboost_metrics,
        },
        "inference_latency_us": latency,
        "notes": notes,
    }


def evaluate_quality_gate(
    report: dict,
    *,
    min_cv_accuracy: float,
    min_cv_precision_at_10pct: float,
    min_cv_recall_distracted: float,
    min_recall_lift: float,
) -> list[str]:
    quality = report.get("classifier_quality", {})
    heuristic = quality.get("heuristic_rust", {})
    runtime = quality.get("onnx", {})
    cv = quality.get("xgboost_training_metrics", {})

    failures: list[str] = []
    runtime_accuracy = float(runtime.get("accuracy", cv.get("cv_accuracy", 0.0)))
    runtime_precision = float(
        runtime.get(
            "precision_at_10pct_distracted",
            cv.get("precision_at_10pct", 0.0),
        )
    )
    runtime_recall = float(runtime.get("recall_distracted", cv.get("recall_distracted", 0.0)))
    heuristic_recall = float(heuristic.get("recall_distracted", 0.0))
    recall_lift = runtime_recall - heuristic_recall

    if runtime_accuracy < min_cv_accuracy:
        failures.append(
            f"runtime_accuracy {runtime_accuracy:.3f} < required {min_cv_accuracy:.3f}"
        )
    if runtime_precision < min_cv_precision_at_10pct:
        failures.append(
            "runtime_precision_at_10pct "
            f"{runtime_precision:.3f} < required {min_cv_precision_at_10pct:.3f}"
        )
    if runtime_recall < min_cv_recall_distracted:
        failures.append(
            "runtime_recall_distracted "
            f"{runtime_recall:.3f} < required {min_cv_recall_distracted:.3f}"
        )
    if recall_lift < min_recall_lift:
        failures.append(
            f"runtime recall lift vs heuristic {recall_lift:.3f} < required {min_recall_lift:.3f}"
        )

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark heuristic vs ONNX classifier quality")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--skip-train", action="store_true", help="Reuse existing data/ artifacts")
    parser.add_argument("--skip-latency", action="store_true", help="Skip release inference benchmark")
    parser.add_argument("--output-json", default=str(DATA_DIR / "benchmark_quality.json"))
    parser.add_argument("--enforce-gate", action="store_true", help="Fail if quality floors regress")
    parser.add_argument("--min-cv-accuracy", type=float, default=DEFAULT_MIN_CV_ACCURACY)
    parser.add_argument(
        "--min-cv-precision-at-10pct",
        type=float,
        default=DEFAULT_MIN_CV_PRECISION_AT_10PCT,
    )
    parser.add_argument(
        "--min-cv-recall-distracted",
        type=float,
        default=DEFAULT_MIN_CV_RECALL_DISTRACTED,
    )
    parser.add_argument("--min-recall-lift", type=float, default=DEFAULT_MIN_RECALL_LIFT)
    args = parser.parse_args()

    if not args.skip_train:
        generate_and_train(args.seed)

    if not LABELED_CSV.is_file():
        print(f"missing {LABELED_CSV}; run without --skip-train first", file=sys.stderr)
        return 1
    if not MODEL_ONNX.is_file():
        print(f"missing {MODEL_ONNX}; install xgboost/skl2onnx and re-run training", file=sys.stderr)
        return 1

    heuristic = run_rust_classifier_eval(LABELED_CSV, "heuristic")

    notes: list[str] = []
    try:
        onnx = run_rust_classifier_eval(LABELED_CSV, "onnx", MODEL_ONNX)
    except RuntimeError as err:
        print(f"warning: Rust ONNX eval unavailable ({err}); using onnxruntime", file=sys.stderr)
        py_onnx = evaluate_onnx_model(str(LABELED_CSV), str(MODEL_ONNX))
        onnx = ClassifierEval(
            backend=py_onnx.backend,
            samples=py_onnx.samples,
            accuracy=py_onnx.accuracy,
            precision_at_10pct_distracted=py_onnx.precision_at_10pct_distracted,
            recall_distracted=py_onnx.recall_distracted,
        )
        notes.append("ONNX quality evaluated via Python onnxruntime (Rust ort linker unavailable on this host).")

    xgboost_direct = eval_to_dict(evaluate_xgboost_model(str(LABELED_CSV), str(MODEL_JSON)))
    xgboost_metrics = load_xgboost_metrics(METRICS_JSON)

    latency: Dict[str, Dict[str, str]] = {}
    if not args.skip_latency:
        try:
            latency = run_inference_benchmark(MODEL_ONNX)
        except RuntimeError as err:
            print(f"warning: inference benchmark skipped: {err}", file=sys.stderr)

    report = build_report(
        seed=args.seed,
        heuristic=heuristic,
        onnx=onnx,
        xgboost_direct=xgboost_direct,
        xgboost_metrics=xgboost_metrics,
        latency=latency,
        notes=notes,
    )

    output_json = Path(args.output_json)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    with open(output_json, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")

    if args.enforce_gate:
        failures = evaluate_quality_gate(
            report,
            min_cv_accuracy=args.min_cv_accuracy,
            min_cv_precision_at_10pct=args.min_cv_precision_at_10pct,
            min_cv_recall_distracted=args.min_cv_recall_distracted,
            min_recall_lift=args.min_recall_lift,
        )
        if failures:
            print("classifier quality gate failed:", file=sys.stderr)
            for failure in failures:
                print(f" - {failure}", file=sys.stderr)
            return 1
        print("classifier quality gate passed")

    print(json.dumps(report["classifier_quality"], indent=2))
    print(f"wrote {output_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
