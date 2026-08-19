#!/usr/bin/env python3
"""Every script in scripts/ must be named in scripts/README.md.

`scripts/README.md` is a table with a "Runs on" column, and it is the only place that says
which of these are Windows-only, which CI runs, and what each one is for. It drifted: five
scripts that CI invokes as **gates** -- `check_coverage_exclusions.py`,
`check_no_remote_subresources.py`, `check_onnx_pins.py`, `check_release_tag.py`, and
`test_commit_msg_hook.sh` -- were missing from it entirely.

That is worse than an incomplete table. Someone reading it to answer "what will CI check
about my change?" got a confident, wrong answer, and the gates most likely to be missing are
the newest ones, which are exactly the ones nobody has internalised yet.

The check is a substring match on the filename, not a parse of the table. Naming the file
anywhere in the README counts -- `hooks/commit-msg`, for instance, earns its place through a
prose section rather than a row. This is deliberately loose: the goal is that the file is not
invisible, and a stricter check would just push people into satisfying the parser.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPTS_README = REPO_ROOT / "scripts" / "README.md"


def tracked_scripts() -> list[str]:
    """Every tracked path under scripts/, README itself excluded."""
    result = subprocess.run(
        ["git", "ls-files", "-z", "scripts"], cwd=REPO_ROOT, capture_output=True, check=True
    )
    paths = [p for p in result.stdout.decode("utf-8").split("\0") if p]
    return sorted(p for p in paths if os.path.basename(p) != "README.md")


def main() -> int:
    verbose = "--verbose" in sys.argv
    readme = SCRIPTS_README.read_text(encoding="utf-8")

    missing: list[str] = []
    checked = 0
    for script in tracked_scripts():
        checked += 1
        if os.path.basename(script) not in readme:
            missing.append(script)
        elif verbose:
            print(f"  ok    {script}")

    if missing:
        print("scripts that scripts/README.md never mentions:")
        for script in missing:
            print(f"  {script}")
        print("\nAdd a row to the table (or a prose section, for something like a git hook). "
              "An undocumented gate is one nobody knows they have to satisfy.")
        return 1

    print(f"checked {checked} scripts; scripts/README.md names every one")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
