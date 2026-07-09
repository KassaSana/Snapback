"""
Compare Rust feature-parity output against scenario expectations.

Usage (from repo root):

  python3 -m ml.feature_parity_cli
  python3 -m ml.feature_parity_cli --scenarios fixtures/feature_parity/scenarios.json
  python3 -m ml.feature_parity_cli --python-only
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

from ml.parity import run_python_scenarios


@dataclass(frozen=True)
class ParityFailure:
    scenario: str
    message: str


def default_scenarios_path() -> Path:
    return Path(__file__).resolve().parent.parent / "fixtures" / "feature_parity" / "scenarios.json"


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run_rust_feature_parity(scenarios_path: Path) -> subprocess.CompletedProcess[str]:
    manifest = repo_root() / "src-tauri" / "Cargo.toml"
    return subprocess.run(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            str(manifest),
            "--",
            "--feature-parity",
            str(scenarios_path),
        ],
        cwd=repo_root(),
        capture_output=True,
        text=True,
        check=False,
    )


def export_rust_feature_parity(scenarios_path: Path) -> List[dict]:
    manifest = repo_root() / "src-tauri" / "Cargo.toml"
    completed = subprocess.run(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            str(manifest),
            "--",
            "--export-feature-parity-json",
            str(scenarios_path),
        ],
        cwd=repo_root(),
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr)
        raise RuntimeError("failed to export Rust feature parity JSON")

    return json.loads(completed.stdout)


def load_scenarios(path: Path) -> List[dict]:
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    return list(payload.get("scenarios", []))


def check_expectations(scenario: dict, features: Dict[str, float]) -> List[str]:
    errors: List[str] = []
    name = str(scenario.get("name", "unknown"))
    expect = scenario.get("expect", {})

    for key, expected in expect.items():
        if key.endswith("_min"):
            base = key[: -len("_min")]
            actual = features.get(base)
            if actual is None:
                errors.append(f"{name}.{base}: missing column")
                continue
            minimum = float(expected)
            if actual < minimum:
                errors.append(f"{name}.{base}: {actual} < min {minimum}")
            continue

        actual = features.get(key)
        if actual is None:
            errors.append(f"{name}.{key}: missing column")
            continue

        if isinstance(expected, dict) and "min" in expected:
            minimum = float(expected["min"])
            if actual < minimum:
                errors.append(f"{name}.{key}: {actual} < min {minimum}")
            continue

        expected_num = float(expected)
        if abs(actual - expected_num) > 1e-6:
            errors.append(f"{name}.{key}: expected {expected_num}, got {actual}")

    return errors


def compare_exported_rust_results(
    scenarios_path: Path,
    rust_results: List[dict],
) -> List[ParityFailure]:
    scenarios = {item["name"]: item for item in load_scenarios(scenarios_path)}
    failures: List[ParityFailure] = []

    for result in rust_results:
        name = str(result.get("name", ""))
        scenario = scenarios.get(name)
        if scenario is None:
            failures.append(ParityFailure(name, "scenario missing from fixtures file"))
            continue

        raw_features = result.get("features", {})
        features = {str(k): float(v) for k, v in raw_features.items()}
        for message in check_expectations(scenario, features):
            failures.append(ParityFailure(name, message))

    return failures


def compare_python_rust_results(
    python_results: List[dict],
    rust_results: List[dict],
    tolerance: float = 1e-4,
) -> List[ParityFailure]:
    python_by_name = {item["name"]: item for item in python_results}
    rust_by_name = {item["name"]: item for item in rust_results}
    failures: List[ParityFailure] = []

    for name in sorted(set(python_by_name) | set(rust_by_name)):
        if name not in python_by_name:
            failures.append(ParityFailure(name, "missing Python replay result"))
            continue
        if name not in rust_by_name:
            failures.append(ParityFailure(name, "missing Rust replay result"))
            continue

        python_features = {
            str(k): float(v) for k, v in python_by_name[name].get("features", {}).items()
        }
        rust_features = {
            str(k): float(v) for k, v in rust_by_name[name].get("features", {}).items()
        }

        for key in sorted(set(python_features) | set(rust_features)):
            if key not in python_features:
                failures.append(ParityFailure(name, f"{key}: missing in Python"))
                continue
            if key not in rust_features:
                failures.append(ParityFailure(name, f"{key}: missing in Rust"))
                continue

            python_value = python_features[key]
            rust_value = rust_features[key]
            if abs(python_value - rust_value) > tolerance:
                failures.append(
                    ParityFailure(
                        name,
                        f"{key}: python={python_value}, rust={rust_value}",
                    )
                )

    return failures


def run_python_expectations(scenarios_path: Path) -> List[ParityFailure]:
    scenarios = {item["name"]: item for item in load_scenarios(scenarios_path)}
    failures: List[ParityFailure] = []
    for result in run_python_scenarios(scenarios_path):
        name = str(result.get("name", ""))
        scenario = scenarios.get(name)
        if scenario is None:
            failures.append(ParityFailure(name, "scenario missing from fixtures file"))
            continue
        features = {str(k): float(v) for k, v in result.get("features", {}).items()}
        for message in check_expectations(scenario, features):
            failures.append(ParityFailure(name, message))
    return failures


def run_cli(
    scenarios_path: Path,
    *,
    python_only: bool = False,
    rust_only: bool = False,
) -> int:
    if not scenarios_path.is_file():
        print(f"Scenarios file not found: {scenarios_path}", file=sys.stderr)
        return 1

    failures: List[ParityFailure] = []

    if rust_only:
        completed = run_rust_feature_parity(scenarios_path)
        if completed.returncode != 0:
            if completed.stdout:
                print(completed.stdout, end="")
            if completed.stderr:
                print(completed.stderr, end="", file=sys.stderr)
            return completed.returncode
        print(completed.stdout, end="")
        return 0

    failures.extend(run_python_expectations(scenarios_path))
    if python_only:
        return _report_failures(failures)

    try:
        python_results = run_python_scenarios(scenarios_path)
        rust_results = export_rust_feature_parity(scenarios_path)
    except RuntimeError:
        return 1

    failures.extend(compare_python_rust_results(python_results, rust_results))

    completed = run_rust_feature_parity(scenarios_path)
    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr)
        return completed.returncode
    print(completed.stdout, end="")

    return _report_failures(failures)


def _report_failures(failures: List[ParityFailure]) -> int:
    if failures:
        for failure in failures:
            print(f"FAIL: {failure.scenario}: {failure.message}", file=sys.stderr)
        return 1

    print("Python/Rust feature parity passed.")
    return 0


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run Rust/Python feature parity checks.")
    parser.add_argument(
        "--scenarios",
        default=str(default_scenarios_path()),
        help="Path to shared parity scenarios JSON.",
    )
    parser.add_argument(
        "--python-only",
        action="store_true",
        help="Validate Python replay against scenario expectations only.",
    )
    parser.add_argument(
        "--rust-only",
        action="store_true",
        help="Run the Rust parity CLI only.",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    return run_cli(
        Path(os.path.expanduser(args.scenarios)),
        python_only=args.python_only,
        rust_only=args.rust_only,
    )


if __name__ == "__main__":
    raise SystemExit(main())
