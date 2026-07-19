# CLAUDE.md — Snapback C++ rewrite

This file is auto-loaded. Read it fully before doing anything in this repo.

## What this project is

A **from-scratch C++ rewrite of Snapback**, replacing the Rust/Tauri core. The
original lives at `../Snapback` (a sibling folder). It is the **spec and source of
truth** — when in doubt about behavior, thresholds, schema, or the IPC contract,
open the matching Rust file and port it faithfully.

The goal is not just a working app — it's for the human (Kassa) to **understand and
be able to defend every line**. Teaching quality matters as much as correctness.

## How to work here (non-negotiable)

These come from Kassa's standing preferences. Follow them on every change:

1. **Small, reviewable increments.** One coherent piece at a time. Never dump a
   whole subsystem in one shot.
2. **The work loop, every change:** (1) write the code → (2) write/adjust tests →
   (3) **explain it senior-to-junior so Kassa learns** → (4) suggest a one-line
   commit message. Do not skip the explanation.
3. **Explanations are ADHD-friendly:** lead with a **bold one-line takeaway**, then
   short scannable bullets. No walls of text.
4. **Claude commits; Kassa pushes.** Claude runs `git add`/`git commit` with a terse
   one-liner (e.g. `feat: SPSC ring buffer`) under Kassa's identity
   (KassaSana / kassaplayz@gmail.com). **Never push.** **ABSOLUTELY NO AI attribution,
   ever** — no `Co-Authored-By`, no "Generated with Claude", no AI footprint of any kind
   in any commit message. Non-negotiable.
5. **Teach the C++ vs Rust delta.** Whenever the port touches something Rust did for
   free (ownership, `Result`, `Option`, `Send`/`Sync`, trait dispatch), name what
   Rust guaranteed and how we uphold it here (RAII, `std::optional`,
   `std::expected`, mutexes, virtual dispatch). That delta is the whole point.

## Architecture (mirror the Rust, don't reinvent)

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full Rust→C++ module map. The shape:

```
capture/ (OS hooks) → ring buffer → engine/ (features→classifier→onnx) → storage/ (SQLite)
                                          ↓
                                     snapback/ (context recovery)
                                          ↓
                          app/ (state + webview IPC bridge) → React UI (reused as-is)
```

Do **not** rebuild the retired 4-layer design (C++→ZeroMQ→Python→Spring) described
in `../Snapback/docs/ARCHITECTURE.md`. That's the thing the project migrated away
from. We're porting **today's** single-binary v0.2 app.

## Toolchain & libraries

- **C++20**, CMake ≥ 3.20. Compiler: MSVC (this is a Windows machine) via
  `cmake --build build --config Release`.
- Deps (pulled by CMake FetchContent unless noted): `webview/webview` (replaces
  Tauri), `nlohmann/json` (replaces serde_json), SQLite amalgamation in
  `third_party/sqlite/` (replaces rusqlite), ONNX Runtime under
  `third_party/onnxruntime/` behind the `SNAPBACK_ONNX` option (replaces `ort`),
  `doctest` (replaces `cargo test`).
- Frontend is **not** rewritten — see [frontend/README.md](frontend/README.md).

## Ground rules for the port

- **Port behavior, not syntax.** Match observable behavior (numbers, states, wire
  format), then write idiomatic C++ — don't transliterate Rust.
- **The IPC contract is sacred.** Command names in `src/app/commands.hpp` must match
  the frontend's `invoke(...)` calls and the Rust `generate_handler![...]` list. A
  mismatch silently breaks the UI.
- **The feature-vector order is a contract** with the model + CSV export. Never
  reorder `engine/features.hpp` without retraining + updating the exporter.
- **DB filename stays `focoflow.db`** (install compatibility — see the Rust README).
- **Every ported module gets a test** before moving on. Green tests are the
  definition of "phase done."
- **Memory safety is now manual.** The capture path runs an OS hook thread + an
  engine thread over a shared buffer. Use the SPSC ring buffer, keep the hook
  callback allocation-free, and reason explicitly about lifetimes across the FFI
  boundary. This is where bugs will hide.

## Where to start

The Rust→C++ port is **complete** — the phase playbook that got us here is archived in
[docs/PORT_HISTORY.md](docs/PORT_HISTORY.md) as a teaching record. New work comes from
[docs/ROADMAP.md](docs/ROADMAP.md); start at **Tier 0** (the last parity/CI gaps) before
reaching for shipping or platform polish.

## Status (2026-07-19)

The port runs end-to-end on Windows: capture → engine → SQLite → webview IPC → reused React
UI. ONNX is optional and verified. CI guards it on three OSes with ASan/UBSan/TSan (plus a
dual-language Rust/C++ feature-parity job); a tag-driven release workflow packages Windows
builds with optional Authenticode signing. This table is a summary — **the single source
of truth for open work is [docs/ROADMAP.md](docs/ROADMAP.md)**; when they disagree, the
roadmap wins.

| Area | Status |
|------|--------|
| Core pipeline (types → storage → engine → app → IPC → ONNX) | **Done** — 22 headless test suites green |
| Windows capture / overlay / tray | **Done** |
| Linux capture (evdev) | **Done** — real evdev with polling fallback |
| macOS capture / overlay / tray | **Polling only** — native `CGEventTap` + tray are Roadmap Tier 0.3 / Tier 3.1 |
| Packaging / CI | **Partial** — CI + parity job + tag release + signing wired; cert itself is Roadmap 0.4b |
| Idle/AFK, pomodoro, confidence gating, retention prune | **Done (backend)** — pomodoro/focus-summary UI are Roadmap 0.7 / 0.8 |
| Logger / notifications | **Built, not wired** — adoption + real-event trigger are Roadmap 0.5 / 0.6 |
| Perf / safety hardening | **Done** — WAL, stmt cache, two-lock split, interning, ASan/TSan, concurrent tests |

**Do next (Roadmap Tier 0):** close the wiring gaps — adopt the logger (0.5), fire the
toast on real distractions (0.6), pomodoro UI (0.7), expose focus summary (0.8) — then
native macOS capture (0.3). See [docs/ROADMAP.md](docs/ROADMAP.md).
