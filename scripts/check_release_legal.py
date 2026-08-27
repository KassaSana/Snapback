#!/usr/bin/env python3
"""Fail if release legal artifacts are missing or miswired.

ROADMAP 9.12. A public repository without a project license is not open source in any
usable sense, and CPack was pointing at README.md rather than a real license file.
This guard makes that state fail CI instead of shipping quietly.

Checks:
  * LICENSE exists at the repository root
  * THIRD_PARTY_NOTICES.md exists at the repository root
  * CMakeLists.txt sets CPACK_RESOURCE_FILE_LICENSE to LICENSE, not README.md
  * LICENSE is installed into release packages via CMake install(FILES ...)

Usage:  python3 scripts/check_release_legal.py [--verbose]
Exit 0 = legal artifacts are present and wired correctly.
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
