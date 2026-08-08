#!/usr/bin/env python3
"""Fail if a PowerShell script runs a native command without checking its exit code.

`$ErrorActionPreference = "Stop"` -- which every script here sets, and which reads like it
covers everything -- only makes *cmdlets* terminate. A native executable (cmake, ctest, npm,
cpack, ...) reports failure by setting `$LASTEXITCODE`, and a bare call discards it. The
script keeps going, reaches its last line, and exits 0.

That is not a hypothetical. `windows_demo.ps1` ran `cmake --build --target snapback`
unchecked, so when `main.cpp` stopped compiling the Windows CI job still passed. Three
commits landed on master with a desktop binary that could not be built, and the only reason
anyone found out is that the macOS and Linux jobs -- which run cmake directly from the
workflow, where the runner checks the exit code -- went red.

`package_windows.ps1` already had the fix (`Invoke-Native`) and had had it for a while. The
convention existed; nothing made the other scripts follow it. Hence this guard.

The rule: inside `scripts/*.ps1`, a native tool may only be invoked inside an
`Invoke-Native { ... }` block. Anything else is an unchecked call.

Usage:  python3 scripts/check_ps_exit_codes.py [--verbose]
Exit 0 = every native invocation in every PowerShell script is exit-code checked.
"""

from __future__ import annotations

import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(REPO, "scripts")

# Tools that report failure through $LASTEXITCODE rather than by throwing. Cmdlets are absent
# on purpose: $ErrorActionPreference genuinely does cover those.
NATIVE_TOOLS = {
    "cmake", "ctest", "cpack", "npm", "npx", "node", "tsc", "pip", "python", "python3",
    "git", "powershell", "pwsh", "dotnet", "msbuild", "signtool", "7z", "tar", "curl",
}

# A statement whose first token is a native tool, e.g. `cmake --build ...`.
TOOL_CALL = re.compile(r"^\s*([A-Za-z0-9_.\-]+)\b")
# A statement invoking something through the call operator, e.g. `& $Exe` or `& $tool.Source`.
CALL_OPERATOR = re.compile(r"^\s*&\s*[\"'$]")
# The helper itself, in any of its equivalent spellings.
INVOKE_NATIVE = re.compile(r"\bInvoke-Native\b")
FUNCTION_DEF = re.compile(r"^\s*function\s+Invoke-Native\b", re.IGNORECASE)
LAST_EXIT_CODE = re.compile(r"\$LASTEXITCODE\b", re.IGNORECASE)

# How many lines after a call may carry its `if ($LASTEXITCODE -ne 0) { throw }`. Checking the
# variable inline is as correct as the helper -- Sign-ReleaseBinary in package_windows.ps1 does
# exactly that -- so the guard accepts it rather than forcing a rewrite of working code. Kept
# short so a check belonging to some later statement cannot be mistaken for this one's.
INLINE_CHECK_LOOKAHEAD = 3


def strip_noise(line: str) -> str:
    """Drop comments and string literals so their contents cannot look like a call."""
    line = re.sub(r'"[^"]*"', '""', line)
    line = re.sub(r"'[^']*'", "''", line)
    return line.split("#", 1)[0]


def scan(path: str) -> list[tuple[int, str]]:
    """Return (line number, offending statement) for every unchecked native invocation."""
    with open(path, "r", encoding="utf-8-sig") as handle:
        raw_lines = handle.read().splitlines()

    cleaned = [strip_noise(raw) for raw in raw_lines]

    def checked_inline(index: int) -> bool:
        """True if the statement starting at `index` tests $LASTEXITCODE just after it."""
        end = index
        while end < len(cleaned) - 1 and cleaned[end].rstrip().endswith("`"):
            end += 1
        window = cleaned[index:end + 1 + INLINE_CHECK_LOOKAHEAD]
        return any(LAST_EXIT_CODE.search(line) for line in window)

    offenders: list[tuple[int, str]] = []
    # Depth of the `function Invoke-Native { ... }` body: the `& $Command` inside the helper
    # is the one call that is allowed to look unchecked, because it *is* the check.
    in_helper = False
    helper_depth = 0
    # A scriptblock argument can span lines: `Invoke-Native {` ... `}`. Track its braces so
    # continuation lines are not reported.
    block_depth = 0

    for number, raw in enumerate(raw_lines, start=1):
        line = strip_noise(raw)
        opens, closes = line.count("{"), line.count("}")

        if FUNCTION_DEF.search(line):
            in_helper = True
            helper_depth = opens - closes
            continue
        if in_helper:
            helper_depth += opens - closes
            if helper_depth <= 0:
                in_helper = False
            continue

        if block_depth > 0:
            block_depth += opens - closes
            continue

        checked = bool(INVOKE_NATIVE.search(line))
        if checked:
            block_depth = opens - closes
            continue

        stripped = line.strip()
        if not stripped:
            continue

        is_native = bool(CALL_OPERATOR.match(line))
        if not is_native:
            match = TOOL_CALL.match(line)
            is_native = bool(match and match.group(1).lower() in NATIVE_TOOLS)

        if is_native and not checked_inline(number - 1):
            offenders.append((number, raw.strip()))

    return offenders


def main() -> int:
    verbose = "--verbose" in sys.argv
    paths = sorted(glob.glob(os.path.join(SCRIPTS, "*.ps1")))

    # An empty check is a broken check. If the scripts move or get renamed, this guard must
    # say so rather than report success over a directory it no longer reads.
    if not paths:
        print("ERROR: found no PowerShell scripts under scripts/ -- this guard is not "
              "checking anything. Update SCRIPTS if the layout changed.", file=sys.stderr)
        return 1

    failures = 0
    for path in paths:
        name = os.path.relpath(path, REPO).replace("\\", "/")
        offenders = scan(path)
        for number, statement in offenders:
            print(f"{name}:{number}: native command is not exit-code checked: {statement}",
                  file=sys.stderr)
            failures += 1
        if verbose and not offenders:
            print(f"ok: {name}")

    if failures:
        print(f"\n{failures} unchecked native invocation(s). $ErrorActionPreference does not "
              "cover native executables -- wrap each one in Invoke-Native { ... } so a "
              "non-zero exit fails the script.", file=sys.stderr)
        return 1

    print(f"checked {len(paths)} PowerShell scripts; every native invocation is exit-code checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
