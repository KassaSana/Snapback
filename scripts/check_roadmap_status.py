#!/usr/bin/env python3
"""Every open roadmap item carries exactly one status, and no completed item is left live.

Roadmap 12.8. `docs/ROADMAP.md` was at once the backlog, the changelog and the postmortem
record, and an open proposal had the same shape as agreed work. That is how commit 907517d
landed eleven unverified items -- several on premises the code contradicted -- as siblings of
accepted work, and how a later reader would have taken them for plans. The split moved
completed items to `docs/roadmap_archive.md`; this guard is what stops the shapes merging
again.

Two rules, both about ambiguity rather than tidiness:

1. **Every open item names its status** -- `proposed`, `accepted` or `in progress`, exactly
   one. `proposed` is the default and most of the backlog is there. Promoting something is
   then a visible edit someone makes on purpose, which is precisely what 907517d bypassed.
2. **No item in the live file is marked DONE.** Completed work lives in the archive. A DONE
   entry left here is the old failure mode restarting.

Deliberately not checked: which status an item *should* have. That is a judgment call, and a
guard that tried to make it would either be wrong or become a second backlog.
"""
from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
ROADMAP = REPO_ROOT / "docs" / "ROADMAP.md"

STATUSES = ("proposed", "accepted", "in progress")

# An item line: "- **<id> — <title>** `status` `S` ...". Ids are the tier numbering plus the
# audit/forward-looking prefixes the file already uses.
ITEM = re.compile(r"- \*\*((?:\d+\.\d+[a-z]?|P0-\d+|FWD-\d+|AUD-\d+)\b[^*]*)\*\*(.*)$")

# Quoted history kept beside an item that is still open -- the finding as first written. It is
# context for the item above it, not a second item, so it carries no status of its own.
HISTORY = re.compile(r"\((?:original finding|earlier state|reopened finding|original|first finding)[^)]*\)")

# Narrative bullets that happen to start with an id but are sentences about the plan, not
# tracked items. Keyed by the section they are allowed to appear in, so one cannot quietly
# spread to a tier.
NARRATIVE_SECTIONS = {"The six-month sequence", "Start here — the current sequence"}


def main() -> int:
    verbose = "--verbose" in sys.argv
    lines = ROADMAP.read_text(encoding="utf-8").split("\n")

    section = ""
    missing: list[tuple[int, str, str]] = []
    multiple: list[tuple[int, str, str]] = []
    still_done: list[tuple[int, str]] = []
    counts = dict.fromkeys(STATUSES, 0)
    checked = 0

    for number, line in enumerate(lines, start=1):
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        match = ITEM.match(line)
        if not match:
            continue
        title, rest = match.group(1), match.group(2)

        if section in NARRATIVE_SECTIONS:
            continue
        if HISTORY.search(title):
            continue

        if "DONE" in title or "CLOSED" in title:
            still_done.append((number, title.strip()[:70]))
            continue

        checked += 1
        found = [s for s in STATUSES if f"`{s}`" in rest]
        if not found:
            missing.append((number, title.strip()[:70], rest.strip()[:40]))
        elif len(found) > 1:
            multiple.append((number, title.strip()[:70], ", ".join(found)))
        else:
            counts[found[0]] += 1

    problems = 0
    if missing:
        problems += len(missing)
        print(f"{len(missing)} open item(s) with no status:")
        for number, title, rest in missing:
            print(f"  docs/ROADMAP.md:{number}: {title} -- tags are: {rest}")
        print(f"  Add exactly one of: {', '.join('`' + s + '`' for s in STATUSES)}.")
    if multiple:
        problems += len(multiple)
        print(f"{len(multiple)} open item(s) with more than one status:")
        for number, title, found in multiple:
            print(f"  docs/ROADMAP.md:{number}: {title} -- has {found}")
    if still_done:
        problems += len(still_done)
        print(f"{len(still_done)} completed item(s) still in the live backlog:")
        for number, title in still_done:
            print(f"  docs/ROADMAP.md:{number}: {title}")
        print("  Move them to docs/roadmap_archive.md and index the id under 'Completed work'.")

    if problems:
        return 1
    if verbose:
        summary = ", ".join(f"{counts[s]} {s}" for s in STATUSES)
        print(f"checked {checked} open roadmap items; every one names a status ({summary})")
    else:
        print(f"checked {checked} open roadmap items; every one names a status")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
