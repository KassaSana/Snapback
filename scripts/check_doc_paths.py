#!/usr/bin/env python3
"""Fail if a doc names a repo path or links to a file that doesn't exist.

Checks inline-code paths (optionally with a `:symbol` suffix) and relative markdown links in
every `*.md` under `docs/` plus the root and package READMEs. Globs must match at least one file.

Usage:  python3 scripts/check_doc_paths.py [--verbose]
"""

from __future__ import annotations

import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Only treat something as a path claim if it starts at one of our real top-level dirs,
# or is a relative link. Otherwise prose like `std::chrono` or `window.__snapback` trips it.
ROOTS = ("src/", "tests/", "docs/", "scripts/", "frontend/", "tools/", "benchmarks/",
         "fixtures/", "third_party/", ".github/")

INLINE_CODE = re.compile(r"`([^`\n]+)`")
MD_LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
FENCED_BLOCK = re.compile(r"```.*?```", re.S)
# Strip a `:symbol` or `:line` anchor so what's left is a path.
LINE_SUFFIX = re.compile(r":(?:\d+(?:-\d+)?|[A-Za-z_][A-Za-z0-9_:~]*)$")
# ADR filenames are written as a naming convention, not a claim that a file exists.
PLACEHOLDER = re.compile(r"NNNN")

# Build outputs that docs mention but a fresh checkout doesn't have.
GENERATED = {
    "frontend/dist": "vite build output",
    "frontend/dist/index.html": "vite build output",
    "frontend/coverage": "`npm run test:coverage` output",
    "frontend/coverage/": "`npm run test:coverage` output",
}

# Gitignored local files: a link to one works on the author's machine and nowhere else.
FORBIDDEN_CLONE_ABSENT = {
    "CLAUDE.md": "gitignored local file",
}

# Optional paths the docs describe but that aren't checked in.
EXPECTED_ABSENT = {
    "third_party/onnxruntime": "optional vendored ONNX Runtime",
    "third_party/onnxruntime/": "optional vendored ONNX Runtime",
    "third_party/onnxruntime/lib": "optional vendored ONNX Runtime",
    "third_party/sqlite/": "optional local SQLite override",
}


def candidate_paths(text: str) -> set[str]:
    found: set[str] = set()
    for m in INLINE_CODE.finditer(text):
        token = m.group(1).strip()
        # `a.hpp/.cpp` is our shorthand for two files; expand it.
        if token.endswith("/.cpp") or token.endswith("/.hpp"):
            stem = token[: token.rindex("/")]
            if not stem.startswith(ROOTS):
                continue  # relative to a table's column header; not a checkable claim
            base = stem.rsplit(".", 1)[0]
            found.add(stem)
            found.add(base + token[token.rindex("/") + 1:])
            continue
        for part in token.split():
            part = part.strip(",;")
            if part.startswith(ROOTS):
                found.add(part)
            elif ("/" in part or "\\" in part) and _leaf_name(part) in FORBIDDEN_CLONE_ABSENT:
                # `../../CLAUDE.md` is a path claim even though it does not start at ROOTS.
                found.add(part)
    return found


def link_paths(text: str, doc_dir: str) -> set[str]:
    # Strip code first: `window[cmd](args)` is not a markdown link, but the regex
    # cannot tell. This was a real false positive, not a hypothetical one.
    text = FENCED_BLOCK.sub("", text)
    text = INLINE_CODE.sub("", text)
    out: set[str] = set()
    for m in MD_LINK.finditer(text):
        target = m.group(1).split("#")[0]
        if not target or target.startswith(("http://", "https://", "mailto:")):
            continue
        out.add(os.path.relpath(os.path.join(doc_dir, target), REPO))
    return out


def _leaf_name(path: str) -> str:
    return os.path.basename(path.replace("\\", "/"))


def forbidden_reason(path: str) -> str | None:
    """Why this reference is illegal even if the file exists locally, or None."""
    cleaned = LINE_SUFFIX.sub("", path).rstrip(".,;)")
    return FORBIDDEN_CLONE_ABSENT.get(_leaf_name(cleaned))


def resolve(path: str) -> tuple[bool, str]:
    """(ok, note). Globs must match at least one file."""
    cleaned = LINE_SUFFIX.sub("", path).rstrip(".,;)")
    if PLACEHOLDER.search(cleaned):
        return True, "placeholder"
    if cleaned in GENERATED:
        return True, f"generated ({GENERATED[cleaned]})"
    reason = forbidden_reason(path)
    if reason:
        return False, f"forbidden ({reason})"
    if cleaned in EXPECTED_ABSENT:
        return True, f"expected absent ({EXPECTED_ABSENT[cleaned]})"
    full = os.path.join(REPO, cleaned)

    if "*" in cleaned:
        return bool(glob.glob(full)), "glob"
    return os.path.exists(full), ""


def self_test() -> list[str]:
    """Pin 12.7: a CLAUDE.md *path* is always illegal; naming the file in prose is not."""
    problems: list[str] = []
    adr_dir = os.path.join(REPO, "docs", "adr")

    link_refs = candidate_paths("") | link_paths("[see](../../CLAUDE.md)", adr_dir)
    if not any(forbidden_reason(path) for path in link_refs):
        problems.append("markdown link to CLAUDE.md was not rejected")

    path_refs = candidate_paths("cites `../../CLAUDE.md` as evidence")
    if not any(forbidden_reason(path) for path in path_refs):
        problems.append("inline relative path to CLAUDE.md was not rejected")

    prose_refs = candidate_paths("an existing commit refers to `CLAUDE.md` by name")
    if any(forbidden_reason(path) for path in prose_refs):
        problems.append("bare prose name CLAUDE.md was treated as a path citation")

    return problems


def main() -> int:
    verbose = "--verbose" in sys.argv

    self_test_problems = self_test()
    if self_test_problems:
        print("self-test failed:")
        for problem in self_test_problems:
            print(f"  {problem}")
        return 1

    # Walk docs/ rather than listing docs/ and docs/adr/ by hand. The hand-written version
    # covered exactly the two directories that existed when it was written, so a third one
    # was unguarded by construction: docs/scratch/ sat there carrying 27 citations that this
    # script never opened. A guard whose coverage depends on somebody remembering to widen it
    # is a guard with a hole in it.
    docs = []
    for current, _dirs, files in os.walk(os.path.join(REPO, "docs")):
        for name in sorted(files):
            if name.endswith(".md"):
                docs.append(os.path.relpath(os.path.join(current, name), REPO)
                            .replace(os.sep, "/"))
    docs.sort()
    docs += ["README.md", "CONTRIBUTING.md", "scripts/README.md", "frontend/README.md"]

    failures: list[tuple[str, str, str]] = []
    checked = 0
    for doc in docs:
        full_doc = os.path.join(REPO, doc)
        if not os.path.exists(full_doc):
            failures.append((doc, "<doc itself is missing>", ""))
            continue
        with open(full_doc, encoding="utf-8") as fh:
            text = fh.read()
        for path in sorted(candidate_paths(text) | link_paths(text, os.path.dirname(full_doc))):
            checked += 1
            ok, note = resolve(path)
            if not ok:
                failures.append((doc, path, note))
            elif verbose:
                print(f"  ok  {doc}: {path} {note}".rstrip())

    print(f"checked {checked} path references across {len(docs)} docs")
    if failures:
        print(f"\n{len(failures)} broken reference(s):")
        for doc, path, note in failures:
            suffix = f" — {note}" if note else ""
            print(f"  {doc}: {path}{suffix}")
        return 1
    print("all referenced paths exist")
    return 0


if __name__ == "__main__":
    sys.exit(main())
