# Documentation index

Each document has one owner-level purpose. `ROADMAP.md` is the only backlog; temporary
session notes and parallel TODO lists do not belong in the repository.

## Start here

- [Contributing](../CONTRIBUTING.md) — the conventions CI enforces, and the ones you cannot
  guess. Read it before a first change.
- [Running Snapback](running.md) — build, test, launch, permissions, and troubleshooting by
  platform.
- [Architecture](ARCHITECTURE.md) — current runtime shape, module boundaries, threading,
  storage, IPC, and platform seams.
- [Roadmap](ROADMAP.md) — the six-month phase sequence, then ordered open work and release
  blockers. The only backlog.

## Engineering references

- [Testing strategy](testing_strategy.md) — local, CI, smoke, and untested boundaries.
- [Benchmarking](benchmarking.md) — benchmark commands, interpretation, and measured results.
- [Dependency policy](dependencies.md) — immutable C++ dependency pins and update process.
- [Packaging](PACKAGING.md) — Windows artifacts/signing and cross-platform packaging status.
- [Windows demo](windows_demo.md) — the Windows end-to-end demo runbook.
- [Architecture decisions](adr/README.md) — accepted decisions and questions awaiting ADRs.

## Point-in-time records

- [Audit 2026-08-19](audit-2026-08-19.md) — AUD/FWD findings from a full read of the tree.
  A dated record, not a second backlog: open work it identifies is sequenced in
  [`ROADMAP.md`](ROADMAP.md).
- [Roadmap archive](roadmap_archive.md) — completed roadmap items, kept for history.
  Deliberately stale as of each entry's completion date.

The root [README](../README.md), [frontend README](../frontend/README.md), and
[scripts README](../scripts/README.md) remain short entry points for their own audiences.
Detailed history belongs in Git and accepted ADRs, not in operational runbooks.
