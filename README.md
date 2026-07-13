# Snapback (C++ port in progress)

**A staged C++ port of Snapback's Rust/Tauri core.**

This repository is no longer just a directory sketch. The core pipeline is wired:
CMake configures, the shared core/capture/app libraries build, and the doctest
suite passes for the real ports (`types` JSON parity, SQLite storage, feature
extraction, classifier guardrails, Windows capture scaffolding, app state,
IPC dispatch, context tracking, training export/deploy orchestration,
`title_parser`, and `app_context`).

The Windows demo path is live enough to build behind `SNAPBACK_BUILD_APP=ON`.
The remaining hardening work is installer signing, deeper GUI automation, and
real macOS/Linux keyboard/mouse hooks beyond active-window polling.

The original lives at `../Snapback` (Rust + Tauri + React). The React frontend is
reused **unchanged** here: the system webview loads the same built assets.

> ⚠️ Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first. It maps every Rust module
> to its C++ counterpart and is honest about what you gain and lose.

## The one-paragraph pitch

Tauri (Rust) is not a library you swap out — it's the *frame* of the app: it gives
you the window, the system webview, the frontend↔backend IPC, installers, tray,
and a security model, all for free. In C++ you assemble that yourself, mostly from
[`webview/webview`](https://github.com/webview/webview) (same system webviews Tauri
uses). The backend pieces — SQLite, ONNX Runtime, feature math — are actually
*easier* in C++ because SQLite and ONNX Runtime are C/C++ libraries first. The
part you re-solve by hand is **global input capture** (per-OS hooks) and everything
Tauri gave you for free.

## Layout

```
snapbackCplusplus/
├── README.md              # you are here
├── docs/                  # architecture, build plan, system design (see docs/)
├── CMakeLists.txt         # json/doctest/sqlite/capture now; webview is fetched once the app target is enabled
├── src/
│   ├── main.cpp           # entry point (Rust: lib.rs / main.rs)
│   ├── types.hpp          # shared structs/enums (Rust: types.rs)
│   ├── app/               # state + webview IPC bridge (Rust: state.rs, commands.rs)
│   ├── capture/           # global hooks + active window + ring buffer (Rust: capture/)
│   ├── engine/            # features, classifier, onnx, app_context (Rust: engine/)
│   ├── storage/           # SQLite persistence (Rust: storage/)
│   └── snapback/          # context recovery: tracker, title parser, overlay (Rust: snapback/)
├── tests/                 # doctest-based unit tests
└── frontend/README.md     # note: reuse ../Snapback/frontend build output as-is
```

## Current Status

- Phase 0: toolchain + test target working
- Phase 1: `types.hpp/.cpp` implemented with camelCase wire-format tests
- Phase 2: SQLite storage opens, migrates, persists core rows, recaps, and exports CSVs
- Phase 3: feature extraction, app-context rules, and heuristic classifier are implemented
- Phase 4: capture backend compiles, Windows active-window lookup/event enrichment is implemented, and ring buffer tests cover drop behavior
- Phase 5: app state and the engine tick persist predictions, feature snapshots, gated context snapshots, and snapback payloads
- Phase 6: webview IPC bindings are wired to the reused frontend contract
- Phase 7: ONNX backend is optional and reports through health/classifier status
- Phase 8: Windows overlay/tray are wired; macOS/Linux report real permission status and poll active-window context where platform tools allow
- Reused frontend copied into `frontend/`
- App target remains gated behind `SNAPBACK_BUILD_APP=ON`
- CI, bundled frontend assets, and unsigned Windows ZIP packaging are wired
- Phase 9 (CI, packaging, parity fixtures) | **Partial** — feature-parity + IPC contract tests, retention prune, TSan; signed installer doc in [`docs/PACKAGING.md`](docs/PACKAGING.md)
- macOS CGEventTap + Linux evdev capture | **Done** (with polling fallback when taps unavailable)

## Build The Current Core

For the full testing plan, see [docs/testing_strategy.md](docs/testing_strategy.md).

```powershell
cmake -S . -B build
cmake --build build --target snapback_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Or run the local mock/headless + frontend suite:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1
```

Run repeatable core-loop benchmarks:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_benchmarks.ps1
```

See [docs/benchmarking.md](docs/benchmarking.md) for what the trace measures and
how to compare results.

## Build The Desktop App

For the Windows demo workflow, use [docs/windows_demo.md](docs/windows_demo.md).

```powershell
cmake -S . -B build -DSNAPBACK_BUILD_APP=ON
cmake --build build --config Release
```

The app target loads bundled `frontend/dist` assets when present. Set
`SNAPBACK_FRONTEND_URL=http://127.0.0.1:5173` only for Vite-based development.
