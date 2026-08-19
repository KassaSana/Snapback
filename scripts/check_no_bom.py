#!/usr/bin/env python3
"""No tracked file may start with a UTF-8 byte-order mark.

A BOM is three invisible bytes (`EF BB BF`) in front of the first real character. Nothing in
this project needs one -- every file here is UTF-8 already, and UTF-8 has no byte order to
mark. What a BOM does instead is break tools that expect the first byte to be the first byte:

- a shell script whose `#!` is preceded by a BOM is not executable ("bad interpreter"),
- a `.json` file with one is rejected by strict parsers,
- a C++ or TypeScript file with one shows a stray `` at the top of a diff, and
- a `git blame` on the first line attributes it to whoever last saved from an editor that
  added it, not to whoever wrote the code.

Five files had picked one up (`frontend/src/main.tsx`, `frontend/src/utils.ts`,
`frontend/tests/utils.test.ts`, `frontend/tsconfig.json`, `frontend/vite.config.ts`) with no
pattern to it beyond "a Windows editor touched this file." That is the tell: BOMs arrive by
accident, from tooling, without anyone deciding. So this is checked rather than agreed.

Related but separate: `.gitattributes` pins line endings for the files where CRLF would
break something. Line endings are a *storage* question git can normalise; a BOM is content,
and git will faithfully preserve it forever unless something objects.
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
