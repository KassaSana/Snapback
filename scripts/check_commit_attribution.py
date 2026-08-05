#!/usr/bin/env python3
"""Fail if any commit claims an author, co-author, or contributor who is not Kassa.

Every commit in this repository is Kassa's own work. That is a standing rule, not a
preference expressed once: ROADMAP.md's standard work loop already says "commit (terse
one-liner, Kassa's identity, zero AI attribution)".

A rule stated in prose is a rule someone eventually forgets. Several tools append attribution
trailers by default -- `Co-Authored-By:` lines, "Generated with ..." footers, vendor noreply
addresses -- and they do it silently, at the moment of committing, when nobody is reading the
message. One such commit is permanent in a way an ordinary mistake is not: rewriting it
changes every SHA after it, which would break release tags and the CI-conclusion checks that
9.11's release gate depends on. So this has to be caught before it lands, not after.

What it checks, across the full history of every ref:

  * no commit message contains a co-author trailer or a generated-with marker
  * no author or committer address belongs to an AI vendor
  * every author identity is on the allowlist below

Usage:  python3 scripts/check_commit_attribution.py [--verbose] [--range <git-range>]
Exit 0 = every commit is attributed to Kassa (or to an explicitly allowed bot).
"""

from __future__ import annotations

import re
import subprocess
import sys

# Kassa's three git identities. All are the same person; the spread is historical, from
# committing on different machines and through the GitHub web UI.
ALLOWED_AUTHORS = {
    "kassasana03@gmail.com",
    "kassaplayz@gmail.com",
    "87040046+kassasana@users.noreply.github.com",
}

# Bots that are allowed to author commits. A dependency bumper is not a co-author claim on
# Kassa's work -- it opens PRs against the frontend lockfile and nothing else.
ALLOWED_BOTS = {
    "49699333+dependabot[bot]@users.noreply.github.com",
}

# GitHub itself is the *committer* on anything merged or authored through the web UI, which
# is why it is allowed here and not in ALLOWED_AUTHORS.
ALLOWED_COMMITTERS = ALLOWED_AUTHORS | ALLOWED_BOTS | {"noreply@github.com"}

# Trailers and footers that assert someone else helped write the commit.
FORBIDDEN_MESSAGE = [
    (re.compile(r"^\s*co[-\s]?authored[-\s]?by\s*:", re.IGNORECASE | re.MULTILINE),
     "a Co-Authored-By trailer"),
    (re.compile(r"^\s*co[-\s]?committed[-\s]?by\s*:", re.IGNORECASE | re.MULTILINE),
     "a Co-Committed-By trailer"),
    (re.compile(r"generated\s+with\s+\[?", re.IGNORECASE), "a generated-with footer"),
    (re.compile(r"\b(?:written|created|authored)\s+by\s+(?:claude|chatgpt|copilot|gpt)",
                re.IGNORECASE), "an AI authorship claim"),
    (re.compile(r"\bsigned-off-by\s*:.*(?:anthropic|openai|github copilot)", re.IGNORECASE),
     "a vendor sign-off"),
]

# Vendor addresses. Matched against the author/committer email only -- deliberately NOT
# against message prose, because a commit may legitimately *discuss* a tool (an existing
# commit references CLAUDE.md while explaining a filename decision). Naming a thing is not
# claiming it wrote the code.
FORBIDDEN_EMAIL = re.compile(
    r"(anthropic\.com|openai\.com|users\.noreply\.github\.com/copilot|copilot@)",
    re.IGNORECASE)

# A record separator that cannot appear in a commit message.
SEP = "\x1e"
FIELDS = "%H%x1f%an%x1f%ae%x1f%cn%x1f%ce%x1f%B"


def is_shallow() -> bool:
    """A shallow clone hides most of history, so a full-history check would pass on nothing."""
    result = subprocess.run(["git", "rev-parse", "--is-shallow-repository"],
                            capture_output=True, text=True, check=False)
    return result.stdout.strip() == "true"


def commits(rev_range: str | None) -> list[tuple[str, str, str, str, str, str]]:
    args = ["git", "log", f"--format={FIELDS}{SEP}"]
    args.append(rev_range if rev_range else "--all")
    raw = subprocess.run(args, capture_output=True, text=True, check=True,
                         encoding="utf-8", errors="replace").stdout

    out = []
    for record in raw.split(SEP):
        record = record.strip("\n")
        if not record:
            continue
        parts = record.split("\x1f")
        if len(parts) != 6:
            continue
        out.append(tuple(parts))  # type: ignore[arg-type]
    return out


def main() -> int:
    verbose = "--verbose" in sys.argv
    rev_range = None
    if "--range" in sys.argv:
        index = sys.argv.index("--range")
        if index + 1 >= len(sys.argv):
            print("--range needs a value", file=sys.stderr)
            return 2
        rev_range = sys.argv[index + 1]

    # Refuse to run against a truncated history rather than report success for the handful
    # of commits that happen to be present. CI must check out with fetch-depth: 0.
    if rev_range is None and is_shallow():
        print("check_commit_attribution: this is a shallow clone, so most of history is "
              "invisible. Check out with fetch-depth: 0, or pass an explicit --range.",
              file=sys.stderr)
        return 1

    try:
        history = commits(rev_range)
    except subprocess.CalledProcessError as error:
        print(f"check_commit_attribution: git log failed: {error}", file=sys.stderr)
        return 1

    if not history:
        # An empty check is a broken check. A bad range or a shallow clone must not report
        # success for having looked at nothing.
        print("check_commit_attribution: no commits examined -- bad range or shallow clone?",
              file=sys.stderr)
        return 1

    problems: list[str] = []

    for sha, author_name, author_email, committer_name, committer_email, message in history:
        short = sha[:9]

        for pattern, label in FORBIDDEN_MESSAGE:
            if pattern.search(message):
                subject = message.strip().splitlines()[0] if message.strip() else ""
                problems.append(f"{short} ({subject[:50]}): message contains {label}.")

        for role, name, email in (("author", author_name, author_email),
                                  ("committer", committer_name, committer_email)):
            if FORBIDDEN_EMAIL.search(email):
                problems.append(f"{short}: {role} address '{email}' belongs to an AI vendor.")

        if author_email.lower() not in (ALLOWED_AUTHORS | ALLOWED_BOTS):
            problems.append(
                f"{short}: author '{author_name} <{author_email}>' is not an allowed "
                f"identity. If this is Kassa on a new machine, add the address to "
                f"ALLOWED_AUTHORS in this script.")

        if committer_email.lower() not in ALLOWED_COMMITTERS:
            problems.append(
                f"{short}: committer '{committer_name} <{committer_email}>' is not allowed.")

    if problems:
        # Cap the output: a rewrite gone wrong would otherwise print thousands of lines.
        shown = problems[:25]
        print(f"{len(problems)} attribution problem(s) across {len(history)} commits:",
              file=sys.stderr)
        for problem in shown:
            print(f"  {problem}", file=sys.stderr)
        if len(problems) > len(shown):
            print(f"  ... and {len(problems) - len(shown)} more", file=sys.stderr)
        return 1

    if verbose:
        authors = sorted({f"{a} <{e}>" for _, a, e, _, _, _ in history})
        for author in authors:
            print(f"  ok   {author}")
    print(f"checked {len(history)} commits; every one is attributed to Kassa")
    return 0


if __name__ == "__main__":
    sys.exit(main())
