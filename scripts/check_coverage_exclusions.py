#!/usr/bin/env python3
"""Every module excluded from the Vitest coverage gate must still have a unit test.

`vite.config.ts` excludes a few pure-logic modules from coverage because their tests run under
`tsx` (`npm run test:unit`), which doesn't feed V8's coverage. This makes sure each excluded
module has a test file that `test:unit` actually runs, so the list can't be used to hide
untested code.
"""
from __future__ import annotations

import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
FRONTEND = REPO_ROOT / "frontend"
VITE_CONFIG = FRONTEND / "vite.config.ts"
PACKAGE_JSON = FRONTEND / "package.json"

# Excluded because they are not source under test, rather than because they are tested
# elsewhere. Each needs a stated reason here to stay off the list above.
NOT_SOURCE = {
    "src/main.tsx": "the entry point; it wires the app to the DOM and has no logic",
    "src/vite-env.d.ts": "ambient type declarations; no runtime code exists",
}


def coverage_exclusions(config_text: str) -> list[str]:
    """The string entries of the `exclude:` array inside the coverage block."""
    coverage_at = config_text.find("coverage:")
    if coverage_at == -1:
        raise SystemExit("check_coverage_exclusions: no coverage block in vite.config.ts")
    exclude_at = config_text.find("exclude:", coverage_at)
    if exclude_at == -1:
        raise SystemExit("check_coverage_exclusions: coverage block has no exclude list")
    open_bracket = config_text.index("[", exclude_at)
    close_bracket = config_text.index("]", open_bracket)
    return re.findall(r'"([^"]+)"', config_text[open_bracket:close_bracket])


def main() -> int:
    verbose = "--verbose" in sys.argv
    config_text = VITE_CONFIG.read_text(encoding="utf-8")
    unit_script = json.loads(PACKAGE_JSON.read_text(encoding="utf-8"))["scripts"]["test:unit"]

    failures: list[str] = []
    checked = 0
    for entry in coverage_exclusions(config_text):
        checked += 1
        if entry in NOT_SOURCE:
            if verbose:
                print(f"  skip  {entry} -- {NOT_SOURCE[entry]}")
            continue

        module = pathlib.PurePosixPath(entry).stem
        test_file = FRONTEND / "tests" / f"{module}.test.ts"
        if not test_file.is_file():
            failures.append(
                f"{entry} is excluded from the coverage gate but has no "
                f"frontend/tests/{module}.test.ts"
            )
            continue
        # Existing is not enough: an unregistered test file is a file nobody runs.
        if f"tests/{module}.test.ts" not in unit_script:
            failures.append(
                f"{entry} is excluded from the coverage gate and "
                f"frontend/tests/{module}.test.ts exists, but `npm run test:unit` never runs it"
            )
            continue
        if verbose:
            print(f"  ok    {entry} -- covered by tests/{module}.test.ts")

    if failures:
        print("coverage exclusions that are not tested elsewhere:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"checked {checked} coverage exclusions; every one is tested or is not source")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
