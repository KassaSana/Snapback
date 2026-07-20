<!-- Badge points at the repo this README is hosted in. If you push the C++ port to
     an owner/repo other than KassaSana/Snapback, update the two URLs below to match. -->
[![CI](https://github.com/KassaSana/Snapback/actions/workflows/ci.yml/badge.svg)](https://github.com/KassaSana/Snapback/actions/workflows/ci.yml)
[![Release](https://github.com/KassaSana/Snapback/actions/workflows/release.yml/badge.svg)](https://github.com/KassaSana/Snapback/actions/workflows/release.yml)

# Snapback (C++)

**A from-scratch C++ port of Snapback's Rust/Tauri core — a single native binary that
watches for focus drift and snaps you back to what you were doing.**

The pipeline runs end-to-end today: global input capture → feature extraction →
classifier (heuristic or ONNX) → SQLite → a system-webview UI, with a native overlay
and tray. The React frontend from the original is reused **unchanged**.

The original lives at [`../FocoFlow-1`](../FocoFlow-1) (Rust + Tauri + React) and is the
source of truth for behavior, thresholds, and the IPC contract.

> Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first. It maps every Rust module to
> its C++ counterpart and is honest about what you gain and lose in the port.

## Why port it at all

Tauri (Rust) isn't a library you swap out — it's the *frame* of the app. It hands you
the window, the system webview, frontend↔backend IPC, installers, a tray, and a security
model, all for free. In C++ you assemble that yourself, mostly from
[`webview/webview`](https://github.com/webview/webview) (the same system webviews Tauri
wraps). The backend pieces — SQLite, ONNX Runtime, the feature math — are actually
*easier* in C++, since SQLite and ONNX Runtime are C/C++ libraries first. The part you
re-solve by hand is **global input capture** (per-OS hooks) and everything else Tauri
gave you for free.

## What works today

| Capability | Status |
| --- | --- |
| Core pipeline: capture → features → classifier → SQLite → IPC → React UI | ✅ Runs end-to-end |
| Lock-free SPSC ring buffer between the OS hook thread and the engine | ✅ Stress-tested under ASan/TSan |
| SQLite storage: migrations, sessions, predictions, recaps, CSV export | ✅ |
| Heuristic classifier + optional ONNX Runtime backend | ✅ ONNX behind `SNAPBACK_ONNX` |
| Windows: input hooks, active-window enrichment, overlay, tray | ✅ |
| Linux capture: real `evdev` hook (polling fallback otherwise) | ✅ |
| macOS capture: active-window polling today; native `CGEventTap` | 🔜 [Roadmap Tier 0.3](docs/ROADMAP.md) |
| Feature-parity + IPC-contract tests against the Rust fixtures | ✅ |
| CI on Windows/macOS/Linux + ASan, UBSan, TSan; tag-driven release | ✅ |
| Signed installer, macOS/Linux tray & overlay, deeper GUI automation | 🔜 See [docs/ROADMAP.md](docs/ROADMAP.md) |

The desktop app is gated behind `SNAPBACK_BUILD_APP=ON`; the headless core builds and
tests without it.

## Layout

```
snapbackCplusplus/
├── README.md              # you are here
├── docs/                  # architecture, build plan, packaging, testing (see docs/)
├── CMakeLists.txt         # FetchContent for json/doctest/sqlite; webview fetched when the app target is on
├── src/
│   ├── main.cpp           # entry point (Rust: lib.rs / main.rs)
│   ├── types.hpp          # shared structs/enums + JSON wire format (Rust: types.rs)
│   ├── app/               # state + webview IPC bridge (Rust: state.rs, commands.rs)
│   ├── capture/           # global hooks + active window + ring buffer (Rust: capture/)
│   ├── engine/            # features, classifier, onnx, app_context (Rust: engine/)
│   ├── storage/           # SQLite persistence (Rust: storage/)
│   └── snapback/          # context recovery: tracker, title parser, overlay (Rust: snapback/)
├── tests/                 # doctest-based unit + parity tests
├── scripts/               # Windows demo / packaging / benchmark helpers
└── frontend/              # reused React build output (see frontend/README.md)
```

## Build & test the core

Requires C++20, CMake ≥ 3.20, and (on Windows) MSVC.

```powershell
cmake -S . -B build
cmake --build build --target snapback_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Or run the full headless + frontend suite in one shot:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1
```

See [docs/testing_strategy.md](docs/testing_strategy.md) for the complete plan and
[docs/benchmarking.md](docs/benchmarking.md) for the core-loop benchmark harness
(`scripts\run_benchmarks.ps1`).

## Build the desktop app

```powershell
cmake -S . -B build -DSNAPBACK_BUILD_APP=ON
cmake --build build --config Release
```

The app loads the bundled `frontend/dist` assets when present; set
`SNAPBACK_FRONTEND_URL=http://127.0.0.1:5173` only for Vite-based development. For the
full end-to-end Windows walkthrough, see [docs/windows_demo.md](docs/windows_demo.md).

## CI / CD

- **CI** ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs on every push and
  PR: the headless core on Windows/macOS/Linux, AddressSanitizer + UBSan + ThreadSanitizer
  on the manual-memory paths, the ONNX backend, feature-parity fixtures, the React
  frontend, and a no-launch Windows desktop smoke.
- **Release** ([`.github/workflows/release.yml`](.github/workflows/release.yml)) fires on a
  `v*` tag: it builds and tests the Windows package, then publishes a GitHub Release with
  the ZIP and installer attached.

```powershell
git tag v0.2.0
git push origin v0.2.0   # kicks off the release build + publish
```
