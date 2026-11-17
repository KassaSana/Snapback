#!/usr/bin/env python3
"""Every `frontend/tests/*.test.ts` must be run by `npm run test:unit`.

`test:unit` lists its files by hand, so a new test file that isn't added to it never runs.
`.test.tsx` files are found by Vitest's own glob and aren't checked here.
"""
from __future__ import annotations

import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
FRONTEND = REPO_ROOT / "frontend"
TESTS_DIR = FRONTEND / "tests"
PACKAGE_JSON = FRONTEND / "package.json"


def unit_test_files() -> list[pathlib.Path]:
    """Every pure-logic test file the `tsx` runner is responsible for, sorted."""
    return sorted(p for p in TESTS_DIR.glob("*.test.ts") if p.is_file())


def main() -> int:
    verbose = "--verbose" in sys.argv
    scripts = json.loads(PACKAGE_JSON.read_text(encoding="utf-8"))["scripts"]
    unit_script = scripts["test:unit"]

    failures: list[str] = []
    checked = 0
    for test_file in unit_test_files():
        checked += 1
        # Match the same way check_coverage_exclusions.py does, so the two guards cannot
        # disagree about what "test:unit runs it" means.
        if f"tests/{test_file.name}" not in unit_script:
            failures.append(
                f"frontend/tests/{test_file.name} exists but `npm run test:unit` never runs "
                f"it -- add `tsx tests/{test_file.name}` to the chain in package.json"
            )
            continue
        if verbose:
            print(f"  ok    tests/{test_file.name}")

    if failures:
        print("test files that nothing runs:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"checked {checked} pure-logic test files; `npm run test:unit` runs every one")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
