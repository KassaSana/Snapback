# Snapback C++ — architecture sketch

This mirrors **today's** Snapback (v0.2: one Tauri binary), not the retired 4-layer
C++→ZeroMQ→Python→Spring design in `../FocoFlow-1/docs/ARCHITECTURE.md`. That older
design is the thing the project already migrated *away from* — don't rebuild it.

> Paths corrected 2026-07-20: every `../Snapback/...` reference in this file pointed at
> **this repo itself** — self-referential, so anyone following one landed back where they
> started and concluded the Rust spec was gone. The same bug was fixed in `CLAUDE.md`
> earlier the same day but survived in five other docs. The Rust original is `../FocoFlow-1`.

## The shape (unchanged from Rust)

```
 ┌─────────────────────────────────────────────────────────────┐
 │  Native process (C++)                                        │
 │                                                              │
 │   capture/ ──▶ ring buffer ──▶ engine/ ──▶ storage/ (SQLite) │
 │   (OS hooks)                   (features,                     │
 │      │                          classifier,                  │
 │      │                          onnx)                         │
 │      │                            │                          │
 │      └──────────▶ snapback/ ◀─────┘   (context recovery)     │
 │                     │                                        │
 │            webview IPC bridge (app/commands.hpp)             │
 │                     │                                        │
 └─────────────────────┼────────────────────────────────────────┘
                       │  bind() / eval()   ← webview/webview
                       ▼
              System WebView (WebView2 / WKWebView / WebKitGTK)
                       │
              React dashboard  (reused from ../FocoFlow-1/frontend, unchanged)
```

## Module map: Rust → C++

> ⚠️ **This table is the pre-port *plan*, not the shipped shape.** Verified 2026-07-20 —
> five of the C++ paths below do not exist, and two library choices were never taken.
> Reconciling it is Roadmap **12.1**; until then, trust the code over this table.
>
> | This table says | Reality |
> |---|---|
> | `app/events.hpp` | Never created — `emit()` lives in `app/commands.hpp:222` |
> | `engine/goal_alignment.hpp/.cpp` | Folded into `engine/app_context.{hpp,cpp}` |
> | `capture/active_window_*.cpp` | One file, `capture/active_window.cpp`, `#if`-branched |
> | `capture/permissions_*.cpp` | One file, `capture/permissions.cpp`, `#if`-branched |
> | `snapback/overlay.hpp/.cpp` | Split as `overlay_common` / `overlay_windows` / `overlay_stub` |
> | Logging via `spdlog` | Hand-written `util/logger.hpp` — spdlog is not a dependency |
> | UUID via `stduuid` | Not a dependency |
>
> The rest of the table matches. This is a *sketch that predates the build*, kept because
> the Rust→C++ reasoning is the teaching value; the file list is just stale.

| Rust (`src-tauri/src/`)        | C++ (`src/`)                     | Library / mechanism |
|--------------------------------|----------------------------------|---------------------|
| `lib.rs`, `main.rs`            | `main.cpp`                       | plain `int main()` + webview loop |
| `types.rs`                     | `types.hpp`                      | structs/enums + `nlohmann::json` (de)serialize |
| `state.rs` (`AppState`)        | `app/state.hpp/.cpp`             | `std::mutex` / `std::shared_ptr` |
| `commands.rs` (`#[tauri::command]`) | `app/commands.hpp/.cpp`     | `webview.bind("name", handler)` |
| `events.rs` (emit to frontend) | `app/events.hpp`                 | `webview.eval("window.__snapback.emit(...)")` |
| `capture/thread.rs`            | `capture/capture_thread.hpp/.cpp`| `std::thread` |
| `capture/mod.rs` (input hooks) | `capture/input_hook_*.cpp`       | Win32 `SetWindowsHookExW` / macOS `CGEventTap` / X11 |
| `capture/active_window.rs`     | `capture/active_window_*.cpp`    | Win32 `GetForegroundWindow` / macOS/X11 equivalents |
| `capture/permissions.rs`       | `capture/permissions_*.cpp`      | per-OS permission probes |
| (rdev's bounded channel)       | `capture/ring_buffer.hpp`        | hand-written lock-free SPSC ring buffer |
| `engine/features.rs`           | `engine/features.hpp/.cpp`       | plain math (trivial port) |
| `engine/classifier.rs`         | `engine/classifier.hpp/.cpp`     | plain math + optional ONNX |
| `engine/onnx_model.rs`         | `engine/onnx_model.hpp/.cpp`     | **ONNX Runtime C++ API** (easier than Rust `ort`) |
| `engine/app_context.rs`        | `engine/app_context.hpp/.cpp`    | string matching |
| `engine/focus_modes.rs`        | `engine/focus_modes.hpp`         | thresholds (ported verbatim below) |
| `engine/goal_alignment.rs`     | `engine/goal_alignment.hpp/.cpp` | plain math |
| `storage/mod.rs`               | `storage/storage.hpp/.cpp`       | **SQLite C API** (easier than Rust `rusqlite`) |
| `snapback/tracker.rs`          | `snapback/tracker.hpp/.cpp`      | state machine |
| `snapback/title_parser.rs`     | `snapback/title_parser.hpp/.cpp` | string parsing |
| `snapback/overlay.rs`          | `snapback/overlay.hpp/.cpp`      | second webview window |
| `tray.rs`                      | `app/tray_*.cpp`                 | per-OS tray (no free abstraction) |
| `training_deploy.rs`           | `app/training_deploy.hpp/.cpp`   | `std::filesystem` + subprocess |

## Libraries

| Concern            | Rust today            | C++ choice                         | Notes |
|--------------------|-----------------------|------------------------------------|-------|
| Window + webview   | Tauri                 | `webview/webview` (header-only)    | same OS webviews Tauri wraps |
| IPC frontend↔native| Tauri `invoke`/events | `webview.bind` / `webview.eval`    | you define the JSON protocol |
| SQLite             | `rusqlite` (bundled)  | SQLite amalgamation (`sqlite3.c`)  | it's literally a C library |
| ONNX inference     | `ort` crate           | ONNX Runtime C++ API               | native C++ first-class |
| JSON               | `serde_json`          | `nlohmann/json`                    | header-only |
| Threads / sync     | `std::thread`, `parking_lot` | `std::thread`, `std::mutex` | std is enough |
| UUID               | `uuid` crate          | `stduuid` (header-only)            | or Win32 `UuidCreate` |
| Time               | `chrono` crate        | `std::chrono` + `date`/`std::format` | |
| CSV export         | `csv` crate           | hand-rolled (it's just commas)     | matches `storage` export |
| Global hooks       | `rdev`                | **hand-written per OS**            | ⚠️ the real cost |
| Active window      | `active-win-pos-rs`   | **hand-written per OS**            | ⚠️ |
| Logging            | `log` + `env_logger`  | `spdlog` (or `std::print`)         | |
| Tests              | `#[test]` / `cargo test` | `doctest` (header-only)         | |

## What you gain

- **SQLite & ONNX bindings are cleaner** — both are C/C++ libraries; you skip the
  wrapper-crate layer entirely.
- **Feature math ports 1:1** — `features.rs`, `classifier.rs`, `goal_alignment.rs`
  are plain arithmetic.
- **Familiarity**, if C++ is your home turf.

## What you lose (be honest with yourself here)

- **Every Tauri battery**: installers (NSIS/DMG), auto-updater, IPC plumbing, tray
  abstraction, capability/security model. You rebuild each one.
- **Memory safety in exactly the danger zone.** This app runs threads + global OS
  hooks + FFI at once — the precise place C++ footguns live (use-after-free across
  the hook callback boundary, data races on the event buffer). Rust's borrow
  checker prevents these *by construction*; here they're your responsibility.
- **The whole test/CI setup** (`cargo test`, feature-parity harness, GitHub
  release workflows) gets reworked.

## Why this port is a rewrite, not a language switch

Roughly the split, by effort:

- **Easy (port the math):** `engine/`, `types`, `storage` schema — days.
- **Medium (re-solve with a lib):** webview UI + IPC bridge, SQLite/ONNX wiring — a week.
- **Hard (hand-write per OS):** global input capture, active-window, permissions,
  tray, installers, updater — the bulk of the time, and the risky part.

The project already went C++ → Rust once. Before reversing that, the question worth
answering isn't "can it be done" (yes) but "what made them leave C++" — the answer
is almost certainly the Hard row above.

## focus_modes thresholds (ported verbatim from `engine/focus_modes.rs` / `types.rs`)

Kept here so the port has a concrete, checkable reference point:

| Mode      | risk_threshold | hyperfocus_minutes |
|-----------|----------------|--------------------|
| Deep      | 0.55           | 90                 |
| Normal    | 0.70           | 120                |
| Recovery  | 0.85           | 45                 |
