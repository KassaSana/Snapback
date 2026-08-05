#!/usr/bin/env python3
"""Fail unless a release tag names exactly the version CMake builds.

ROADMAP 9.11. `release.yml` publishes any pushed `v*` tag. Nothing proved that the tag and
`PROJECT_VERSION` agreed, so `v0.3.0` could publish artifacts whose embedded version — the
one 9.2 compiles into the binary and the one the installer names — still said `0.2.0`.

That mismatch is worse than a wrong number on a page. The version in the binary is what a
support bundle reports and what an upgrade path compares against, so a release whose tag and
artifacts disagree makes every later "which version are you running?" answer untrustworthy.

The tag is the human's intent and `CMakeLists.txt` is what actually gets built. Neither can
be inferred from the other, so the only safe rule is that they must be stated identically.

Usage:  python3 scripts/check_release_tag.py v0.2.0
Exit 0 = the tag names exactly the version this tree builds.
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKELISTS = os.path.join(REPO, "CMakeLists.txt")

# project(<name> VERSION <x.y.z> ...) -- the single place the version is declared (9.2).
PROJECT_VERSION = re.compile(r"^\s*project\s*\([^)]*?\bVERSION\s+(\d+\.\d+\.\d+)", re.MULTILINE)
TAG = re.compile(r"^v(\d+\.\d+\.\d+)$")


def project_version(source: str) -> str | None:
    match = PROJECT_VERSION.search(source)
    return match.group(1) if match else None


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {os.path.basename(sys.argv[0])} <tag>", file=sys.stderr)
        return 2

    tag = sys.argv[1]

    with open(CMAKELISTS, encoding="utf-8") as handle:
        source = handle.read()

    version = project_version(source)
    if version is None:
        # A refactor that moves or reformats the project() call must fail loudly rather than
        # let this check quietly pass on every future release.
        print("check_release_tag: no project(... VERSION x.y.z) found in CMakeLists.txt -- "
              "this parser is out of date", file=sys.stderr)
        return 1

    match = TAG.match(tag)
    if not match:
        print(f"check_release_tag: tag '{tag}' is not of the form vX.Y.Z", file=sys.stderr)
        return 1

    if match.group(1) != version:
        print(f"check_release_tag: tag '{tag}' does not match CMake's PROJECT_VERSION "
              f"({version}). Either tag v{version}, or bump the version in CMakeLists.txt "
              f"and commit that before tagging.", file=sys.stderr)
        return 1

    print(f"tag {tag} matches CMake PROJECT_VERSION {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
