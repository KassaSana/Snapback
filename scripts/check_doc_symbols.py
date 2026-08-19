#!/usr/bin/env python3
"""A citation names a symbol, and that symbol must still be in that file.

Docs here used to cite code by line number -- <file>:<line>. That is the one kind of reference
that rots *silently and invisibly*: the file keeps existing, so `check_doc_paths.py` stays
green while the number drifts onto unrelated code. An audit of the tree on 2026-08-19 found
84 such citations, of which a large fraction had already drifted -- several onto blank lines,
one onto a closing brace, one past the end of a 20-line file. A citation that confidently
points at the wrong code is worse than no citation, because the reader stops looking.

So the convention is now `path:symbol` -- `state.cpp:health`, `storage.hpp:kSchemaVersion`,
`tests/test_autostart.cpp:autostart reports a Run-key backend on Windows`. A symbol moves
*with* the code when it moves, which is the property a line number does not have. When a
symbol is deleted or renamed this guard fails, which is correct: that is exactly the moment
a human has to decide whether the surrounding claim is still true.

The check is deliberately a plain substring search rather than a parse. It is not trying to
prove the citation is *apt*; it is trying to prove the thing named is still there. A parser
would need to understand C++, TypeScript, PowerShell, and doctest macro names to do the same
job, and would be wrong in more interesting ways.

Quoted historical code -- a `original finding was:` block showing what the tree used to look
like -- must NOT carry a `path:symbol` citation. Say "as it stood on <date>" instead. Those
blocks are history, and a live-looking pointer into them sends the reader to code that has
deliberately changed out from under the quote.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INLINE_CODE = re.compile(r"`([^`\n]+)`")
# A citation is <path-ending-in-a-source-extension>:<anchor>. The anchor runs to the end of
# the inline-code span: doctest case names are sentences, spaces and all.
CITATION = re.compile(
    r"^(?P<path>[A-Za-z0-9_./-]+\.(?:cpp|hpp|mm|ts|tsx|py|ps1|sh|mjs))"
    r":(?P<symbol>[^`]+)$"
)
# Line numbers are the thing this convention replaced. Catch a regression explicitly rather
# than letting it fall through as "symbol not found", which would read as a puzzling error.
LINE_NUMBER = re.compile(r"^\d+(-\d+)?$")

SOURCE_EXTENSIONS = (".md", ".cpp", ".hpp", ".mm", ".ts", ".tsx", ".py", ".ps1", ".sh", ".mjs")


def tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=REPO, capture_output=True, check=True
    )
    return [p for p in result.stdout.decode("utf-8").split("\0") if p]


def resolve(path: str, tracked: list[str]) -> str | None:
    """A citation may give a full repo path or just a basename, as the docs always have."""
    if path in tracked:
        return path
    leaf = os.path.basename(path)
    matches = [t for t in tracked if os.path.basename(t) == leaf]
    return matches[0] if len(matches) == 1 else None


def main() -> int:
    verbose = "--verbose" in sys.argv
    tracked = tracked_files()
    scan = [t for t in tracked if t.endswith(SOURCE_EXTENSIONS)]

    failures: list[str] = []
    checked = 0
    for doc in scan:
        full = os.path.join(REPO, doc)
        if not os.path.isfile(full):
            continue
        with open(full, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        for span in INLINE_CODE.findall(text):
            match = CITATION.match(span.strip())
            if not match:
                continue
            path, symbol = match.group("path"), match.group("symbol").strip()

            if LINE_NUMBER.match(symbol):
                failures.append(
                    f"{doc}: `{span}` cites a line number. Cite a symbol instead "
                    f"(`{path}:some_function`) -- line numbers rot silently."
                )
                continue

            target = resolve(path, tracked)
            if target is None:
                failures.append(f"{doc}: `{span}` names a file that is not tracked, or is ambiguous")
                continue

            checked += 1
            with open(os.path.join(REPO, target), encoding="utf-8", errors="replace") as fh:
                body = fh.read()
            if symbol not in body:
                failures.append(
                    f"{doc}: `{span}` -- '{symbol}' does not appear in {target}. "
                    f"It was renamed or deleted; re-check the claim around the citation "
                    f"rather than just repointing it."
                )
                continue
            if verbose:
                print(f"  ok    {doc}: {path}:{symbol}")

    if failures:
        print("citations whose symbol is gone:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"checked {checked} symbol citations; every one still resolves")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
