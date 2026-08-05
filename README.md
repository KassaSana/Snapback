[![CI](https://github.com/KassaSana/Snapback/actions/workflows/ci.yml/badge.svg)](https://github.com/KassaSana/Snapback/actions/workflows/ci.yml)
[![Release](https://github.com/KassaSana/Snapback/actions/workflows/release.yml/badge.svg)](https://github.com/KassaSana/Snapback/actions/workflows/release.yml)

# Snapback (C++)

Snapback is a native C++ desktop app that watches for focus drift and helps you
return to the work you were doing. Its pipeline is input capture → feature
extraction → classification → SQLite → webview UI, with native overlay and tray
support on Windows and macOS.

The React frontend is plain TypeScript and React. It uses the project-owned
`frontend/src/bridge.ts` module to call native commands and subscribe to events.

## What works today

| Capability | Status |
| --- | --- |
| Core pipeline: capture → features → classifier → SQLite → IPC → React UI | ✅ |
| Lock-free SPSC ring buffer | ✅ Stress-tested under ASan/TSan |
| SQLite storage, sessions, predictions, recaps, and CSV export | ✅ |
| Versioned schema migrations with a downgrade guard | ✅ |
| Heuristic classifier and optional ONNX Runtime backend | ✅ |
| Windows input hooks, active-window enrichment, overlay, and tray | ✅ |
| macOS capture: `CGEventTap`, active-window and browser-tab enrichment | ✅ Needs Accessibility permission |
| macOS `NSStatusItem` tray and native `NSPanel` overlay | ✅ |
| Native notifications | ✅ Windows · ❌ macOS — needs a bundle id from [Roadmap 3.3](docs/ROADMAP.md) |
| Linux capture with polling fallback | ✅ |
| Linux tray and overlay | ❌ Stubbed — [Roadmap 3.2](docs/ROADMAP.md) |
| Packaging and signing | ⚠️ Windows package builds; packaged signing is incomplete ([Roadmap 0.4b](docs/ROADMAP.md)) · ❌ macOS and Linux |
| C++ feature-vector golden fixtures and IPC contract tests | ✅ |
| CI on Windows, macOS, and Linux plus sanitizer jobs | ✅ |
| CI launches the app, not just links it | ✅ Windows · ✅ macOS · ❌ Linux |

The desktop app is gated behind `SNAPBACK_BUILD_APP=ON`; the headless core builds
and tests without it.

What has changed, and what is not in a release yet, is in [CHANGELOG.md](CHANGELOG.md).
**Nothing has shipped as a published release**; the existing `v0.2.0` tag points at a commit
that is no longer on any branch. See the note at the end of the changelog before cutting one.

## Layout

```
src/
├── app/       state, commands, bridge, tray, settings
├── capture/   OS hooks, active window, permissions, ring buffer
├── engine/    features, classifier, ONNX, focus modes
├── storage/   SQLite persistence and schema migrations
├── snapback/  context recovery and overlay
└── util/      logger and clock helpers (header-only leaves)
tests/         doctest unit and contract tests
fixtures/      model and feature-vector scenarios/golden data
frontend/      React dashboard and native bridge adapter
scripts/       local test and packaging helpers
```

## Build and test

Requires C++20 and CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target snapback_tests --parallel
ctest --test-dir build --output-on-failure
```

For the frontend:

```sh
cd frontend
npm ci
npm test
npm run typecheck
```

See the [documentation index](docs/README.md) for architecture, platform, testing,
benchmarking, packaging, and roadmap references.

For the full end-to-end Windows walkthrough, see
[docs/windows_demo.md](docs/windows_demo.md).
