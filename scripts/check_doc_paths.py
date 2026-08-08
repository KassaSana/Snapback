#!/usr/bin/env python3
"""Fail if a doc names a file that does not exist.

Tier 12 of the roadmap exists because docs here repeatedly asserted paths that were
never created, or that moved. Auditing that by hand is what let it rot; this makes it
mechanical.

What it checks, for every `*.md` under `docs/` plus the tracked root/readme files:

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

# Paths a doc must not cite, even when the file happens to exist on the author's machine.
# CLAUDE.md is gitignored; a clone never has it, so a link that "works for Kassa" is a
# broken citation for everyone else. Roadmap 12.7: restate the fact inline instead.
FORBIDDEN_CLONE_ABSENT = {
    "CLAUDE.md": "gitignored local agent file; restating the fact inline is the citation",
}

EXPECTED_ABSENT = {
    "third_party/onnxruntime": "optional vendored ONNX Runtime; CI vendors it, absent locally",
    "third_party/onnxruntime/": "same",
    "third_party/onnxruntime/lib": "same -- the lib subdir docs tell you to populate",
    "third_party/sqlite/": "optional vendored SQLite override; docs say it is not checked in",
    "docs/TODO.md": "deleted 2026-07-20; ROADMAP.md names it to say so",
    "frontend/public/snapback.html": "deleted 2026-08-05 by ROADMAP 8.10 — a pre-3.1 webview "
                                     "overlay nothing loaded, which shipped in every build "
                                     "and fetched webfonts; the item names it to say so",
    "docs/DECISIONS.md": "a rejected option in ADR-0001, never created",
    # ADR-0002 and ADR-0003 both cite this header as the live example of "the scores are
    # undecided". ADRs are append-only, so those references cannot be edited away — the
    # entry is required, not a convenience.
    "src/engine/confidence.hpp": "deleted 2026-08-03 by ADR-0004; ADR-0002/0003 and "
                                 "ROADMAP 5.3 name it to say it is gone",
    "tests/test_confidence.cpp": "deleted with it; ROADMAP 5.3 names it to say so",
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

    docs = [os.path.join("docs", f) for f in sorted(os.listdir(os.path.join(REPO, "docs")))
            if f.endswith(".md")]
    docs += [os.path.join("docs", "adr", f)
             for f in sorted(os.listdir(os.path.join(REPO, "docs", "adr")))
             if f.endswith(".md")]
    docs += ["README.md", os.path.join("scripts", "README.md")]

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
