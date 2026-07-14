#!/usr/bin/env python3
"""Run Rust/Python feature parity, then diff Rust and C++ feature vectors."""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable


TOLERANCE = 1e-6


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run(cmd: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> None:
    print(f"== {' '.join(cmd)}")
    completed = subprocess.run(cmd, cwd=cwd, env=env, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def capture_json(cmd: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> Any:
    print(f"== {' '.join(cmd)}")
    completed = subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr)
        raise SystemExit(completed.returncode)
    return json.loads(completed.stdout)


def executable_path(build_dir: Path, name: str, config: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [
        build_dir / config / f"{name}{suffix}",
        build_dir / f"{name}{suffix}",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    joined = ", ".join(str(candidate) for candidate in candidates)
    raise SystemExit(f"{name} executable not found. Checked: {joined}")


def by_name(results: Iterable[dict[str, Any]], side: str) -> dict[str, dict[str, float]]:
    out: dict[str, dict[str, float]] = {}
    for result in results:
        name = str(result.get("name", ""))
        if not name:
            raise SystemExit(f"{side} emitted a scenario without a name")
        raw_features = result.get("features", {})
        out[name] = {str(key): float(value) for key, value in raw_features.items()}
    return out


def compare_results(rust_results: Any, cpp_results: Any) -> int:
    rust = by_name(rust_results, "Rust")
    cpp = by_name(cpp_results, "C++")
    failures: list[str] = []

    for name in sorted(set(rust) | set(cpp)):
        if name not in rust:
            failures.append(f"{name}: missing Rust result")
            continue
        if name not in cpp:
            failures.append(f"{name}: missing C++ result")
            continue

        rust_features = rust[name]
        cpp_features = cpp[name]
        for key in sorted(set(rust_features) | set(cpp_features)):
            if key not in rust_features:
                failures.append(f"{name}.{key}: missing Rust value")
                continue
            if key not in cpp_features:
                failures.append(f"{name}.{key}: missing C++ value")
                continue

            rust_value = rust_features[key]
            cpp_value = cpp_features[key]
            if not math.isclose(rust_value, cpp_value, rel_tol=0.0, abs_tol=TOLERANCE):
                failures.append(
                    f"{name}.{key}: rust={rust_value}, cpp={cpp_value}, "
                    f"delta={abs(rust_value - cpp_value)}"
                )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("Rust/C++ feature vector parity passed.")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp-repo", type=Path, default=root)
    parser.add_argument("--rust-repo", type=Path, default=root.parent / "Snapback")
    parser.add_argument("--build-dir", type=Path, default=root / "build-parity")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-rust-python", action="store_true")
    parser.add_argument(
        "--scenarios",
        type=Path,
        default=None,
        help="Defaults to <rust-repo>/fixtures/feature_parity/scenarios.json.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    cpp_repo = args.cpp_repo.resolve()
    rust_repo = args.rust_repo.resolve()
    build_dir = args.build_dir.resolve()
    scenarios = (
        args.scenarios.resolve()
        if args.scenarios
        else rust_repo / "fixtures" / "feature_parity" / "scenarios.json"
    )

    if not (rust_repo / "ml" / "feature_parity_cli.py").is_file():
        raise SystemExit(f"Rust parity CLI not found under {rust_repo}")
    if not scenarios.is_file():
        raise SystemExit(f"Scenario file not found: {scenarios}")

    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"

    if not args.skip_build:
        run(
            [
                "cmake",
                "-S",
                str(cpp_repo),
                "-B",
                str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DSNAPBACK_BUILD_APP=OFF",
                "-DSNAPBACK_ONNX=OFF",
            ],
            cwd=cpp_repo,
            env=env,
        )
        run(
            [
                "cmake",
                "--build",
                str(build_dir),
                "--config",
                args.config,
                "--target",
                "snapback_feature_parity_export",
            ],
            cwd=cpp_repo,
            env=env,
        )

    if not args.skip_rust_python:
        run(
            [
                sys.executable,
                "-m",
                "ml.feature_parity_cli",
                "--scenarios",
                str(scenarios),
            ],
            cwd=rust_repo,
            env=env,
        )

    rust_results = capture_json(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            str(rust_repo / "src-tauri" / "Cargo.toml"),
            "--",
            "--export-feature-parity-json",
            str(scenarios),
        ],
        cwd=rust_repo,
        env=env,
    )

    cpp_export = executable_path(build_dir, "snapback_feature_parity_export", args.config)
    cpp_results = capture_json([str(cpp_export), str(scenarios)], cwd=cpp_repo, env=env)
    return compare_results(rust_results, cpp_results)


if __name__ == "__main__":
    raise SystemExit(main())
