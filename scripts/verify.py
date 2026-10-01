#!/usr/bin/env python3
"""Run Snapback's local headless checks or one focused test selection.

Usage:
    python scripts/verify.py
    python scripts/verify.py guards
    python scripts/verify.py native <CTest name regex>
    python scripts/verify.py frontend-unit <test file>
    python scripts/verify.py frontend-component <test file>
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
FRONTEND = ROOT / "frontend"
BUILD = ROOT / "build"

# This is also the list used by the Ubuntu CI repository-guards step. The bundle guard runs
# again in frontend CI after the shipped HTML has been built.
GUARDS = (
    ("check_release_legal.py", ()),
    ("check_doc_paths.py", ()),
    ("check_doc_symbols.py", ()),
    ("check_dependency_pins.py", ()),
    ("check_onnx_pins.py", ()),
    ("check_pin_freshness.py", ("--offline",)),
    ("check_ps_exit_codes.py", ()),
    ("check_commit_attribution.py", ()),
    ("check_no_remote_subresources.py", ()),
    ("check_coverage_exclusions.py", ()),
    ("check_unit_test_wiring.py", ()),
    ("check_no_bom.py", ()),
    ("check_scripts_documented.py", ()),
    ("check_roadmap_status.py", ()),
    ("check_dead_headers.py", ()),
)


def executable(name: str) -> str:
    found = shutil.which(name)
    if found is None:
        raise RuntimeError(f"{name} is required but was not found on PATH")
    return found


def run(label: str, args: list[str], cwd: Path = ROOT) -> None:
    print(f"== {label} ==", flush=True)
    result = subprocess.run(args, cwd=cwd, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"{label} failed (exit {result.returncode})")


def ensure_frontend_dependencies() -> None:
    if not (FRONTEND / "node_modules").is_dir():
        run("Install frontend dependencies", [executable("npm"), "ci"], FRONTEND)


def guards() -> None:
    # This small README contract was a standalone shell check in CI. Keep it portable here.
    if "docs/windows_demo.md" not in (ROOT / "README.md").read_text(encoding="utf-8"):
        raise RuntimeError("README.md must link to docs/windows_demo.md")
    if not (ROOT / "docs" / "windows_demo.md").is_file():
        raise RuntimeError("docs/windows_demo.md is missing")

    for script, options in GUARDS:
        run(script, [sys.executable, str(ROOT / "scripts" / script), *options])

    # The hook smoke needs a POSIX shell. CI always runs it on Ubuntu; Windows developers
    # still run the shared Python attribution check above.
    if os.name != "nt":
        run("commit-msg hook smoke", [executable("sh"), str(ROOT / "scripts" / "test_commit_msg_hook.sh")])


def configure_native() -> None:
    run(
        "Configure native tests",
        [
            executable("cmake"),
            "-S", str(ROOT),
            "-B", str(BUILD),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DSNAPBACK_BUILD_APP=OFF",
            "-DSNAPBACK_ONNX=OFF",
        ],
    )


def native(pattern: str) -> None:
    configure_native()
    run(
        "Build native tests",
        [
            executable("cmake"), "--build", str(BUILD), "--config", "Release",
            "--target", "snapback_tests", "--parallel",
        ],
    )
    run(
        f"Native tests matching {pattern!r}",
        [
            executable("ctest"), "--test-dir", str(BUILD), "-C", "Release", "-R", pattern,
            "--output-on-failure", "--timeout", "120", "--no-tests=error",
        ],
    )


def frontend_test(mode: str, name: str) -> None:
    given = Path(name)
    if given.is_absolute():
        raise RuntimeError("pass a test filename or a path under frontend/tests")
    parts = given.parts
    if parts[:2] == ("frontend", "tests"):
        given = Path(*parts[1:])
    elif not parts or parts[0] != "tests":
        given = Path("tests") / given
    path = (FRONTEND / given).resolve()
    try:
        path.relative_to((FRONTEND / "tests").resolve())
    except ValueError as error:
        raise RuntimeError("test file must be under frontend/tests") from error
    suffix = ".test.ts" if mode == "frontend-unit" else ".test.tsx"
    if not path.name.endswith(suffix) or not path.is_file():
        raise RuntimeError(f"expected an existing frontend/tests/*{suffix} file: {name}")
    ensure_frontend_dependencies()
    relative = path.relative_to(FRONTEND).as_posix()
    runner = ["tsx", relative] if mode == "frontend-unit" else ["vitest", "run", relative]
    run(f"{mode}: {relative}", [executable("npm"), "exec", "--", *runner], FRONTEND)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "mode", nargs="?", choices=("guards", "native", "frontend-unit", "frontend-component")
    )
    parser.add_argument("selection", nargs="?", help="CTest name regex or frontend test file")
    args = parser.parse_args()
    if (args.mode in ("native", "frontend-unit", "frontend-component")) != (args.selection is not None):
        parser.error("targeted modes require one selection; the full and guards modes take none")

    try:
        if args.mode == "guards":
            guards()
        elif args.mode == "native":
            native(args.selection)
        elif args.mode in ("frontend-unit", "frontend-component"):
            frontend_test(args.mode, args.selection)
        else:
            if os.name == "nt":
                run(
                    "Local headless suite",
                    [
                        executable("powershell"), "-ExecutionPolicy", "Bypass", "-File",
                        str(ROOT / "scripts" / "test_local.ps1"),
                    ],
                )
            else:
                run(
                    "Local headless suite",
                    [executable("bash"), str(ROOT / "scripts" / "test_local.sh")],
                )
            run("Frontend lint", [executable("npm"), "run", "lint"], FRONTEND)
            guards()
    except (OSError, RuntimeError) as error:
        print(f"verify: {error}", file=sys.stderr)
        return 1
    print("Verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
