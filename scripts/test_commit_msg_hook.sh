#!/bin/sh
#
# Behavioural tests for scripts/hooks/commit-msg.
#
# A text-contract test -- reading the hook and asserting it mentions each pattern -- would
# pass on a hook whose `grep -v` silently matched nothing. The hook's entire value is what it
# does to a message, so every case here runs the real hook against a real file and checks the
# bytes that come back.
#
# Run locally with:  sh scripts/test_commit_msg_hook.sh
# CI runs it in the script-checks job.

set -eu

repo="$(git rev-parse --show-toplevel)"
hook="$repo/scripts/hooks/commit-msg"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

failures=0
checks=0

# Reports one assertion. Keeps the count honest so a silently skipped case cannot look green.
assert() {
    checks=$((checks + 1))
    if [ "$2" = "$3" ]; then
        printf '  ok   %s\n' "$1"
    else
        printf '  FAIL %s\n        expected: %s\n        actual:   %s\n' "$1" "$3" "$2"
        failures=$((failures + 1))
    fi
}

run_hook() {
    set +e
    "$hook" "$1" >"$work/stderr" 2>&1
    status=$?
    set -e
    return $status
}

# --- stripped silently: trailer-shaped boilerplate no human typed --------------------------

# The address here deliberately belongs to no vendor in the pattern list. An earlier version
# of this case used `Co-authored-by: Cursor <cursoragent@cursor.com>`, which matches two
# patterns at once -- so the case still passed with the co-authored pattern deleted, and
# proved nothing about it. Each case below exercises exactly one rule.
printf 'feat: add a thing\n\nSome body text.\n\nCo-authored-by: Someone <someone@example.com>\n' \
    > "$work/trailer"
run_hook "$work/trailer" || true
assert "Co-authored-by trailer is removed" \
    "$(grep -c -i 'co-authored' "$work/trailer" || true)" "0"
assert "the subject line survives" \
    "$(head -n 1 "$work/trailer")" "feat: add a thing"
assert "the body survives" \
    "$(grep -c 'Some body text' "$work/trailer" || true)" "1"
# A trailer sits behind a blank line. Deleting it must not strand that blank line, or every
# such commit ends with trailing whitespace.
assert "no blank line is stranded at the end" \
    "$(tail -n 1 "$work/trailer")" "Some body text."

printf 'feat: y\n\nGenerated with [Some Tool](https://example.com)\n' > "$work/footer"
run_hook "$work/footer" || true
assert "generated-with footer is removed" \
    "$(grep -c -i 'generated with' "$work/footer" || true)" "0"

# The vendor-address rule stands on its own: this line is not trailer-shaped at all, so only
# the address pattern can catch it.
printf 'feat: z\n\nReviewed by agent <cursoragent@cursor.com> before landing.\n' > "$work/vendor"
run_hook "$work/vendor" || true
assert "a vendor agent address is removed" \
    "$(grep -c -i 'cursoragent' "$work/vendor" || true)" "0"

printf 'feat: w\n\nCo-committed-by: Someone <someone@example.com>\n' > "$work/cocommit"
run_hook "$work/cocommit" || true
assert "Co-committed-by trailer is removed" \
    "$(grep -c -i 'co-committed' "$work/cocommit" || true)" "0"

# --- refused loudly: an authorship claim written into prose --------------------------------
#
# Reworded prose is the author's call, not a hook's, so this one is reported rather than
# silently edited. It is also the case that proves the hook and CI share a definition: the
# hook does not match this itself, it fails because check_commit_attribution.py does.

printf 'fix: thing\n\nThis was written by Claude, honestly.\n' > "$work/prose"
if run_hook "$work/prose"; then
    assert "a prose authorship claim is refused" "accepted" "rejected"
else
    assert "a prose authorship claim is refused" "rejected" "rejected"
fi

# --- left alone: an ordinary message must come back byte-identical -------------------------

printf 'fix: a clean message\n\nNothing wrong here.\n' > "$work/clean"
cp "$work/clean" "$work/clean.orig"
run_hook "$work/clean" || true
if cmp -s "$work/clean" "$work/clean.orig"; then
    assert "a clean message is untouched" "unchanged" "unchanged"
else
    assert "a clean message is untouched" "modified" "unchanged"
fi

# Naming a tool is not claiming it wrote the code, so prose that merely mentions one stays.
printf 'docs: explain how the Cursor workflow is configured\n' > "$work/mentions"
run_hook "$work/mentions" || true
assert "prose that merely names a tool is kept" \
    "$(grep -c 'Cursor workflow' "$work/mentions" || true)" "1"

# --- comments: git discards these after the hook runs, so they are not a claim --------------

printf 'feat: x\n\n# Co-authored-by: someone, in a comment git will drop\n' > "$work/comment"
if run_hook "$work/comment"; then
    assert "a trailer inside a comment does not fail the commit" "accepted" "accepted"
else
    assert "a trailer inside a comment does not fail the commit" "rejected" "accepted"
fi

# --- report --------------------------------------------------------------------------------

printf '\n'
if [ "$failures" -eq 0 ]; then
    printf 'commit-msg hook: %d checks passed\n' "$checks"
    exit 0
fi
printf 'commit-msg hook: %d of %d checks failed\n' "$failures" "$checks"
exit 1
