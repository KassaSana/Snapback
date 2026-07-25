# ADR-0002 — Protect `master` from red CI

- **Status:** Accepted
- **Date:** 2026-07-24
- **Roadmap item:** 6.2
- **Decided by:** Kassa

## Question

What rule prevents new commits from landing on `master` while the repository's required CI
checks are failing?

## Context

The roadmap recorded repeated failed `master` runs while commits continued to land. CI runs
on pushes and pull requests targeting `master` (`.github/workflows/ci.yml:15-18`) and covers
the portable C++ suite, sanitizers, ONNX, frontend, desktop, and documentation checks. A red
`master` therefore means at least one guard is failing, not merely that an optional check was
unavailable.

Two forces pull in opposite directions:

- A hard merge gate preserves `master` as a trustworthy integration branch, but can delay work
  when a flaky or infrastructure-only check needs repair.
- A convention is easier to bypass, but it recreates the failure this item documents: a red
  branch stops being a useful signal.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Protected `master` with required CI checks | Enforced by the hosting platform; red `master` cannot be normalized | Requires configuring and maintaining the required-check list |
| B. Written convention only | No hosting configuration; easy to adopt immediately | Depends on every maintainer remembering and honoring it |

## Decision

`master` is a protected integration branch. Merges require all designated CI checks to pass;
direct pushes are disabled for normal work.

The required checks are the contexts currently produced by `.github/workflows/ci.yml`:

- `Security audit / frontend`
- `C++ headless tests / windows-latest`, `C++ headless tests / macos-latest`, and
  `C++ headless tests / ubuntu-latest`
- `Sanitizers (asan+ubsan)` and `ThreadSanitizer`
- `Rust/C++ feature parity fixtures`
- `ONNX backend / windows` and `ONNX backend / linux`
- `Benchmark smoke / linux`
- `Frontend mock tests`
- `Windows desktop integration smoke`
- `Desktop app build / macos-latest` and `Desktop app build / ubuntu-latest`
- `Docs smoke`

## Why

Option A wins because the problem is enforcement, not a missing sentence in a document. The
required checks should cover the CI jobs that define repository health, including the core
tests, frontend checks, platform builds, and docs invariants. If a check is flaky, the fix is
to repair or temporarily adjust that check deliberately—not to merge around a red result.

Option B remains the emergency fallback only until branch protection is configured; it is not
the repository's steady-state policy. This decision should be revisited if the CI matrix is
split into independent release tracks or if a required check is shown to be non-diagnostic.

## Consequences

- Repository hosting must configure `master` branch protection with the required CI checks.
- Normal work lands through a branch and pull request; a red required check blocks merging.
- Emergency recovery remains an explicit administrator action and must be documented in the
  pull request rather than silently bypassing the policy.
- This closes roadmap item 6.2 and leaves 9.1 as the next decision in the ordered sequence.

## Revisit if

Reopen this ADR if required CI checks become persistently non-diagnostic, the project adopts
separate integration branches, or the release workflow no longer depends on `master` staying
green.
