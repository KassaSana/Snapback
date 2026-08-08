#!/usr/bin/env python3
"""Report when pinned C++ deps fall behind the latest GitHub release.

ROADMAP 4.13. Dependabot watches Actions and npm. It does not watch CMake FetchContent
or the ONNX Runtime archives in third_party/onnxruntime-pins.json. 8.6/8.9 made those
pins *trustworthy* (immutable SHA / digest). They did not make them *current*.

This script compares each pin to upstream's latest non-prerelease GitHub release and
exits 1 when any is behind. It never edits a digest or opens a PR: a bot that computes
the hash it is meant to be verifying defeats the pin. The human bump is documented in
docs/dependencies.md.

Usage:
  python3 scripts/check_pin_freshness.py [--verbose] [--offline]
Exit 0 = every watched pin matches the latest release (or --offline parse-only).
Exit 1 = at least one pin is behind.
Exit 2 = the check could not run (parse failure or GitHub API error).
"""

from __future__ import annotations

import json
import os
import re
import sys
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKELISTS = os.path.join(REPO, "CMakeLists.txt")
ONNX_MANIFEST = os.path.join(REPO, "third_party", "onnxruntime-pins.json")

GITHUB_REPO = re.compile(r"github\.com[:/]([^/]+)/([^/.]+)")
VERSION_CORE = re.compile(r"(\d+(?:\.\d+)*)")
DECLARE_NAME = re.compile(r"FetchContent_Declare\s*\(\s*(\w+)")
GIT_REPO_LINE = re.compile(r"GIT_REPOSITORY\s+(\S+)")
GIT_TAG_LINE = re.compile(r"GIT_TAG\s+([0-9a-f]{40})")
GIT_TAG_COMMENT = re.compile(r"#\s*(\S+)")


class Pin:
    def __init__(self, name: str, owner: str, repo: str, sha: str, label: str) -> None:
        self.name = name
        self.owner = owner
        self.repo = repo
        self.sha = sha
        self.label = label  # human version, e.g. v3.11.3 or 1.20.1


def parse_git_pins(cmake_text: str) -> list[Pin]:
    # Line-oriented: webview's version comment sits after the closing paren on the GIT_TAG
    # line (`GIT_TAG <sha>)  # 0.12.0`), so a paren-bounded FetchContent parse would drop it.
    pins: list[Pin] = []
    name = ""
    repo_url = ""
    for line in cmake_text.splitlines():
        declared = DECLARE_NAME.search(line)
        if declared:
            name = declared.group(1)
            repo_url = ""
            continue
        repo_m = GIT_REPO_LINE.search(line)
        if repo_m and name:
            repo_url = repo_m.group(1)
            continue
        tag_m = GIT_TAG_LINE.search(line)
        if tag_m and name and repo_url:
            gh = GITHUB_REPO.search(repo_url)
            comment = GIT_TAG_COMMENT.search(line)
            label = comment.group(1).rstrip(")") if comment else ""
            if gh:
                pins.append(Pin(name, gh.group(1), gh.group(2), tag_m.group(1), label))
            name = ""
            repo_url = ""
    return pins


def parse_onnx_pin(manifest: dict) -> Pin:
    version = str(manifest.get("version", ""))
    return Pin("onnxruntime", "microsoft", "onnxruntime", "", version)


def version_tuple(label: str) -> tuple[int, ...]:
    match = VERSION_CORE.search(label)
    if not match:
        return ()
    return tuple(int(part) for part in match.group(1).split("."))


def is_behind(pinned_label: str, latest_tag: str) -> bool:
    pinned = version_tuple(pinned_label)
    latest = version_tuple(latest_tag)
    if not pinned or not latest:
        return pinned_label.lstrip("v") != latest_tag.lstrip("v")
    return pinned < latest


def _github_json(url: str, token: str):
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "snapback-pin-freshness",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def github_latest_release(owner: str, repo: str, token: str) -> tuple[str, str]:
    """Return (tag_name, commit_sha or '').

    Prefer /releases/latest (skips prereleases). Repos that only cut tags, such as
    webview/webview, 404 that endpoint — fall back to the highest semver tag.
    """
    release_url = f"https://api.github.com/repos/{owner}/{repo}/releases/latest"
    try:
        payload = _github_json(release_url, token)
        tag = str(payload.get("tag_name", ""))
        sha = ""
        target = payload.get("target_commitish") or ""
        if re.fullmatch(r"[0-9a-f]{40}", str(target)):
            sha = str(target)
        if tag:
            return tag, sha
    except urllib.error.HTTPError as exc:
        if exc.code != 404:
            raise

    tags = _github_json(f"https://api.github.com/repos/{owner}/{repo}/tags?per_page=100", token)
    if not isinstance(tags, list) or not tags:
        return "", ""
    versioned = [tag for tag in tags if version_tuple(str(tag.get("name", "")))]
    pool = versioned or tags
    best = max(pool, key=lambda tag: version_tuple(str(tag.get("name", ""))))
    commit = best.get("commit") or {}
    return str(best.get("name", "")), str(commit.get("sha") or "")


def self_test() -> list[str]:
    problems: list[str] = []
    if not is_behind("1.20.1", "v1.21.0"):
        problems.append("1.20.1 should be behind 1.21.0")
    if is_behind("v3.11.3", "v3.11.3"):
        problems.append("equal versions should not be behind")
    if is_behind("v3.11.3", "v3.10.0"):
        problems.append("a newer pin should not be behind an older release")
    sample = (
        "FetchContent_Declare(nlohmann_json\n"
        "  GIT_REPOSITORY https://github.com/nlohmann/json\n"
        "  GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03  # v3.11.3)\n"
        "FetchContent_Declare(webview\n"
        "  GIT_REPOSITORY https://github.com/webview/webview\n"
        "  GIT_TAG        3ab4b5d722438fc8a13e6ca830c5e2372d19a01d)  # 0.12.0\n"
    )
    parsed = parse_git_pins(sample)
    labels = {pin.name: pin.label for pin in parsed}
    if labels.get("nlohmann_json") != "v3.11.3" or labels.get("webview") != "0.12.0":
        problems.append(f"git pin parse failed: {labels}")
    return problems


def main() -> int:
    verbose = "--verbose" in sys.argv
    offline = "--offline" in sys.argv

    self_problems = self_test()
    if self_problems:
        print("self-test failed:", file=sys.stderr)
        for problem in self_problems:
            print(f"  {problem}", file=sys.stderr)
        return 2

    with open(CMAKELISTS, encoding="utf-8") as handle:
        cmake = handle.read()
    with open(ONNX_MANIFEST, encoding="utf-8") as handle:
        onnx = json.load(handle)

    pins = parse_git_pins(cmake)
    pins.append(parse_onnx_pin(onnx))
    if len(pins) < 4:
        print(
            f"check_pin_freshness: expected onnx + 3 git FetchContent pins, found {len(pins)}",
            file=sys.stderr,
        )
        return 2

    if verbose or offline:
        for pin in pins:
            loc = f"{pin.owner}/{pin.repo}"
            extra = f" @{pin.sha[:12]}" if pin.sha else ""
            print(f"  pin  {pin.name}: {pin.label or '(unlabelled)'}{extra} ({loc})")

    if offline:
        print(f"parsed {len(pins)} pins; offline mode does not query GitHub")
        return 0

    token = os.environ.get("GITHUB_TOKEN", "")
    behind: list[str] = []
    errors: list[str] = []
    for pin in pins:
        try:
            latest_tag, latest_sha = github_latest_release(pin.owner, pin.repo, token)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
            errors.append(f"{pin.name}: GitHub API error ({exc})")
            continue
        if not latest_tag:
            errors.append(f"{pin.name}: latest release has no tag_name")
            continue
        stale = is_behind(pin.label, latest_tag)
        if pin.sha and latest_sha and pin.sha != latest_sha and not stale:
            # Same version label, different commit — still a human review item.
            stale = True
        status = "BEHIND" if stale else "current"
        detail = f"pinned {pin.label or pin.sha[:12]}, latest {latest_tag}"
        if latest_sha:
            detail += f" ({latest_sha[:12]})"
        print(f"  {status:7} {pin.name}: {detail}")
        if stale:
            behind.append(
                f"- **{pin.name}** (`{pin.owner}/{pin.repo}`): pinned `{pin.label or pin.sha}` "
                f"-> latest `{latest_tag}`.\n"
                f"  Bump by hand per [docs/dependencies.md](docs/dependencies.md). "
                f"Do not let a bot edit digests."
            )

    if errors:
        print(f"{len(errors)} lookup error(s):", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 2

    if behind:
        print(f"\n{len(behind)} pin(s) behind upstream. Open an issue; do not auto-edit pins.")
        print("BEHIND_MARKDOWN_START")
        print("\n".join(behind))
        print("BEHIND_MARKDOWN_END")
        return 1

    print(f"checked {len(pins)} pins; all match the latest GitHub release")
    return 0


if __name__ == "__main__":
    sys.exit(main())
