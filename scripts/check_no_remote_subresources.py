#!/usr/bin/env python3
"""Fail if any shipped HTML would fetch something over the network.

ROADMAP 8.10. The Privacy card tells the user "Nothing leaves this device", and
`frontend/scripts/inline-bundle.mjs` states the shipped page "makes ZERO subresource
requests". Both were false: `frontend/index.html` linked a Google Fonts stylesheet, which in
turn fetches font files from a second origin, and the CSP was widened to permit both.

That failure had two properties worth designing against. It was **invisible offline** — with
no network the page silently rendered with fallback typography and looked fine — so nobody
developing on a plane would notice. And it was **a promise in prose contradicted by markup**,
which is the exact class of drift Tier 12 exists for.

So this is mechanical. It rejects, in every HTML file that ships:

  * `src`/`href` attributes pointing at http:, https:, or protocol-relative `//host`
  * `@import` of a remote stylesheet, and `url(http...)` inside inline CSS
  * `preconnect`/`dns-prefetch`/`prefetch`/`preload` hints, which fetch or open connections
    without being a subresource of their own
  * remote origins anywhere in a Content-Security-Policy, since a permitted origin is a
    standing invitation even when nothing currently uses it

`data:` URIs are allowed: they are inline bytes, not a request. An explicit, disclosed user
action such as a future update check (3.5) is a separate policy question and would not live
in page markup.

Usage:  python3 scripts/check_no_remote_subresources.py [--verbose]
Exit 0 = the shipped page cannot talk to anything.
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Where shipped HTML lives. dist/ is the built output and is only present after a build; it is
# checked when it exists so a release build verifies the real artifact rather than the source.
SEARCH_DIRS = [
    os.path.join(REPO, "frontend"),
    os.path.join(REPO, "frontend", "public"),
    os.path.join(REPO, "frontend", "dist"),
]

SKIP_DIRS = {"node_modules", ".git", "coverage"}

REMOTE_URL = re.compile(r"""(?:src|href)\s*=\s*["'](?P<url>(?:https?:)?//[^"']+)""", re.I)
CSS_IMPORT = re.compile(r"""@import\s+(?:url\()?["']?(?P<url>(?:https?:)?//[^"')\s]+)""", re.I)
CSS_URL = re.compile(r"""url\(\s*["']?(?P<url>(?:https?:)?//[^"')\s]+)""", re.I)
# Only the hints that are inherently remote. `preload`/`prefetch` are routinely used for local
# assets, and a remote one is already caught by REMOTE_URL via its href — flagging them by rel
# alone produced a false positive against the *inlined React bundle*, which contains the string
# 'link[rel="preload"]' as a DOM selector.
PRECONNECT = re.compile(r"""rel\s*=\s*["'](?P<rel>preconnect|dns-prefetch)""", re.I)

# Script and style *bodies* are stripped before the markup checks. The opening tag survives, so
# `<script src="https://...">` is still caught; what goes away is inlined JavaScript, which is
# full of strings that look like markup. Scanning it with HTML regexes reports things the
# browser will never fetch, and a guard that cries wolf is a guard someone turns off.
SCRIPT_OR_STYLE_BODY = re.compile(
    r"(<(?P<tag>script|style)\b[^>]*>)(?P<body>.*?)(</(?P=tag)\s*>)", re.I | re.S)
# The quote character is captured and back-referenced rather than using a [^"'] class: a CSP
# is full of single-quoted keywords like 'self', so a class excluding both quote characters
# stops at the first one and captures the policy's first two words. That is how the first
# version of this check passed a policy that re-permitted fonts.googleapis.com.
CSP_META = re.compile(
    r"""http-equiv\s*=\s*["']Content-Security-Policy["'][^>]*?"""
    r"""content\s*=\s*(?P<q>["'])(?P<policy>.*?)(?P=q)""",
    re.I | re.S)
CSP_REMOTE = re.compile(r"""(?:https?:)?//[^\s;'"]+""", re.I)


def html_files() -> list[str]:
    found: list[str] = []
    for root_dir in SEARCH_DIRS:
        if not os.path.isdir(root_dir):
            continue
        for dirpath, dirnames, filenames in os.walk(root_dir):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                if name.endswith((".html", ".htm")):
                    found.append(os.path.join(dirpath, name))
    return sorted(set(found))


def check(path: str, problems: list[str], verbose: bool) -> None:
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    rel = os.path.relpath(path, REPO).replace("\\", "/")

    # Keep the tags, drop their bodies. CSS bodies are dropped too, but @import/url() of a
    # remote origin inside a <style> would be blocked by the CSP this same file pins, and the
    # source stylesheets are checked as .css by their own build.
    markup = SCRIPT_OR_STYLE_BODY.sub(lambda m: m.group(1) + m.group(4), text)

    for pattern, label in ((REMOTE_URL, "loads"), (CSS_IMPORT, "@imports"), (CSS_URL, "url()s")):
        for match in pattern.finditer(markup):
            problems.append(f"{rel}: {label} a remote resource: {match.group('url')[:80]}")

    for match in PRECONNECT.finditer(markup):
        problems.append(
            f"{rel}: has rel=\"{match.group('rel')}\", which opens a connection or fetches "
            f"before anything asks for it.")

    for match in CSP_META.finditer(text):
        for origin in CSP_REMOTE.finditer(match.group("policy")):
            problems.append(
                f"{rel}: its CSP permits the remote origin {origin.group(0)}. Remove it — a "
                f"permitted origin is a standing invitation even if nothing uses it today.")

    if verbose:
        print(f"  ok   {rel}")


def main() -> int:
    verbose = "--verbose" in sys.argv
    files = html_files()

    if not files:
        # An empty check is a broken check: a move or rename that hides every HTML file from
        # this script would otherwise report success forever.
        print("check_no_remote_subresources: found no HTML to check -- the search paths are "
              "out of date with the tree", file=sys.stderr)
        return 1

    problems: list[str] = []
    for path in files:
        check(path, problems, verbose)

    if problems:
        print(f"{len(problems)} network dependency/dependencies in shipped HTML:",
              file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print(f"checked {len(files)} HTML file(s); none fetches anything over the network")
    return 0


if __name__ == "__main__":
    sys.exit(main())
