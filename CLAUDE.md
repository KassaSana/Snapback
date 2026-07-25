# CLAUDE.md — Snapback C++

This file is auto-loaded. Read it fully before changing this repository.

## What this project is

Snapback is a native C++20 desktop application with a React frontend. The goal
is that Kassa can understand and defend every line, so teaching quality matters
as much as correctness.

The production path is:

```
OS input hooks → SPSC ring buffer → feature extraction → classifier → SQLite
                                      ↓
                              context recovery
                                      ↓
                         native commands → React UI
```

## How to work here

1. Make small, reviewable changes.
2. Write or adjust tests with each behavior change.
3. Explain the design in senior-to-junior terms and suggest a one-line commit.
4. Commit under Kassa's identity, never push, and never add AI attribution.
5. Prefer explicit C++ ownership, error handling, synchronization, and thread
   boundaries. Describe those guarantees in the code when they are not obvious.

## Toolchain and dependencies

- C++20 and CMake ≥ 3.20.
- `nlohmann/json`, doctest, and SQLite are configured through CMake.
- `webview/webview` is fetched only when `SNAPBACK_BUILD_APP=ON`.
- ONNX Runtime is optional and enabled with `SNAPBACK_ONNX`.
- The frontend is Vite + TypeScript + React. Its native boundary is
  `frontend/src/bridge.ts`; do not add a framework IPC package.

## Ground rules

- Match observable behavior: numbers, states, wire format, and error behavior.
- The IPC command names in `src/app/commands.hpp` must match frontend calls.
- The 31-feature order in `src/engine/features.hpp` is a model contract. Change
  it only with an intentional model/fixture update.
- Keep the database filename `focoflow.db`.
- Every production module needs a test before it is considered complete.
- The capture callback must remain allocation-free and the ring buffer must stay
  single-producer/single-consumer.

## Verification

Run the native suite with CMake/CTest and the frontend suite with `npm test` and
`npm run typecheck`. The feature-vector golden fixture is
`fixtures/feature_parity/golden.json`; it is the guard against accidental input
ordering or calculation drift.

The repository docs are the source of truth for current work:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — current module boundaries.
- [docs/ROADMAP.md](docs/ROADMAP.md) — ordered open work.
