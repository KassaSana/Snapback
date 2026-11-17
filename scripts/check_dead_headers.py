#!/usr/bin/env python3
"""Every header in src/ must be included by product code, not only by its own tests.

Product code is everything tracked under `src/` and `tools/`. A header's own `.cpp` doesn't
count as a caller. Includes are resolved against `src/` and the including file's directory,
so two headers with the same basename can't cover for each other. The scan is textual and
platform-blind.
"""
from __future__ import annotations

import collections
import os
import pathlib
import re
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

# Directories whose tracked sources count as product code. Tests are deliberately absent: a
# header included only from tests/ is precisely what this check is looking for.
PRODUCT_DIRS = ("src", "tools")

# Where headers that must be reachable live.
HEADER_DIR = "src"

HEADER_SUFFIXES = (".hpp", ".h")
SOURCE_SUFFIXES = (".hpp", ".h", ".cpp", ".cc", ".c", ".mm", ".m")

INCLUDE_RE = re.compile(r'#\s*include\s*"([^"]+)"')


def tracked(*paths: str) -> list[str]:
    """Tracked files under `paths`, as repo-relative slash-separated strings."""
    result = subprocess.run(
        ["git", "ls-files", "-z", *paths], cwd=REPO_ROOT, capture_output=True, check=True
    )
    return [p for p in result.stdout.decode("utf-8").split("\0") if p]


def resolve(spelling: str, including_file: str, known: set[str]) -> str | None:
    """The tracked file an `#include "..."` refers to, or None if it names none.

    Tried in the order the compiler would: the include path root (`src/`), then relative to
    the file doing the including. Anything unresolved is a third-party or generated header
    and is not this check's business.
    """
    candidates = [
        f"{HEADER_DIR}/{spelling}",
        f"{os.path.dirname(including_file)}/{spelling}" if os.path.dirname(including_file) else spelling,
    ]
    for candidate in candidates:
        normalized = os.path.normpath(candidate).replace(os.sep, "/")
        if normalized in known:
            return normalized
    return None


def is_product(path: str) -> bool:
    return any(path.startswith(f"{d}/") for d in PRODUCT_DIRS)


def own_translation_unit(header: str, path: str) -> bool:
    """True if `path` is the .cpp/.mm that implements `header` -- same directory, same stem."""
    return os.path.dirname(path) == os.path.dirname(header) and (
        os.path.splitext(os.path.basename(path))[0]
        == os.path.splitext(os.path.basename(header))[0]
    )


def callers(header: str, users: set[str]) -> set[str]:
    """The product files that include `header`, excluding its own implementation file."""
    return {u for u in users if is_product(u) and not own_translation_unit(header, u)}


def main() -> int:
    verbose = "--verbose" in sys.argv

    product_files = [
        p for p in tracked(*PRODUCT_DIRS) if p.endswith(SOURCE_SUFFIXES)
    ]
    test_files = [p for p in tracked("tests") if p.endswith(SOURCE_SUFFIXES)]
    known = set(product_files) | set(test_files)

    headers = sorted(
        p for p in product_files if p.startswith(f"{HEADER_DIR}/") and p.endswith(HEADER_SUFFIXES)
    )

    included_by: dict[str, set[str]] = collections.defaultdict(set)
    for path in product_files + test_files:
        text = (REPO_ROOT / path).read_text(encoding="utf-8", errors="replace")
        for match in INCLUDE_RE.finditer(text):
            target = resolve(match.group(1), path, known)
            if target and target != path:
                included_by[target].add(path)

    dead: list[tuple[str, set[str]]] = []
    for header in headers:
        users = included_by.get(header, set())
        if not callers(header, users):
            dead.append((header, users))

    if dead:
        print("Headers in src/ with no caller in product code:", file=sys.stderr)
        for header, users in dead:
            if users:
                where = ", ".join(sorted(users))
                print(f"  {header} -- included only by {where}", file=sys.stderr)
                print(
                    "     (its own .cpp and its tests do not count as callers)",
                    file=sys.stderr,
                )
            else:
                print(f"  {header} -- included by nothing at all", file=sys.stderr)
        print(
            "\nEither give it a caller or delete it. A header whose only include is its own "
            "test ships nothing while looking tested.",
            file=sys.stderr,
        )
        return 1

    print(f"checked {len(headers)} headers in {HEADER_DIR}/; every one has a product caller")
    if verbose:
        for header in headers:
            users = sorted(callers(header, included_by.get(header, set())))
            print(f"  {header} <- {', '.join(users)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
