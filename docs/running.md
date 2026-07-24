# Running Snapback on your machine

**What this page is for.** Roadmap 12.4: there was no single page saying *here is how you
build, test, and launch this, on the OS you are actually sitting at* — including which
targets cannot be built on which host, and why. Several sessions were spent rediscovering
that. Everything below was run on the machine it claims to run on, or is marked as
CI-only.

**Short version, macOS/Linux:**

```sh
./scripts/test_local.sh            # build + test everything that works headless
```

**Verified on this machine 2026-07-23** (Apple Silicon, Apple Clang): the headless suite
configures/builds/passes, the benchmark replay runs, and `SNAPBACK_BUILD_APP=ON` produces a
linked `snapback` binary. Windows and Linux rows below are from CI, not from a local run —
they are marked where it matters.

---

## 1. What can be built where

This is the table to read first. **Most "it doesn't build" confusion is a host mismatch,
not a broken tree.**

| Target | Windows | macOS | Linux |
|--------|---------|-------|-------|
| `snapback_tests` (headless core) | ✅ | ✅ | ✅ |
| `snapback` (desktop app, `SNAPBACK_BUILD_APP=ON`) | ✅ | ✅ links, tray/overlay are no-ops | ✅ links, tray/overlay are no-ops |
| Benchmarks (`SNAPBACK_BUILD_BENCHMARKS=ON`) | ✅ | ✅ | ✅ |
| ONNX backend (`SNAPBACK_ONNX=ON`) | CI only | ❌ | CI only |
| Real input capture | ✅ | ✅ needs Accessibility permission | ✅ needs `/dev/input` access |
| Tray + overlay | ✅ | ❌ stub | ❌ stub |
| Packaging / signing | ✅ | ❌ | ❌ |

Why the ❌s, concretely:

- **ONNX** expects a vendored runtime at `third_party/onnxruntime` (`CMakeLists.txt:84`).
  **That directory is not in this repo** — CI's `onnx-windows` / `onnx-linux` jobs vendor
  it as a build step. Turning `SNAPBACK_ONNX=ON` locally is a `FATAL_ERROR`, not a
  slow build. It is **off by default**, so the normal build never touches it.
- **Tray and overlay** off Windows are deliberate no-op stubs (`tray_stub.cpp`,
  `overlay_stub.cpp`) that exist so the app *links*. Real ones are Roadmap 3.1 / 3.2. The
  app runs; those two surfaces just do nothing.
- **Packaging** drives `signtool`, IExpress, and CPack/NSIS — Windows tooling. See
  [scripts/README.md](../scripts/README.md).
- **`overlay_windows.cpp`, `tray_windows.cpp`, `input_hook_windows.cpp`, and
  `autostart.cpp`'s Run-key path cannot compile here at all.** CI is the only place they
  are exercised — so **when Windows CI is red, they are covered nowhere.**

## 2. Prerequisites

| Need | Windows | macOS | Linux |
|------|---------|-------|-------|
| C++20 compiler | MSVC (VS 2022) | Apple Clang (Xcode CLT) | GCC ≥ 10 or Clang |
| CMake ≥ 3.20 | ✔ | `brew install cmake` | distro package |
| Node + npm | for the frontend | same | same |
| Python 3 | for `check_doc_paths.py`, parity | same | same |
| Webview runtime | WebView2 | WKWebView (built in) | WebKitGTK dev package |

Everything else — `nlohmann/json`, `doctest`, SQLite, and `webview` — is fetched by CMake
via `FetchContent` on first configure. **First configure needs network**; after that the
build is offline.

## 3. Build and test the core (all three OSes)

The headless core is the part that works everywhere. `SNAPBACK_BUILD_APP=OFF` is the
default, so this needs no desktop session:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target snapback_tests --parallel
ctest --test-dir build --output-on-failure
```

On Windows, MSVC is a multi-config generator — choose the config at build time instead:

```powershell
cmake -S . -B build
cmake --build build --config Release --target snapback_tests
ctest --test-dir build -C Release --output-on-failure
```

That difference is the single most common cross-platform papercut here: on macOS/Linux
`-DCMAKE_BUILD_TYPE=` at *configure* time and binaries in `build/`; on Windows `--config`
at *build* time and binaries in `build/Release/`.

Frontend tests are separate:

```sh
cd frontend && npm ci && npm run typecheck && npm run test && npm run build
```

Or use the wrapper that does both: `./scripts/test_local.sh` (`.ps1` on Windows).

## 4. Run the desktop app

The app target is **off by default**. Turning it on also pulls `webview/webview`:

```sh
# Build the React bundle first -- the app loads it from disk.
cd frontend && npm ci && npm run build && cd ..

cmake -S . -B build-app -DCMAKE_BUILD_TYPE=Release -DSNAPBACK_BUILD_APP=ON
cmake --build build-app --target snapback --parallel
./build-app/snapback
```

**Build the frontend first.** CMake copies `frontend/dist` next to the binary as a
post-build step; if `frontend/dist/index.html` is missing you get a CMake *warning*, not an
error, and then a release build has nothing to display (it fails closed to `about:blank` —
Roadmap 8.4). A blank window almost always means "no bundle."

On Windows, use the runbook instead — it wires the demo data dir and the tray:
[windows_demo.md](windows_demo.md).

## 5. Permissions for real capture

Capture is the one part that cannot be verified headlessly.

- **macOS** — needs **Accessibility** (System Settings → Privacy & Security →
  Accessibility). `permissions.cpp:33` probes it with `AXIsProcessTrustedWithOptions`. The
  `CGEventTap` is real, was silently dying under load until 2026-07-20, and **has still
  never been verified on live hardware** — that is Roadmap 0.3.
- **Linux** — reads `/dev/input` directly (evdev). Your user usually needs to be in the
  `input` group; without access it falls back to active-window polling, which yields
  window changes but no keystroke/mouse events.
- **Windows** — no permission prompt; the low-level hook works once the app runs.

## 6. Environment variables

All optional. Read in `main.cpp`.

| Variable | Effect |
|----------|--------|
| `SNAPBACK_DATA_DIR` | Override where `focoflow.db` and exports live |
| `SNAPBACK_LOG` | `TRACE`/`DEBUG`/`INFO`/`WARN`/`ERROR`/`OFF` (default `INFO`) |
| `SNAPBACK_FRONTEND_URL` | Point the webview at a dev server — **debug builds only**; release ignores it (Roadmap 8.4, and see 8.7) |
| `SNAPBACK_OVERLAY_TEST` | Pop a sample overlay on launch |
| `SNAPBACK_NOTIFICATION_TEST` | Fire a sample notification on launch |
| `SNAPBACK_BENCH_MINUTES` | Benchmark trace length |

Default data directory: `%APPDATA%\snapback` on Windows, `~/.snapback` elsewhere
(`main.cpp:51-61`). **The database file is named `focoflow.db`** and that is deliberate —
install compatibility with the Rust build.

## 7. Benchmarks

```sh
./scripts/run_benchmarks.sh                 # 180-minute replay
./scripts/run_benchmarks.sh --minutes 30
./scripts/run_benchmarks.sh --hotpaths      # producer/consumer/lock/SQLite micro-benchmarks
```

⚠️ The baseline table in [benchmarking.md](benchmarking.md) was measured on **Windows**
(i5-12500H, MSVC). Numbers from another host are not comparable to it.

## 8. When something fails

| Symptom | Cause |
|---------|-------|
| `SNAPBACK_ONNX requires the platform ONNX Runtime files` | `third_party/onnxruntime` is not vendored. Leave `SNAPBACK_ONNX=OFF`. |
| Undefined `Overlay::instance` / `Tray::instance` off Windows | The stub sources are missing from the target — they exist precisely to satisfy this link. |
| App window is blank | `frontend/dist` was not built before the app. |
| `ctest` finds no tests | You built `snapback` but not `snapback_tests`. |
| A doc references a file that isn't there | Run `python3 scripts/check_doc_paths.py` — it is the CI guard for exactly that. |
| X11 macros (`KeyPress`, `None`, `Status`) break a Linux build | Something included `webview.h` directly. `app/webview_compat.hpp` is the only legal include site (Roadmap 6.3). |
