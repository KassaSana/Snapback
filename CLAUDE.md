# CLAUDE.md — Snapback C++ rewrite

This file is auto-loaded. Read it fully before doing anything in this repo.

## What this project is

A **from-scratch C++ rewrite of Snapback**, replacing the Rust/Tauri core. The
original lives at **`../FocoFlow-1`** — same GitHub repo (`KassaSana/Snapback`),
different local directory name. It is the **spec and source of truth** — when in doubt
about behavior, thresholds, schema, or the IPC contract, open the matching Rust file
(`../FocoFlow-1/src-tauri/src/...`) and port it faithfully.

> Corrected 2026-07-20: this used to say the original was at `../Snapback`. From here,
> `../Snapback` resolves to **this repo itself** — the path was self-referential, so
> anyone following it found C++ instead of Rust and concluded the spec was gone. CI pulls
> the same Rust tree as ref `main-fresh` (`.github/workflows/ci.yml`).
>
> The reference is not automatically right: it has its own bugs, and at least one
> (`title_parser.rs`, Roadmap 4.11) is a bug we should *not* port faithfully. Port
> behavior, but check whether the behavior is correct first.

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
in `../FocoFlow-1/docs/ARCHITECTURE.md`. That's the thing the project migrated away
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
[docs/ROADMAP.md](docs/ROADMAP.md); start at **Tier 0** (native macOS capture is what's
left there) before reaching for shipping or platform polish.

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
| macOS capture | **Native `CGEventTap`, fixed but unverified on real hardware** — the tap existed all along in `input_hook_macos.mm` (the repo's only `.mm` file, which is why audits missed it) and was silently dying under load; fixed 2026-07-20. Needs a live Mac run — Roadmap 0.3 |
| macOS / Linux overlay + tray | **No-op stubs** — `overlay_stub.cpp` / `tray_stub.cpp` exist so the app links; real ones are Roadmap 3.1 / 3.2 |
| Desktop app off Windows | **Links as of 2026-07-20** — it never had, and no CI job built it; now guarded by the `desktop-app-build` job |
| Packaging / CI | **Partial** — CI + parity job + tag release + signing wired; cert itself is Roadmap 0.4b |
| Idle/AFK, pomodoro, retention prune, focus summary | **Done, backend + UI** |
| Confidence gating | **⚠️ Claimed done, actually dead code** — `confidence.hpp` has no callers and its `[0,100]` threshold can't fire against the classifier's `[0,1]` output. Roadmap 5.3 |
| ONNX inference path | **⚠️ Drops user config** — a deployed model bypasses Block app rules, thrash, drift, and goal alignment. Roadmap 5.1; blocks 2.3 |
| Logger / notifications | **Done, adopted + wired** — leveled logger in storage/state, toast fires on real `snapback` events |
| `dismiss_snapback` | **Done** — was silently unreachable everywhere, which stuck `ContextTracker` in `Recovering` after one snapback per session; now wired natively (Windows) and from the web UI |
| Onboarding wizard (1.1) | **Done** — explains capture + local-only, requests permissions, and now picks a default focus mode |
| Start-on-login (1.3) | **Done on Windows** — HKCU Run key, IPC, settings toggle, and tests; launchd/systemd remain follow-ups |
| Privacy, analytics, summary reports, goal categories, diagnostics | **Done** — five-feature product pass, native + frontend tests green |
| Perf / safety hardening | **Done** — WAL, stmt cache, two-lock split, interning, ASan/TSan, concurrent tests |

**Do next (Roadmap):** Tier 5 — the open findings from the 2026-07-20 engine/storage audit.
Start with 5.2 (ONNX failure writes an empty `focus_state`) and 5.4 (`recap()` biases
auto-labels by focus mode), then verify macOS capture on real hardware (0.3). 1.2, 4.11,
and 5.3 each need a product decision first. See [docs/ROADMAP.md](docs/ROADMAP.md).
