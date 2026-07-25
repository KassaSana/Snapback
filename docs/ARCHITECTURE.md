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

**Reconciled against the tree 2026-07-23 (Roadmap 12.1).** Every C++ path below was
confirmed to exist; every Rust file under `../FocoFlow-1/src-tauri/src/` appears exactly
once. Where the port diverged from the pre-port plan, the **Divergence** column says so —
those rows are the interesting ones.

| Rust (`src-tauri/src/`) | C++ |  Library / mechanism | Divergence |
|--------------------------------|----------------------------------|---------------------|------------|
| `lib.rs`, `main.rs`            | `src/main.cpp`                       | plain `int main()` + webview loop | |
| `types.rs`                     | `src/types.hpp/.cpp`                 | structs/enums + `nlohmann::json` (de)serialize | |
| `state.rs` (`AppState`)        | `src/app/state.hpp/.cpp`             | `std::mutex` / `std::shared_ptr` | |
| `commands.rs` (`#[tauri::command]`) | `src/app/commands.hpp`, `src/app/command_dispatch.hpp` | `webview.bind("name", handler)` | header-only; dispatch split out from the handler bodies |
| `events.rs` (emit to frontend) | `src/app/commands.hpp:222` (`emit()`) | `webview.eval("window.__snapback.emit(...)")` | **no `events.hpp` was ever created** — one function, so it never earned a file |
| `capture/thread.rs`            | `src/capture/capture_thread.hpp/.cpp`| `std::thread` | |
| `capture/mod.rs` (input hooks) | `src/capture/input_hook.hpp` + `src/capture/input_hook_windows.cpp` / `input_hook_macos.mm` / `input_hook_linux.cpp` / `input_hook_posix.cpp` | Win32 `SetWindowsHookExW` / macOS `CGEventTap` / evdev | one file **per OS**, selected in CMake — the macOS one is `.mm` (Objective-C++), which is why `*.cpp` audits keep missing it |
| `capture/active_window.rs`     | `src/capture/active_window.hpp/.cpp` | Win32 `GetForegroundWindow` / macOS / X11 | **one file, `#if`-branched inside** — not `active_window_*.cpp` |
| `capture/permissions.rs`       | `src/capture/permissions.hpp/.cpp`   | per-OS permission probes | **one file, `#if`-branched inside** |
| (rdev's bounded channel)       | `src/capture/ring_buffer.hpp`        | hand-written lock-free SPSC ring buffer | heap-allocated storage, not `std::array` — see 6.1 in the roadmap's Done archive |
| `engine/features.rs`           | `src/engine/features.hpp/.cpp`       | plain math (trivial port) | |
| `engine/classifier.rs`         | `src/engine/classifier.hpp/.cpp`     | plain math + optional ONNX | |
| `engine/onnx_model.rs`         | `src/engine/onnx_model.hpp/.cpp`     | **ONNX Runtime C++ API** (easier than Rust `ort`) | behind `SNAPBACK_ONNX`, **off by default** |
| `engine/app_context.rs`        | `src/engine/app_context.hpp/.cpp`    | string matching | |
| `engine/focus_modes.rs`        | `src/engine/focus_modes.hpp`        | thresholds (ported verbatim below) | header-only |
| `engine/goal_alignment.rs`     | `src/engine/app_context.hpp/.cpp`    | plain math | **folded into `app_context`** — no `goal_alignment.*` file exists |
| `engine/parity.rs`             | `src/engine/feature_parity.hpp/.cpp` + `tools/feature_parity_export.cpp` | CSV export compared across languages in CI | renamed; export split into its own tool binary |
| `engine/mod.rs`                | — | | Rust module plumbing; C++ uses headers |
| `storage/mod.rs`               | `src/storage/storage.hpp/.cpp`       | **SQLite C API** (easier than Rust `rusqlite`) | |
| `snapback/tracker.rs`          | `src/snapback/tracker.hpp/.cpp`      | state machine | |
| `snapback/title_parser.rs`     | `src/snapback/title_parser.hpp/.cpp` | string parsing | ⚠️ ported bug — Roadmap 4.11 |
| `snapback/overlay.rs`          | `src/snapback/overlay.hpp` + `src/snapback/overlay_common.cpp` / `overlay_windows.cpp` / `overlay_stub.cpp` | second webview window | **no `overlay.cpp`** — interface + shared logic + per-OS impl; off Windows the stub is a no-op (Roadmap 3.1/3.2) |
| `snapback/mod.rs`, `capture/mod.rs` | — | | Rust module plumbing |
| `tray.rs`                      | `src/app/tray.hpp` + `src/app/tray_common.cpp` / `tray_windows.cpp` / `tray_stub.cpp` | per-OS tray (no free abstraction) | same split as overlay |
| `bench.rs`                     | `benchmarks/bench_snapback.cpp`, `benchmarks/bench_hotpaths.cpp` | behind `SNAPBACK_BUILD_BENCHMARKS` | moved out of `src/` |
| `label_shortcuts.rs`           | — **not ported** | Tauri global-shortcut plugin | ⚠️ **genuine gap**, see below |

`training_deploy` has no Rust counterpart — it is listed in the C++-only table below.
Earlier versions of this file mapped it to a `training_deploy.rs` that does not exist.

### C++ modules with no Rust counterpart

These exist because C++ gave us nothing for free, or because the feature was added after
the port. Not divergences — additions.

| C++ | Why it exists |
|--------------|---------------|
| `src/result.hpp` | Stands in for Rust's `Result` — the error channel the language handed us |
| `src/util/logger.hpp` | Stands in for `log` + `env_logger`. **Hand-written; `spdlog` was never taken as a dependency** |
| `src/util/time.hpp` | `chrono` helpers; Rust leaned on the `chrono` crate |
| `src/app/settings.hpp/.cpp` | Settings persistence Tauri's plugin ecosystem provided |
| `src/app/autostart.cpp` | Start-on-login (Windows HKCU Run key) — a Tauri plugin in Rust |
| `src/app/notification.hpp` | Toast notifications — a Tauri plugin in Rust |
| `src/app/frontend_assets.hpp/.cpp` | Locating/serving the built React bundle, which Tauri embedded |
| `src/app/ipc_shim.hpp` | The `window.__snapback` JS shim Tauri's `invoke` provided |
| `src/app/webview_compat.hpp` | **The only legal include site for `webview.h`** — scrubs X11's `#define KeyPress/None/Status`, which broke the Linux build (Roadmap 6.3) |
| `src/app/version.hpp` | Version compiled in from CMake (Roadmap 9.2) |
| `src/app/training_deploy.hpp/.cpp` | `std::filesystem` + subprocess model deployment |
| `src/engine/idle_detector.hpp` | Idle/AFK detection |
| `src/engine/pomodoro.hpp/.cpp` | Pomodoro timer |
| `src/engine/focus_summary.hpp` | Session focus summary |
| `src/engine/confidence.hpp` | ⚠️ **dead code** — no callers, units inverted. Roadmap 5.3 |

### Rust modules not ported

- **`label_shortcuts.rs` (143 lines, wired in `lib.rs:73`) has no C++ equivalent.** It
  registers global hotkeys for labelling the current window as focused/distracted — a
  Tauri global-shortcut plugin call in Rust, and hand-written per-OS work here, which is
  presumably why it was skipped. Nothing in `src/`, `tests/`, or `frontend/src` mentions it.
  It was invisible because this map never listed the file. Not currently a roadmap item.
- `*/mod.rs` — Rust module declarations with no C++ analogue.

## Libraries

**Reconciled 2026-07-23 (Roadmap 12.1) — this is what we actually depend on**, not what
was planned. The whole third-party list is four entries; everything else is hand-written
or `std`. "How it arrives" matters: three of the four are `FetchContent`, so a clean
clone + network is all you need.

| Concern            | Rust today            | C++ **as built**                   | How it arrives |
|--------------------|-----------------------|------------------------------------|----------------|
| Window + webview   | Tauri                 | `webview/webview` 0.12.0           | `FetchContent`, **only when `SNAPBACK_BUILD_APP=ON`** (defaults OFF) |
| IPC frontend↔native| Tauri `invoke`/events | `webview.bind` / `webview.eval`    | ours — `app/ipc_shim.hpp` defines the JSON protocol |
| SQLite             | `rusqlite` (bundled)  | SQLite amalgamation 3.45.0300      | `FetchContent`; `third_party/sqlite/` overrides it if present, and **is not in this repo** |
| JSON               | `serde_json`          | `nlohmann/json`                    | `FetchContent`, header-only |
| Tests              | `#[test]` / `cargo test` | `doctest`                       | `FetchContent`, header-only |
| ONNX inference     | `ort` crate           | ONNX Runtime C++ API               | vendored at `third_party/onnxruntime`, behind `SNAPBACK_ONNX`; **absent locally**, CI vendors it |
| Threads / sync     | `std::thread`, `parking_lot` | `std::thread`, `std::mutex` | std |
| UUID               | `uuid` crate          | **hand-written** `make_uuid_v4()` (`storage.cpp:228`) | `std::random_device` — **`stduuid` was never taken** |
| Time               | `chrono` crate        | `std::chrono` + `<ctime>`/`<iomanip>` in `util/time.hpp` | std — **no `date` library, no `std::format`** |
| Logging            | `log` + `env_logger`  | **hand-written** `util/logger.hpp` | ours — **`spdlog` was never taken** |
| CSV export         | `csv` crate           | hand-rolled (it's just commas)     | ours — matches `storage` export |
| Global hooks       | `rdev`                | **hand-written per OS**            | ⚠️ the real cost |
| Active window      | `active-win-pos-rs`   | **hand-written per OS**            | ⚠️ |

The pattern in that table is the port's actual story: **every row Rust solved with a crate
that isn't a C library, we wrote ourselves.** SQLite and ONNX were free because they were
already C/C++. UUID, logging, and time were three crates and are now three hand-written
files we own the bugs in.

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
