#!/usr/bin/env python3
"""Fail if a doc names a file that does not exist.

Tier 12 of the roadmap exists because docs here repeatedly asserted paths that were
never created, or that moved. Auditing that by hand is what let it rot; this makes it
mechanical.

What it checks, for every `*.md` under `docs/` plus `CLAUDE.md` and `README.md`:

  * inline-code paths -- `src/app/state.cpp`, `scripts/test_local.ps1`, `frontend/dist`
  * the same with a `:123` line suffix, which we use a lot
  * relative markdown links -- [text](adr/README.md)

Deliberately NOT checked:

  * anything containing a glob (`input_hook_*.cpp`) -- these are shorthand for a real
    set, so we glob them and require at least one match instead
  * URLs, and bare words that merely look path-ish (no slash, or no known extension)
  * build outputs listed in GENERATED -- a doc naming `frontend/dist` is describing what a
    build produces, which is a correct claim even though a fresh checkout lacks it

Usage:  python3 scripts/check_doc_paths.py [--verbose]
Exit 0 = every referenced path resolves.
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
# `src/app/state.cpp:161` or `state.hpp:161` -- strip the line number.
LINE_SUFFIX = re.compile(r":\d+(-\d+)?$")
# ADR filenames are written as a naming convention, not a claim that a file exists.
PLACEHOLDER = re.compile(r"NNNN")

# Paths a doc names on purpose while saying they are absent. Each needs a reason, so an
# entry can be re-checked rather than becoming a permanent excuse.
GENERATED = {
    "frontend/dist": "vite build output, .gitignore:10 -- docs name it as a build product",
    "frontend/dist/index.html": "same; running.md tells you to build it before the app",
    "frontend/coverage": "istanbul report from `npm run test:coverage`; untracked 2026-07-24",
    "frontend/coverage/": "same",
}

EXPECTED_ABSENT = {
    "third_party/onnxruntime": "optional vendored ONNX Runtime; CI vendors it, absent locally",
    "third_party/onnxruntime/": "same",
    "third_party/onnxruntime/lib": "same -- the lib subdir docs tell you to populate",
    "third_party/sqlite/": "optional vendored SQLite override; docs say it is not checked in",
    "docs/TODO.md": "deleted 2026-07-20; ROADMAP.md names it to say so",
    "docs/DECISIONS.md": "a rejected option in ADR-0001, never created",
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


def resolve(path: str) -> tuple[bool, str]:
    """(ok, note). Globs must match at least one file."""
    cleaned = LINE_SUFFIX.sub("", path).rstrip(".,;)")
    if PLACEHOLDER.search(cleaned):
        return True, "placeholder"
    if cleaned in GENERATED:
        return True, f"generated ({GENERATED[cleaned]})"
    if cleaned in EXPECTED_ABSENT:
        return True, f"expected absent ({EXPECTED_ABSENT[cleaned]})"
    full = os.path.join(REPO, cleaned)

    if "*" in cleaned:
        return bool(glob.glob(full)), "glob"
    return os.path.exists(full), ""


def main() -> int:
    verbose = "--verbose" in sys.argv
    docs = [os.path.join("docs", f) for f in sorted(os.listdir(os.path.join(REPO, "docs")))
            if f.endswith(".md")]
    docs += [os.path.join("docs", "adr", f)
             for f in sorted(os.listdir(os.path.join(REPO, "docs", "adr")))
             if f.endswith(".md")]
    docs += ["CLAUDE.md", "README.md", os.path.join("scripts", "README.md")]

    failures: list[tuple[str, str]] = []
    checked = 0
    for doc in docs:
        full_doc = os.path.join(REPO, doc)
        if not os.path.exists(full_doc):
            failures.append((doc, "<doc itself is missing>"))
            continue
        with open(full_doc, encoding="utf-8") as fh:
            text = fh.read()
        for path in sorted(candidate_paths(text) | link_paths(text, os.path.dirname(full_doc))):
            checked += 1
            ok, note = resolve(path)
            if not ok:
                failures.append((doc, path))
            elif verbose:
                print(f"  ok  {doc}: {path} {note}".rstrip())

    print(f"checked {checked} path references across {len(docs)} docs")
    if failures:
        print(f"\n{len(failures)} broken reference(s):")
        for doc, path in failures:
            print(f"  {doc}: {path}")
        return 1
    print("all referenced paths exist")
    return 0


if __name__ == "__main__":
    sys.exit(main())
