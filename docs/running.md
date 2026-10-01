# Running Snapback on your machine

This is the per-platform source for building, testing, launching, permissions, and common
failures. Claims that have not been exercised locally are marked CI-only.

**Short version, Windows/macOS/Linux:**

```text
python scripts/verify.py            # headless build/tests, frontend lint, repository guards
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

- **ONNX** expects a vendored runtime at `third_party/onnxruntime` (`CMakeLists.txt`).
  **That directory is not in this repo** — the weekly/on-demand `onnx-linux` job vendors
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
- **Packaging** drives `signtool` and CPack/NSIS — Windows tooling. See
  [scripts/README.md](../scripts/README.md).
- **Windows-only sources need a Windows build.** The headless capture target includes
  `input_hook_windows.cpp`; the desktop app target adds `overlay_windows.cpp` and
  `tray_windows.cpp`. The Run-key path in `autostart.cpp` is also Windows-only. macOS and
  Linux builds do not exercise those paths, so validate changes to them on Windows.

## 2. Prerequisites

| Need | Windows | macOS | Linux |
|------|---------|-------|-------|
| C++20 compiler | MSVC (VS 2022) | Apple Clang (Xcode CLT) | GCC ≥ 10 or Clang |
| CMake ≥ 3.20 | ✔ | `brew install cmake` | distro package |
| Node + npm | for the frontend | same | same |
| Python 3 | for `check_doc_paths.py`, parity | same | same |
| Webview runtime | WebView2 | WKWebView (built in) | WebKitGTK dev package |

CMake fetches pinned dependencies on a clean configure unless their sources are cached;
an optional local SQLite copy overrides its fetch. The webview is fetched only for the
desktop target. See [Dependencies](dependencies.md) for pins and update procedures.

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

If reusing a build directory configured with MSYS2 GCC, launch from its UCRT64 environment
or put `C:\msys64\ucrt64\bin` on `PATH` before running CMake or `verify.py`. Otherwise the
GCC driver can start while its compiler child fails with no useful diagnostic.

The test executable also needs that toolchain's runtime DLLs. If CTest processes exit
with `0xc0000139` before a test runs, check which GCC runtime the child shell resolves;
another GCC installation can shadow UCRT64 even when a direct launch succeeds. For an
isolated existing build, placing the matching `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, and
`libwinpthread-1.dll` beside the build executables avoids that loader ambiguity. These are
local build artifacts, not a change to release packaging.

That difference is the single most common cross-platform papercut here: on macOS/Linux
`-DCMAKE_BUILD_TYPE=` at *configure* time and binaries in `build/`; on Windows `--config`
at *build* time and binaries in `build/Release/`.

Frontend tests are separate:

```sh
cd frontend && npm ci && npm run typecheck && npm run test && npm run build
```

Or use `python scripts/verify.py` for the normal local headless check, including lint and
repository guards. The older wrappers still run both build/test suites:
`./scripts/test_local.sh` (`.ps1` on Windows).

The wrapper also **compiles** the benchmarks every run, because `SNAPBACK_BUILD_BENCHMARKS`
defaults to `OFF` and nothing else local builds `benchmarks/` — so a type change in `src/`
used to break them where only CI would notice. Compiling is seconds; *running* them is not,
so the run is opt-in:

```sh
./scripts/test_local.sh --include-benchmark-smoke        # .ps1: -IncludeBenchmarkSmoke
```

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
sequence above and then checks it worked: it launches the binary, runs five page-side
checks across the real webview bridge (including session storage, async export, and an
error envelope), requires the run loop to exit on its own, and fails if the webview landed
on `about:blank` instead of the bundle. It is the same script CI runs, so a local failure
is a real failure.

```sh
./scripts/gui_smoke_macos.sh                  # frontend + build + launch
./scripts/gui_smoke_macos.sh --skip-frontend --no-build   # just relaunch and re-check
```

## 5. Permissions for real capture

Real capture and permission prompts need desktop hardware; headless fixtures verify
translation and engine behavior, not live OS delivery.

- **macOS** — needs **Accessibility** (System Settings → Privacy & Security →
  Accessibility). `permissions.cpp:check_capture_permissions` probes it with `AXIsProcessTrustedWithOptions`. The
  Live macOS capture was recorded as verified on hardware on 2026-07-25
  (Roadmap 0.3); this documentation pass did not repeat that hardware check.
- **Linux** — reads `/dev/input` directly (evdev). Your user usually needs to be in the
  `input` group; without access it falls back to active-window polling, which yields
  window changes but no keystroke/mouse events.
- **Windows** — no permission prompt; the low-level hook works once the app runs.

## 6. Environment variables

All optional. Runtime entry-point options are read in `main.cpp`; developer-tool gating
is implemented by `src/app/frontend_assets.cpp`.

| Variable | Effect |
|----------|--------|
| `SNAPBACK_DATA_DIR` | Override where `focoflow.db` and exports live |
| `SNAPBACK_LOG` | `TRACE`/`DEBUG`/`INFO`/`WARN`/`ERROR`/`OFF` (default `INFO`) |
| `SNAPBACK_DEV_TRAINING` | Enable developer training tools in Release; Debug enables them by default (ADR-0006) |
| `SNAPBACK_FRONTEND_URL` | Point the webview at a dev server — **debug builds only**; release ignores it (Roadmap 8.4, and see 8.7) |
| `SNAPBACK_OVERLAY_TEST` | Pop a sample overlay on launch |
| `SNAPBACK_NOTIFICATION_TEST` | Fire a sample notification on launch (Windows only — macOS returns `false` until 3.3) |
| `SNAPBACK_GUI_SESSION_SMOKE` | Legacy native Windows launch check: start and stop a session through storage on the UI thread, write `gui_session_smoke.ok` into the data directory, then terminate. The real-webview acceptance smokes use `SNAPBACK_ACCEPTANCE_SCRIPT` instead. |
| `SNAPBACK_ACCEPTANCE_SCRIPT` | Test builds compiled with `SNAPBACK_ENABLE_ACCEPTANCE_HARNESS=ON` only: inject a page-side JavaScript acceptance program. The GUI smokes use this to cross the real webview bridge and publish `acceptance-verdict.json`; ordinary builds ignore it. |
| `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` | WebView2 runtime option used only by the Windows `-Driven` smoke to expose a loopback CDP port. The smoke chooses an ephemeral port and ordinary launches leave this unset. |
| `SNAPBACK_BENCH_MINUTES` | Benchmark trace length |

Default data directory: `%APPDATA%\snapback` on Windows, `~/.snapback` elsewhere
(`main.cpp:app_data_dir`). **The database file is named `focoflow.db`** and that is deliberate —
install compatibility across releases. Since Roadmap 7.3 the file also carries a schema
version in `PRAGMA user_version`; a database written by a *newer* Snapback than the one you
are running is refused rather than opened, and the log says so.

## 7. Benchmarks

```sh
./scripts/run_benchmarks.sh                 # 180-minute replay
./scripts/run_benchmarks.sh --minutes 30
./scripts/run_benchmarks.sh --hotpaths      # producer/consumer/lock/SQLite micro-benchmarks
```

[Benchmarking](benchmarking.md) owns workloads, commands, host/toolchain details, and
measured results, including the separate disk-budget and idle harnesses. Compare like-for-like runs.

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
