#!/usr/bin/env python3
"""No tracked file may start with a UTF-8 byte-order mark.

A BOM breaks shebangs ("bad interpreter"), strict JSON parsers, and shows up as noise in diffs.
They creep in from Windows editors.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
BOM = b"\xef\xbb\xbf"


def tracked_files() -> list[str]:
    """Every path git tracks, as repo-relative POSIX strings."""
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=REPO_ROOT,
        capture_output=True,
        check=True,
    )
    return [p for p in result.stdout.decode("utf-8").split("\0") if p]


def main() -> int:
    verbose = "--verbose" in sys.argv

    offenders: list[str] = []
    checked = 0
    for relative in tracked_files():
        path = REPO_ROOT / relative
        if not path.is_file():
            # A path can be tracked without being present in a partial or sparse checkout.
            continue
        checked += 1
        with path.open("rb") as handle:
            if handle.read(3) == BOM:
                offenders.append(relative)

    if offenders:
        print("tracked files starting with a UTF-8 BOM:")
        for offender in offenders:
            print(f"  {offender}")
        print("\nStrip the first three bytes (EF BB BF). In an editor, save as "
              '"UTF-8" rather than "UTF-8 with BOM".')
        return 1

    if verbose:
        print(f"  ok    no BOM in any of the {checked} tracked files")
    print(f"checked {checked} tracked files; none starts with a UTF-8 BOM")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
