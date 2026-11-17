# ADR-0008: Protect `master` from red CI

- **Status:** Accepted, branch protection configured 2026-08-27
- **Date:** 2026-08-27

## Question

What stops commits from landing on `master` while CI is failing?

## Context

In July 2026, four of the last five `master` runs had failed and commits kept landing anyway,
including a CI fix that didn't fix CI. A red `master` had stopped meaning anything.

By August, work was already landing through branches and pull requests out of habit. Nothing
enforced it, though, and a habit is exactly what lapses when a one-line fix looks obviously
safe.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. Protected `master` with required CI checks | Enforced by GitHub | The required-check list has to be maintained |
| B. A written convention | Nothing to configure | Already tried; it's what produced the red streak |

## Decision

`master` is protected. Work lands through a pull request, and every job in
`.github/workflows/ci.yml` is a required check. Direct pushes are rejected.

Three settings are deliberately off:

- **No required review.** GitHub doesn't let you approve your own PR, so on a one-person repo a
  review requirement would block every merge. Turn it on if a second maintainer joins.
- **Branches don't have to be up to date before merging.** Requiring it would mean a rebase and
  a full CI re-run every time another PR lands first. Not worth it at this volume.
- **Admins aren't included.** This is the emergency exit. A branch that can't be escaped in an
  emergency gets unprotected in one, and then stays that way. Since the only maintainer is also
  the admin, this makes protection a speed bump for that account rather than a wall: a red merge
  needs an explicit override, and pushing straight to `master` has to be deliberate.

## Why

The problem was enforcement, not a missing rule. All CI jobs are required rather than a fast
subset, because a gate that runs a cheaper approximation of CI looks like proof without being
it. A flaky check now blocks a merge instead of being scrolled past; the answer is to fix the
test, not drop the check.

## Consequences

- Every change, including a one-line doc fix, goes through a PR and a full CI run.
- **The required checks are tied to job names in `ci.yml`.** Renaming a job leaves an old
  required check that never reports, which blocks every merge. Adding a job doesn't make it
  required until it's added to the protection settings. Change both together.
- The release workflow's check that a tag's commit is on `master` and passed CI now holds by
  construction.

## Revisit if

A second maintainer joins (turn on required reviews), or merge queues are adopted.
