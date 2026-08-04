#!/usr/bin/env python3
"""Fail if the ONNX Runtime archives CI downloads are not pinned by SHA-256.

ROADMAP 8.9. `scripts/check_dependency_pins.py` (8.6) covers the dependencies CMake
fetches. It does not cover these: the two ONNX jobs in `.github/workflows/ci.yml` download
a prebuilt runtime archive from a GitHub release and extract it into `third_party/`, where
CMake links it into the test binary. That is executable third-party code entering the build
by a path 8.6 never looked at, which is why 8.6's broad claim that fetched dependencies are
immutable was incomplete.

A release URL is not a pin. GitHub release assets can be deleted and re-uploaded under the
same name, so `onnxruntime-win-x64-1.20.1.zip` is a mutable pointer in exactly the way a git
tag is. The digest is the pin; the version number beside it is for humans.

`third_party/onnxruntime-pins.json` is the single source of truth, and both jobs read the
URL, filename, and expected digest out of it rather than restating any of them. This script
enforces the properties that make that arrangement actually hold:

  * every platform CI vendors has a 64-hex SHA-256 recorded
  * each archive filename embeds the manifest's version -- this is what makes a version bump
    that forgets to re-hash fail loudly instead of silently verifying the old digest against
    a new file (it cannot: the URL it builds would still point at the old version)
  * no two platforms share a digest, which is what a copy-paste bump looks like
  * ci.yml reads the manifest and hardcodes neither a version nor a digest
  * each vendor step verifies the hash *before* it extracts -- checking afterwards would
    already have written attacker-controlled paths to disk

Usage:  python3 scripts/check_onnx_pins.py [--verbose]
Exit 0 = CI cannot extract an ONNX archive it has not verified.
"""

from __future__ import annotations

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "third_party", "onnxruntime-pins.json")
WORKFLOW = os.path.join(REPO, ".github", "workflows", "ci.yml")

MANIFEST_REL = "third_party/onnxruntime-pins.json"

SHA256 = re.compile(r"^[0-9a-f]{64}$")
VERSION = re.compile(r"^\d+\.\d+\.\d+$")

# The step name both ONNX jobs use, and the shell tokens that mean "extract" and "verify".
# Keep these in sync with ci.yml; a rename that this script stops recognising is caught by
# the "found no vendor steps" check rather than passing silently.
VENDOR_STEP = "- name: Vendor ONNX Runtime"
EXTRACT_TOKENS = ("Expand-Archive", "tar -xzf")
VERIFY_TOKENS = ("Get-FileHash", "sha256sum")


def vendor_steps(workflow: str) -> list[str]:
    """Return the text of each 'Vendor ONNX Runtime' step, up to the next step at its indent."""
    steps = []
    for match in re.finditer(re.escape(VENDOR_STEP), workflow):
        start = match.start()
        indent = start - workflow.rfind("\n", 0, start) - 1
        following = re.compile(rf"^ {{{indent}}}- name: ", re.MULTILINE)
        nxt = following.search(workflow, match.end())
        steps.append(workflow[start : nxt.start() if nxt else len(workflow)])
    return steps


def check_manifest(problems: list[str], verbose: bool) -> dict:
    with open(MANIFEST, encoding="utf-8") as handle:
        pins = json.load(handle)

    version = pins.get("version", "")
    if not VERSION.match(str(version)):
        problems.append(f"version is {version!r}; expected a plain x.y.z release number.")

    if not str(pins.get("release_url", "")).endswith(f"v{version}"):
        problems.append(
            f"release_url does not end in 'v{version}', so the URL and the recorded "
            f"digests describe different releases."
        )

    archives = pins.get("archives") or {}
    if not archives:
        problems.append("no archives recorded; the manifest pins nothing.")

    seen: dict[str, str] = {}
    for platform, entry in sorted(archives.items()):
        name = entry.get("file", "")
        digest = entry.get("sha256", "")

        if not SHA256.match(str(digest)):
            problems.append(
                f"{platform}: sha256 is {digest!r}, not 64 lowercase hex characters. "
                f"Record the real digest -- see docs/dependencies.md."
            )
        elif digest in seen:
            problems.append(
                f"{platform}: shares a digest with {seen[digest]}. Two different archives "
                f"cannot hash the same; this is what a half-finished bump looks like."
            )
        else:
            seen[digest] = platform

        if str(version) not in str(name):
            problems.append(
                f"{platform}: filename {name!r} does not contain version {version}. "
                f"The version and the digests must move together."
            )
        elif verbose:
            print(f"  ok   {platform}: {name} pinned to {digest}")

    return pins


def check_workflow(problems: list[str], pins: dict, verbose: bool) -> None:
    with open(WORKFLOW, encoding="utf-8") as handle:
        workflow = handle.read()

    if MANIFEST_REL not in workflow:
        problems.append(
            f"ci.yml never reads {MANIFEST_REL}, so the manifest is not the source of truth."
        )

    for digest in (entry.get("sha256", "") for entry in (pins.get("archives") or {}).values()):
        if digest and digest in workflow:
            problems.append(
                "ci.yml hardcodes a digest that also lives in the manifest. Read it from "
                "the manifest instead, or the two will drift."
            )
            break

    if re.search(r"^\s*ORT_VERSION\s*:", workflow, re.MULTILINE):
        problems.append(
            "ci.yml still sets ORT_VERSION. The version belongs in the manifest beside the "
            "digests, so a bump cannot change one without the other."
        )

    steps = vendor_steps(workflow)
    if not steps:
        # A renamed step would otherwise make this script report success forever.
        problems.append(
            f"found no {VENDOR_STEP!r} steps in ci.yml -- this parser is out of date with "
            f"the workflow."
        )

    for step in steps:
        extract = min((step.find(t) for t in EXTRACT_TOKENS if t in step), default=-1)
        verify = min((step.find(t) for t in VERIFY_TOKENS if t in step), default=-1)
        label = step.splitlines()[0].strip()

        if extract < 0:
            problems.append(f"{label}: no extraction command recognised; parser out of date.")
        elif verify < 0:
            problems.append(
                f"{label}: extracts an archive without ever hashing it. Verify the SHA-256 "
                f"against the manifest first."
            )
        elif verify > extract:
            problems.append(
                f"{label}: verifies the digest after extracting. By then the archive's "
                f"contents are already on disk; verify first."
            )
        elif verbose:
            print("  ok   a vendor step verifies its archive before extracting")


def main() -> int:
    verbose = "--verbose" in sys.argv
    problems: list[str] = []

    try:
        pins = check_manifest(problems, verbose)
    except FileNotFoundError:
        print(f"check_onnx_pins: {MANIFEST_REL} is missing", file=sys.stderr)
        return 1
    except json.JSONDecodeError as error:
        print(f"check_onnx_pins: {MANIFEST_REL} is not valid JSON: {error}", file=sys.stderr)
        return 1

    check_workflow(problems, pins, verbose)

    if problems:
        print(f"{len(problems)} ONNX pinning problem(s):", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    count = len(pins.get("archives") or {})
    print(f"checked {count} ONNX Runtime archives; all pinned by SHA-256 and verified "
          f"before extraction")
    return 0


if __name__ == "__main__":
    sys.exit(main())
