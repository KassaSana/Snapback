# Documentation index

Maintained guides describe the current repository. Historical findings and planning live
in the archive; the roadmap is the sole live backlog.

## Start here

- [Contributing](../CONTRIBUTING.md) — development conventions and repository guards.
- [Running](running.md) — prerequisites, build/test/launch commands, permissions, and troubleshooting.
- [Architecture](ARCHITECTURE.md) — current modules, threading, storage, and IPC contracts.
- [Roadmap](ROADMAP.md) — current priorities, open items, decision gates, and binding exclusions.

## Engineering references

- [Testing strategy](testing_strategy.md) — verification layers, CI, and coverage limitations.
- [Benchmarking](benchmarking.md) — workloads, commands, dated measurements, and caveats.
- [Dependencies](dependencies.md) — immutable C++ pins and their update process.
- [Packaging](PACKAGING.md) — release procedure, signing, artifacts, and platform limits.
- [Windows demo](windows_demo.md) — isolated demo setup, switches, and walkthrough.
- [Architecture decisions](adr/README.md) — accepted choices and their reasoning.

## Historical evidence

These documents are not current instructions or additional work queues. Recheck findings
against code before using them; original review dates and evidence limitations are retained.

- [August 19 audit](archive/audit-2026-08-19.md) — original AUD/FWD findings and proposals.
- [Astra review](archive/ASTRA_REVIEW.md) — reconciled systems/product findings and twelve historical slices; reorganization dated September 19, audit date unknown.
- [Fable review draft](archive/fable_Review.md) — independent findings and prior-review verdicts; review date/commit unspecified.
- [Planning history](archive/roadmap-planning-history.md) — dated strategy and audit narratives removed from the live backlog.
- [Roadmap archive](roadmap_archive.md) — completed resolutions and historical evidence for condensed open items, distinguished explicitly.

The root [README](../README.md), [frontend README](../frontend/README.md), and
[scripts README](../scripts/README.md) are entry points for their respective audiences.
[Release notes](../CHANGELOG.md), [security reporting](../SECURITY.md), and
[license notices](../THIRD_PARTY_NOTICES.md) retain their separate public purposes.
