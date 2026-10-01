# Agent guidance for Snapback

Snapback is a local-first desktop app that detects focus drift and helps people return to
their work. This is the short, tool-neutral entry point; the linked docs hold the details.

## Where things live

`src/capture/` receives OS input and window context; `src/engine/` extracts features and
classifies them; `src/storage/` persists sessions and predictions; `src/app/` owns state,
commands, and the native bridge. `src/snapback/` handles context recovery. The flow is
capture → ring buffer → engine → SQLite → native commands/events → `frontend/src/` (React).
Native tests are in `tests/`, contract fixtures in `fixtures/`, and verification tools in
`scripts/`. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for threading and wire contracts.

## Read first

1. [`CONTRIBUTING.md`](CONTRIBUTING.md) — the conventions that are enforced but not obvious.
   Read all of it before the first change.
2. [`docs/running.md`](docs/running.md) — per-OS build, test, and run commands.
3. [`docs/testing_strategy.md`](docs/testing_strategy.md) — what each CI job proves.
4. [`docs/ROADMAP.md`](docs/ROADMAP.md) — where work is tracked; pick items from here and
   record progress in the item, as its neighbours do.
5. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and [`docs/adr/`](docs/adr/) — the shape
   of the system and the decisions behind it.

## The one rule with no exceptions

Every commit is the repository owner's own work. No `Co-Authored-By:` trailer, no
"Generated with …" footer, no AI or vendor attribution anywhere in a commit message or PR
body. Do not pass `--author`; do not change `git config`. CI rejects violations after the
fact, which is the expensive way to learn this. Enable the local hook once per clone:

```sh
git config core.hooksPath scripts/hooks
```

## Build and verify

From the repository root on Windows, macOS, or Linux:

```text
python scripts/verify.py
python scripts/verify.py native "CaptureThread"
python scripts/verify.py frontend-unit sessionStatus.test.ts
python scripts/verify.py frontend-component sessionFlow.test.tsx
```

The no-argument command builds and tests the headless C++ core, compiles benchmarks,
typechecks, tests, lints, and builds the frontend, then runs repository guards. Targeted
native selection is a CTest case-name regex; use `ctest --test-dir build -N` to discover cases.
The two frontend modes take an existing file under `frontend/tests/`. The platform wrappers
remain available; [`docs/running.md`](docs/running.md) has build and platform commands.

The full command covers local headless checks. Desktop GUI, sanitizer, and optional deep
checks run separately in CI; see [`docs/testing_strategy.md`](docs/testing_strategy.md).
Formatting commands and the existing-file policy are in [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Invariants and boundaries

- Snapback reflects focus drift; it never blocks an app or starts a session for the user.
- Shipped pages do not fetch remote subresources. Released SQLite migrations are append-only
  and idempotent.
- IPC command and event names must agree across native registration, fixtures, and frontend
  calls. Training and model tooling is developer-only under
  [ADR-0006](docs/adr/0006-trainer-is-developer-tooling.md).
- The roadmap's [Decided not to build](docs/ROADMAP.md#decided-not-to-build-2026-09-22)
  section is binding until the owner agrees to revisit an item.

## Working style

- Inspect relevant implementation, tests, architecture docs, and git state before editing.
  For a roadmap item, confirm its evidence still matches the code and flag scope changes.
- Make the smallest coherent change. Run targeted checks while developing and the full
  `python scripts/verify.py` before finishing; investigate failures. Report what changed,
  why, checks run, and any remaining uncertainty. Never claim unverified behavior.
- Review the diff before one focused commit per concern. Update the roadmap item worked on
  in that commit. Never push unless the owner asks.
- Adding or renaming a native command touches `src/app/command_handlers.cpp`,
  `fixtures/ipc_commands.json` (and its count in `tests/test_ipc_contract.cpp`),
  `frontend/src/api.ts`, and `frontend/demo/backend.ts` together; the contract tests will
  tell you which one you forgot.
- Review, audit, and analysis requests are read-only. Do not edit docs, add roadmap items,
  or commit unless the owner explicitly asks.
- **Context re-entry:** when returning for feedback after parallel or long work, open with a
  plain-language recap of the current task, ask only one question at a time, and end with the
  exact next action needed to move forward.
