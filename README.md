[![CI](https://github.com/KassaSana/Snapback/actions/workflows/ci.yml/badge.svg)](https://github.com/KassaSana/Snapback/actions/workflows/ci.yml)
[![Release](https://github.com/KassaSana/Snapback/actions/workflows/release.yml/badge.svg)](https://github.com/KassaSana/Snapback/actions/workflows/release.yml)

# Snapback

A desktop focus tracker. You say what you're working on, Snapback watches keyboard, mouse and
window activity while you work, and when you drift off it shows a small overlay that takes you
back to where you were. Afterwards you can review where the time actually went.

Everything stays on your machine: no account, no network calls, one SQLite file.

**[Try the live demo](https://kassasana.github.io/Snapback/)**. It's the real dashboard
running on generated sample data, so there's nothing to install.

## How it works

```
OS input hooks → ring buffer → feature extraction → classifier → SQLite → webview UI
                                                         ↓
                                              overlay + tray (native)
```

- **Capture** (`src/capture/`): low-level input hooks and active-window tracking per OS.
  Events go through a lock-free SPSC ring buffer to the engine thread.
- **Engine** (`src/engine/`): turns the event stream into rolling feature windows and
  classifies each moment as deep work, productive, drifting or distracted. There's a tuned
  heuristic by default, and an optional ONNX Runtime model.
- **Storage** (`src/storage/`): SQLite with versioned migrations and a downgrade guard.
- **App** (`src/app/`): the webview host, IPC commands, tray, settings, import/export.
- **Frontend** (`frontend/`): a React + TypeScript dashboard bundled into the binary.

There's more detail in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Platforms

- **Windows:** everything works. Installer and ZIP on the
  [releases page](https://github.com/KassaSana/Snapback/releases); they aren't code-signed
  yet, so SmartScreen will warn on first run.
- **macOS:** capture, tray and overlay all work (needs Accessibility permission). Native
  notifications and a packaged `.app` aren't there yet.
- **Linux:** capture works; tray and overlay are stubbed.

CI builds and tests on all three. It also launches the real app on Windows and macOS, and runs
ASan/UBSan and TSan jobs.

## Build

Needs a C++20 compiler, CMake 3.20+, Python 3 and Node.

```text
python scripts/verify.py
```

That builds and tests the headless core and the frontend. To build and run the desktop app,
see [docs/running.md](docs/running.md); for a one-command Windows demo, see
[docs/windows_demo.md](docs/windows_demo.md).

## Repo layout

```
src/
├── app/       webview host, IPC commands, tray, settings
├── capture/   OS input hooks, active window, ring buffer
├── engine/    features, classifier, ONNX backend
├── storage/   SQLite persistence and migrations
├── snapback/  the return-to-work overlay
└── util/      logging, clocks, small system helpers
tests/         doctest unit and contract tests
benchmarks/    replay and hot-path benchmarks
fixtures/      golden feature vectors, IPC contract, test model
frontend/      React dashboard and the hosted demo
scripts/       build, packaging and repo checks
```

[Roadmap](docs/ROADMAP.md) · [Changelog](CHANGELOG.md) · [Contributing](CONTRIBUTING.md)

## License

MIT. Third-party licenses are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
