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

- **C++20**, CMake ≥ 3.20.
- **This development machine is macOS** (`Darwin`), not Windows. Corrected 2026-07-20 —
  this file previously asserted "this is a Windows machine", which is why past sessions
  reached for MSVC and Windows paths that don't exist here. Consequences to plan around:
  - **Anything Windows-only cannot be compiled or run locally.** That's
    `overlay_windows.cpp`, `tray_windows.cpp`, `input_hook_windows.cpp`,
    `autostart.cpp`'s Run-key path, and the whole ONNX build (its import lib is
    `onnxruntime.lib`). CI is the only place those are exercised — so **when Windows CI is
    red, they are exercised nowhere.**
  - MSVC is still the *target* compiler for releases (`cmake --build build --config
    Release`); local builds use the host toolchain.
- Deps and where they actually come from:
  - `nlohmann/json` (replaces serde_json) and `doctest` (replaces `cargo test`) —
    CMake `FetchContent`.
  - `webview/webview` (replaces Tauri) — `FetchContent`, only when
    `SNAPBACK_BUILD_APP=ON` (which **defaults to OFF**).
  - **SQLite** (replaces rusqlite) — `FetchContent` of the 3.45.0300 amalgamation by
    default. `CMakeLists.txt:45` *prefers* a vendored copy at `third_party/sqlite/` when
    one exists, **but that directory does not exist in this repo.** Don't go looking for it
    and conclude something is missing; it's an optional override, not a checked-in vendor.
  - **ONNX Runtime** (replaces `ort`) — behind the `SNAPBACK_ONNX` option, expected at
    `third_party/onnxruntime` (`CMakeLists.txt:84`, a `CACHE PATH` you can override).
    **Also absent locally** — CI's `onnx-windows` job vendors it as a build step.
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
[docs/PORT_HISTORY.md](docs/PORT_HISTORY.md) as a teaching record. All new work comes from
[docs/ROADMAP.md](docs/ROADMAP.md), which opens with an ordered **"Start here"** table.
Follow that table; don't pick by tier number, since the tiers are numbered by when they were
opened, not by priority. (Tier 6's "CI is red" blocker cleared 2026-07-22 — 6.1 and 6.4 are
done and CI-confirmed; 6.2 remains as a process decision.)

### Verifying a claim before you act on it

This repo's docs have repeatedly asserted things that were false in both directions — work
described as missing that already existed, and work marked done whose code never ran. As of
2026-07-20 six such claims were found *in this file alone*. So:

- **"X is missing"** → grep for it first, including non-`.cpp` extensions. `.mm` files have
  been missed twice by `*.cpp`/`*.hpp` sweeps.
- **"X is done"** → confirm the code has a caller outside its own test.
- **"CI guards X"** → check the job actually runs. Several jobs `needs:` others and are
  silently *skipped* rather than failed when CI is red (Roadmap 6.3).
- **A test passing does not mean the production path runs.** `seconds_since_session_start`
  was 0.0 in every row ever written because every test passed an explicit origin while
  production passed `nullopt`. Roadmap 7.1 is the same shape, still open.

## Status (2026-07-20, CI rows amended 2026-07-22)

The port runs end-to-end on Windows: capture → engine → SQLite → webview IPC → reused React
UI. CI covers three OSes with ASan/UBSan/TSan plus a dual-language Rust/C++ feature-parity
job; a tag-driven release workflow packages Windows builds with optional Authenticode
signing. This table is a summary — **the single source of truth for open work is
[docs/ROADMAP.md](docs/ROADMAP.md)**; when they disagree, the roadmap wins.

**Windows CI went green again 2026-07-22** — the 6.1 stack overflow (6 MB ring buffer held
inline by value, overflowing Windows' 1 MB thread stack) is fixed: storage moved to the heap,
guarded by a `static_assert` on `sizeof(CaptureThread)`. All 161 test cases now run on all
three OSes. Un-skipping the desktop guard immediately surfaced the next bug — X11 macro
pollution (`#define KeyPress`/`None`/`Status`) breaking the Linux desktop build — fixed via
`src/app/webview_compat.hpp`, the now-only legal include site for `webview.h`.

| Area | Status |
|------|--------|
| Core pipeline (types → storage → engine → app → IPC → ONNX) | **Done** — 161 test cases green on all three OSes as of 2026-07-22 |
| Windows capture / overlay / tray | **Compiled + smoke-tested in CI as of 2026-07-22** — the `windows-desktop-integration` job ran for real (green) once 6.1 unblocked it; still never exercised interactively |
| Linux capture (evdev) | **Done** — real evdev with polling fallback |
| macOS capture | **Native `CGEventTap`, fixed but unverified on real hardware** — the tap existed all along in `input_hook_macos.mm` (the repo's only `.mm` file, which is why audits missed it) and was silently dying under load; fixed 2026-07-20. Needs a live Mac run — Roadmap 0.3 |
| macOS / Linux overlay + tray | **No-op stubs** — `overlay_stub.cpp` / `tray_stub.cpp` exist so the app links; real ones are Roadmap 3.1 / 3.2 |
| Desktop app off Windows | **macOS links (local + CI); Linux fixed 2026-07-22, awaiting CI** — the guard's first real run caught X11 macros breaking the build; `webview_compat.hpp` scrubs them. The job no longer has a `needs:`, so it runs even when CI is red (6.3) |
| Packaging / CI | **Green as of 2026-07-22** (except the Linux desktop link, fix pending push) — 6.1 fixed, all actions off Node 20 (6.4). Parity job, tag release, and signing are wired; cert is 0.4b |
| Engine thread resilience | **Done** — the engine loop catches and logs standard/unknown tick exceptions; invalid UTF-8 is contained by the boundary rather than terminating the process |
| Analytics / summary windows | **⚠️ Silently capped** — both read `recent_predictions(10000)` ≈ 2h46m of use, so the "weekly" report covers this afternoon; hourly buckets are UTC presented as local. Roadmap 7.1 / 7.2 |
| DB schema migrations | **⚠️ None** — all `CREATE TABLE IF NOT EXISTS`, no `user_version`, no `ALTER TABLE`, while we promise `focoflow.db` compatibility with the Rust build. Roadmap 7.3 |
| Idle/AFK, pomodoro, retention prune, focus summary | **Done, backend + UI** |
| Confidence gating | **⚠️ Claimed done, actually dead code** — `confidence.hpp` has no callers and its `[0,100]` threshold can't fire against the classifier's `[0,1]` output. Roadmap 5.3 |
| ONNX inference path | **CI-verified as of 2026-07-22** — 5.1 moved the layering boundary so the model returns raw probabilities and the classifier still applies Block rules, thrash, drift, and goal alignment. **Unblocks 2.3.** The ONNX code is behind `SNAPBACK_ONNX`, **not compiled in the default build**; its `onnx-windows` CI job is green again |
| Logger / notifications | **Done, adopted + wired** — leveled logger in storage/state, toast fires on real `snapback` events |
| `dismiss_snapback` | **Done** — was silently unreachable everywhere, which stuck `ContextTracker` in `Recovering` after one snapback per session; now wired natively (Windows) and from the web UI |
| Onboarding wizard (1.1) | **Done** — explains capture + local-only, requests permissions, and now picks a default focus mode |
| Start-on-login (1.3) | **Done on Windows** — HKCU Run key, IPC, settings toggle, and tests; launchd/systemd remain follow-ups |
| Privacy, analytics, summary reports, goal categories, diagnostics | **Done** — five-feature product pass, native + frontend tests green |
| Perf / safety hardening | **Partial** — WAL, statement cache, two-lock split, interning, ASan/TSan and concurrent tests are all real and in place. But `analytics()`, `summary_report()`, and `session_history()` still issue N+1 queries holding `storage_mutex_`, which the engine tick also takes — so a UI read can stall capture writes. Roadmap 7.12 |

**Do next (Roadmap):** the ordered sequence lives at the top of
[docs/ROADMAP.md](docs/ROADMAP.md) under **"Start here"** — follow it there rather than
duplicating it here. In short (6.1, 6.4, and 8.1 done 2026-07-22): **6.2** (red-master
rule, a decision) → **9.1** (define v1) → **12.3** (`docs/adr/`) → **7.4 + 7.10**
(capture/prediction health, which are the instruments 0.3 needs) → **0.3**.

Then one decision session settles **5.3, 5.4, 1.2, and 7.7** together — they are all the same
question: *what do our scores mean, and on what scale?* Nothing gets coded in that session.
`decision`-tagged items must not be implemented before they're answered; that mistake has
already produced work that had to be reverted.
