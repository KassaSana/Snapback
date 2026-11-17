#!/usr/bin/env python3
"""Fail if release legal files are missing or not wired into packaging.

Checks that LICENSE and THIRD_PARTY_NOTICES.md exist, that CPACK_RESOURCE_FILE_LICENSE points
at LICENSE, and that LICENSE is installed into release packages.

Usage:  python3 scripts/check_release_legal.py [--verbose]
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LICENSE = os.path.join(REPO, "LICENSE")
NOTICES = os.path.join(REPO, "THIRD_PARTY_NOTICES.md")
CMAKELISTS = os.path.join(REPO, "CMakeLists.txt")


def main() -> int:
    verbose = "--verbose" in sys.argv
    problems: list[str] = []

    for path, label in ((LICENSE, "LICENSE"), (NOTICES, "THIRD_PARTY_NOTICES.md")):
        if not os.path.isfile(path):
            problems.append(f"missing {label} at repository root")
        elif verbose:
            print(f"  ok    {label}")

    with open(CMAKELISTS, encoding="utf-8") as handle:
        cmake = handle.read()

    license_ref = re.search(
        r'set\s*\(\s*CPACK_RESOURCE_FILE_LICENSE\s+"\$\{CMAKE_SOURCE_DIR\}/([^"]+)"\s*\)',
        cmake,
    )
    if not license_ref:
        problems.append("CMakeLists.txt has no CPACK_RESOURCE_FILE_LICENSE setting")
    elif license_ref.group(1) != "LICENSE":
        problems.append(
            f"CPACK_RESOURCE_FILE_LICENSE points at {license_ref.group(1)!r}, expected LICENSE"
        )
    elif verbose:
        print("  ok    CPACK_RESOURCE_FILE_LICENSE -> LICENSE")

    install_match = re.search(
        r"install\s*\(\s*FILES\s+.*LICENSE.*THIRD_PARTY_NOTICES\.md",
        cmake,
        re.DOTALL,
    )
    if not install_match:
        problems.append(
            "CMakeLists.txt does not install(FILES ... LICENSE ... THIRD_PARTY_NOTICES.md ...)"
        )
    elif verbose:
        print("  ok    install(FILES LICENSE THIRD_PARTY_NOTICES.md ...)")

    if problems:
        for problem in problems:
            print(f"FAIL: {problem}")
        return 1

    print("release legal artifacts present and wired correctly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
