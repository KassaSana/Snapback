# Running Snapback on your machine

This is the per-platform source for building, testing, launching, permissions, and common
failures. Claims that have not been exercised locally are marked CI-only.

**Short version, macOS/Linux:**

```sh
./scripts/test_local.sh            # build + test everything that works headless
```

---

## 1. What can be built where

This is the table to read first. **Most "it doesn't build" confusion is a host mismatch,
not a broken tree.**

| Target | Windows | macOS | Linux |
|--------|---------|-------|-------|
| `snapback_tests` (headless core) | ✅ | ✅ | ✅ |
| `snapback` (desktop app, `SNAPBACK_BUILD_APP=ON`) | ✅ | ✅ | ✅ links, tray/overlay are no-ops |
| Benchmarks (`SNAPBACK_BUILD_BENCHMARKS=ON`) | ✅ | ✅ | ✅ |
| ONNX backend (`SNAPBACK_ONNX=ON`) | CI only | buildable, but no runtime vendored and no CI job | CI only |
| Real input capture | ✅ | ✅ needs Accessibility permission | ✅ needs `/dev/input` access |
| Tray + overlay | ✅ | ✅ | ❌ stub |
| Native notifications | ✅ | ❌ needs a bundle id (Roadmap 3.3) | ❌ |
| Packaging / signing | ✅ | ❌ | ❌ |

Why the ❌s, concretely:

- **ONNX** expects a vendored runtime at `third_party/onnxruntime` (`CMakeLists.txt:84`).
  **That directory is not in this repo** — CI's `onnx-windows` / `onnx-linux` jobs vendor
  it as a build step. Turning `SNAPBACK_ONNX=ON` without it is a `FATAL_ERROR` at configure
  time, not a slow build. It is **off by default**, so the normal build never touches it.

  CMake does know how to link it on all three platforms (`.lib`/`.dll`, `.dylib`, `.so`),
  so a macOS build works if you drop a matching `libonnxruntime.dylib` under
  `third_party/onnxruntime/lib`. But **no CI job builds ONNX on macOS**, so that path is
  unproven — treat a local success as your own result, not a guarantee.
- **Tray and overlay on Linux** are deliberate no-op stubs (`tray_stub.cpp`,
  `overlay_stub.cpp`) that exist so the app *links*. Real ones are Roadmap 3.2. The app
  runs; those two surfaces just do nothing. macOS has real ones as of Roadmap 3.1
  (`tray_macos.mm`, `overlay_macos.mm`) — verified by running the app, and its launch is
  covered in CI by [`scripts/gui_smoke_macos.sh`](../scripts/gui_smoke_macos.sh).
- **Native notifications on macOS** are the one tray behavior still missing.
  `Tray::show_notification()` returns `false` without calling the OS, on purpose:
  `UNUserNotificationCenter` needs a bundle identifier, which arrives with packaging
  (Roadmap 3.3). The `false` is a contract, not an oversight — callers may start trusting
  it to decide whether to fall back, so do not flip it before delivery is real.
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

On macOS, [`scripts/gui_smoke_macos.sh`](../scripts/gui_smoke_macos.sh) does the whole
sequence above and then checks it worked: it launches the binary, drives a session
start/stop through storage from the UI thread, requires the run loop to exit on its own,
and fails if the webview landed on `about:blank` instead of the bundle. It is the same
script CI runs, so a local failure is a real failure.

```sh
./scripts/gui_smoke_macos.sh                  # frontend + build + launch
./scripts/gui_smoke_macos.sh --skip-frontend --no-build   # just relaunch and re-check
```

## 5. Permissions for real capture

Capture is the one part that cannot be verified headlessly.

- **macOS** — needs **Accessibility** (System Settings → Privacy & Security →
  Accessibility). `permissions.cpp:33` probes it with `AXIsProcessTrustedWithOptions`. The
  `CGEventTap` is real, was silently dying under load until 2026-07-20, and **was verified
  on live hardware 2026-07-25** (Roadmap 0.3) — that run is also what found macOS capture
  stamping stale window titles onto events.
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
| `SNAPBACK_NOTIFICATION_TEST` | Fire a sample notification on launch (Windows only — macOS returns `false` until 3.3) |
| `SNAPBACK_GUI_SESSION_SMOKE` | Start and stop a session through storage on the UI thread, write `gui_session_smoke.ok` into the data directory, then terminate. What the launch smokes on both OSes assert against |
| `SNAPBACK_BENCH_MINUTES` | Benchmark trace length |

Default data directory: `%APPDATA%\snapback` on Windows, `~/.snapback` elsewhere
(`main.cpp:51-61`). **The database file is named `focoflow.db`** and that is deliberate —
install compatibility across releases. Since Roadmap 7.3 the file also carries a schema
version in `PRAGMA user_version`; a database written by a *newer* Snapback than the one you
are running is refused rather than opened, and the log says so.

## 7. Benchmarks

```sh
./scripts/run_benchmarks.sh                 # 180-minute replay
./scripts/run_benchmarks.sh --minutes 30
./scripts/run_benchmarks.sh --hotpaths      # producer/consumer/lock/SQLite micro-benchmarks
```

The baseline sections in [benchmarking.md](benchmarking.md) name their host and toolchain.
Compare only like-for-like runs; Windows and macOS measurements are not interchangeable.

## 8. When something fails

| Symptom | Cause |
|---------|-------|
| `SNAPBACK_ONNX requires the platform ONNX Runtime files` | `third_party/onnxruntime` is not vendored. Leave `SNAPBACK_ONNX=OFF`. |
| Undefined `Overlay::instance` / `Tray::instance` on Linux | The stub sources are missing from the target — they exist precisely to satisfy this link. On Windows and macOS the real backends define them. |
| Duplicate `Overlay::instance` / `Tray::instance` on macOS | A stub was listed in the target alongside the native `.mm`. Both stubs also self-guard on `__APPLE__`, so this should be impossible — if it happens, the guard was removed. |
| macOS tray icon appears but its menu never responds | The `NSStatusItem` was created off the main thread. `Tray::install()` returns early rather than crashing in that case, so a missing or dead menu is the only symptom. |
| App window is blank | `frontend/dist` was not built before the app. |
| `ctest` finds no tests | You built `snapback` but not `snapback_tests`. |
| `database schema version N is newer than this build understands` | You downgraded Snapback, or pointed an old build at a newer profile's data directory. The file is left untouched — run the newer build again, or point `SNAPBACK_DATA_DIR` elsewhere. Opening it anyway could write rows the newer build considers malformed, so it fails closed (Roadmap 7.3). |
| A doc references a file that isn't there | Run `python3 scripts/check_doc_paths.py` — it is the CI guard for exactly that. |
| X11 macros (`KeyPress`, `None`, `Status`) break a Linux build | Something included `webview.h` directly. `app/webview_compat.hpp` is the only legal include site (Roadmap 6.3). |
