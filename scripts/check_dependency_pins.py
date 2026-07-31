#!/usr/bin/env python3
"""Fail if a fetched C++ dependency is pinned to anything but an immutable revision.

ROADMAP 8.6. Our C++ dependencies arrive through CMake FetchContent, which compiles
third-party source straight into the shipping binary. Dependabot covers GitHub Actions and
the frontend npm tree; it does not cover these. So the only thing standing between us and a
silently different dependency is what CMakeLists.txt says to fetch.

A tag name is not a promise. `v3.11.3` is a mutable pointer that upstream can move at any
time, including onto a commit we never read, and the next clean build would pick it up with
no diff on our side to review. A 40-character commit SHA cannot move: git's object names are
content-derived, so the same hash is the same tree forever.

This makes that policy mechanical rather than a convention someone remembers. Checking it by
hand is what let the tags sit there in the first place.

What it checks, for every FetchContent_Declare in CMakeLists.txt:

  * a GIT_TAG must be exactly 40 hex characters -- a tag or branch name fails
  * a URL must be accompanied by a URL_HASH (the SQLite amalgamation takes this route:
    an upstream release zip verified by SHA256, which is immutable for the same reason)

Usage:  python3 scripts/check_dependency_pins.py [--verbose]
Exit 0 = every dependency is pinned to something that cannot change underneath us.
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKELISTS = os.path.join(REPO, "CMakeLists.txt")

# FetchContent_Declare(<name> ... ) -- non-greedy to the first closing paren. Our declares
# contain no nested parens, and a comment is stripped before matching so a `#` inside one
# cannot swallow the terminator.
DECLARE = re.compile(r"FetchContent_Declare\s*\(\s*(\w+)(.*?)\)", re.DOTALL)
SHA1 = re.compile(r"^[0-9a-f]{40}$")


def strip_comments(text: str) -> str:
    """Blank out CMake # comments, preserving line structure for readable messages."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def field(body: str, name: str) -> str | None:
    match = re.search(rf"\b{name}\s+(\S+)", body)
    return match.group(1) if match else None


def main() -> int:
    verbose = "--verbose" in sys.argv

    with open(CMAKELISTS, encoding="utf-8") as handle:
        source = strip_comments(handle.read())

    problems: list[str] = []
    checked = 0

    for match in DECLARE.finditer(source):
        name, body = match.group(1), match.group(2)
        checked += 1
        git_tag = field(body, "GIT_TAG")
        url = field(body, "URL")

        if git_tag is not None:
            if SHA1.match(git_tag):
                if verbose:
                    print(f"  ok   {name}: pinned to {git_tag}")
            else:
                problems.append(
                    f"{name}: GIT_TAG is '{git_tag}', which is a mutable ref. "
                    f"Pin the commit SHA instead -- see docs/dependencies.md."
                )
        elif url is not None:
            if field(body, "URL_HASH"):
                if verbose:
                    print(f"  ok   {name}: URL pinned by URL_HASH")
            else:
                problems.append(
                    f"{name}: fetched by URL with no URL_HASH, so its contents are "
                    f"unverified. Add URL_HASH SHA256=... -- see docs/dependencies.md."
                )
        else:
            problems.append(f"{name}: no GIT_TAG and no URL; cannot tell what it fetches.")

    if not checked:
        # A rename or refactor that stops this script from seeing anything would otherwise
        # report success forever. An empty check is a broken check, not a passing one.
        print("check_dependency_pins: found no FetchContent_Declare blocks -- "
              "the parser is out of date with CMakeLists.txt", file=sys.stderr)
        return 1

    if problems:
        print(f"{len(problems)} unpinned dependency/dependencies:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print(f"checked {checked} fetched dependencies; all pinned to immutable revisions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
