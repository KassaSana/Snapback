# ADR-0008 — Protect `master` from red CI

- **Status:** Accepted
- **Applied:** Yes — branch protection configured on `master`, 2026-08-27.
- **Date:** 2026-08-27
- **Roadmap item:** 6.2
- **Decided by:** Kassa

## Question

What rule prevents new commits from landing on `master` while the repository's required CI
checks are failing?

## Context

The finding that opened 6.2, in July 2026: the last five `master` runs were failure, failure,
failure, success (Dependabot only), failure — and five commits landed anyway, including a CI
fix that did not fix CI and was not followed up. The proximate cause was 6.1, which is long
closed. The durable finding was not the outage: it was that **a red `master` had stopped being
a signal**, while several roadmap items went on describing CI as a guard.

**Practice has since moved on its own, and that changes what this decision has to buy.** Work
now lands through `feat/*` branches and pull requests — #46, #47, #48, #49, and #50 all merged
that way. So the common case is already correct by habit. What was missing is that nothing
*enforced* it: until this ADR `master` accepted a direct push from anyone with write access,
and a habit is exactly the thing that lapses at 2am when a one-line fix looks obviously safe.
This ADR therefore ratifies existing practice rather than changing the workflow, and closes
the hole beside it.

CI runs on pushes and pull requests targeting `master` and covers the portable C++ suite on
three platforms plus MinGW, sanitizers, ONNX, benchmarks, the frontend, formatting, the
Windows and Linux desktop builds, the macOS GUI launch, and the documentation invariants. A
red `master` therefore means at least one guard is failing, not merely that an optional check
was unavailable.

Two forces pull in opposite directions:

- A hard merge gate preserves `master` as a trustworthy integration branch, but can delay work
  when a flaky or infrastructure-only check needs repair.
- A convention is easier to bypass, but it recreates the failure this item documents: a red
  branch stops being a useful signal.

**A note for anyone who finds the July draft.** This decision was first written on
2026-07-24 on the branch `phase-6-2-red-master-rule` and never merged; it claimed ADR slot
0002, which `0002-v1-supports-windows-and-macos.md` has since taken. That draft is superseded
by this file before it was ever in force, not after — there is no earlier accepted decision
here to supersede. Its required-check list had also rotted, which is the subject of a
consequence below.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Protected `master` with required CI checks | Enforced by the hosting platform; red `master` cannot be normalized | Requires configuring and maintaining the required-check list |
| B. Written convention only | No hosting configuration; easy to adopt immediately | Depends on every maintainer remembering and honoring it — and this repository has already run that experiment |

## Decision

`master` is a protected integration branch. Work lands through a branch and a pull request,
and every required status check must pass before that pull request can merge. A direct push to
`master` is rejected for anyone the protection applies to.

The required contexts are all fifteen that `.github/workflows/ci.yml` reports, taken from the
check runs GitHub actually published for commit `4732b94` rather than transcribed from the
workflow file:

- `Security audit / frontend`
- `C++ headless tests / windows-latest`, `C++ headless tests / macos-latest`,
  `C++ headless tests / ubuntu-latest`, and `C++ headless tests / windows-gcc`
- `Sanitizers (asan+ubsan)` and `ThreadSanitizer`
- `ONNX backend / linux`
- `Benchmark smoke / linux`
- `Frontend mock tests`
- `Formatting / changed files`
- `Windows desktop integration smoke`
- `Desktop app build / ubuntu-latest`
- `macOS GUI launch smoke`
- `Docs smoke`

Three settings are deliberately *not* enabled, and each one is a decision rather than an
oversight:

- **No approving review is required.** GitHub does not let an author approve their own pull
  request, so on a single-maintainer repository a review requirement does not raise the bar —
  it makes every merge impossible. Add it the day there is a second maintainer.
- **Branches need not be up to date before merging** (`strict: false`). Requiring it would
  force a rebase and a full fifteen-job re-run every time an unrelated pull request landed
  first. The protection this buys — catching two changes that pass separately and fail
  together — is worth that cost on a busy repository and is not worth it here yet.
- **Administrators are not included** (`enforce_admins: false`). This is the emergency hatch,
  and it is better than the alternative hatch, which is disabling protection entirely under
  pressure and then forgetting to restore it. **State plainly what it costs**, because the
  sole maintainer is also the sole administrator: `git push origin HEAD:master` still succeeds
  for that account, and so does anything else holding its credentials. Against the account
  that owns the repository this is a speed bump, not a wall — merging a red pull request in
  the web UI takes an extra confirmation that names the bypass, and pushing straight to
  `master` becomes a thing one has to mean rather than the shortest path. Against every other
  actor it is a wall. Flip this to `true` the moment the speed bump stops being enough.

## Why

Option A wins because the problem is enforcement, not a missing sentence in a document. B was
already the de facto policy while the failure this item records was happening, which is the
strongest evidence available that it does not work.

**Requiring all fifteen checks rather than a fast subset** is the other half of the decision.
A gate that runs a cheaper approximation of CI is worse than no gate, because it looks like
proof — the same reasoning 9.11 used when it made the release gate read CI's conclusion
instead of re-running the matrix. The cost is real: a flaky check now blocks a merge instead
of being scrolled past. That is the intended behavior. 11.12 is an open flaky-test item, and
the correct response to it is to fix the test, not to drop `ThreadSanitizer` from this list.

`enforce_admins: false` is the one place this ADR chooses recoverability over strictness. A
protected branch that cannot be escaped in an emergency gets unprotected in an emergency, and
unprotected is the state it stays in.

## Consequences

- Every change to `master`, including a one-line documentation fix, now costs a branch, a pull
  request, and a full CI run. That is the price of the guard and it is not negotiable per
  change.
- **The required-check list is coupled to the workflow's `name:` values.** Renaming a job in
  `.github/workflows/ci.yml` does not fail anything loudly — the old context simply stops
  reporting, and because it is required-but-absent, merges block until someone updates the
  protection settings. Adding a job is the quieter hazard: it is not required until it is
  added here, so it guards nothing on `master`. Whoever changes the job list changes this list
  in the same pull request.
- **A required check that no longer exists wedges the branch**, which is not hypothetical. The
  July draft required `ONNX backend / windows`, `Rust/C++ feature parity fixtures`, and
  `Desktop app build / macos-latest`; none of the three is produced by CI today. Applying that
  list verbatim would have blocked every merge indefinitely on checks that can never report.
  This is the concrete reason the list above was read from published check runs.
- Emergency recovery is an explicit administrator action and must be stated in the pull
  request or the commit that follows, rather than silently bypassing the policy.
- 9.11's release gate already requires a tagged commit to be an ancestor of `origin/master`
  with a green CI run. Protection makes the premise behind that gate true rather than
  aspirational.

## Revisit if

A second maintainer joins — at which point required reviews become meaningful and should be
turned on. Also reopen if a required check becomes persistently non-diagnostic, if the CI
matrix is split into independent release tracks, or if merge queueing is adopted, since that
changes what `strict` costs.
