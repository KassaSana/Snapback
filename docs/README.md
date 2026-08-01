# Documentation index

Each document has one owner-level purpose. `ROADMAP.md` is the only backlog; temporary
session notes and parallel TODO lists do not belong in the repository.

## Start here

- [Running Snapback](running.md) — build, test, launch, permissions, and troubleshooting by
  platform.
- [Architecture](ARCHITECTURE.md) — current runtime shape, module boundaries, threading,
  storage, IPC, and platform seams.
- [Roadmap](ROADMAP.md) — ordered open work and release blockers.

## Engineering references

- [Testing strategy](testing_strategy.md) — local, CI, smoke, and untested boundaries.
- [Benchmarking](benchmarking.md) — benchmark commands, interpretation, and measured results.
- [Dependency policy](dependencies.md) — immutable C++ dependency pins and update process.
- [Packaging](PACKAGING.md) — Windows artifacts/signing and cross-platform packaging status.
- [Windows demo](windows_demo.md) — the Windows end-to-end demo runbook.
- [Architecture decisions](adr/README.md) — accepted decisions and questions awaiting ADRs.

The root [README](../README.md), [frontend README](../frontend/README.md), and
[scripts README](../scripts/README.md) remain short entry points for their own audiences.
Detailed history belongs in Git and accepted ADRs, not in operational runbooks.
