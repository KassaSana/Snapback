# Windows Demo Runbook

This is the local Windows demo path for the C++ port. It builds the reused React
frontend into `frontend/dist`, copies those assets next to `snapback.exe`, and
launches the native C++ webview shell against the bundled files. Pass `-UseVite` for a
Debug build connected to a local Vite server with live reload; release builds continue to
ignore frontend URL overrides.

## One-Command Demo

From the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1
```

Useful switches:

```powershell
# Build and test, but do not launch the app.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -NoLaunch

# Pop a sample native snapback overlay immediately on launch.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -OverlayTest

# Send a sample Windows notification through the tray icon on launch.
$env:SNAPBACK_NOTIFICATION_TEST = "1"
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1

# Reuse an already-running Vite server.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -UseVite -SkipFrontend

# Start Vite, build Debug, test, and launch against the dev server.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -UseVite
```

## What The Script Does

1. Runs frontend typecheck and `npm run build`.
2. Configures CMake with `SNAPBACK_BUILD_APP=ON` and `SNAPBACK_ONNX=OFF`.
3. Copies `frontend/dist` beside `snapback.exe` as `frontend/`.
4. Builds `snapback_tests`, runs `ctest`, then builds `snapback.exe` in Release by default
   or Debug with `-UseVite`.
5. Launches `snapback.exe` with:
   - `SNAPBACK_DATA_DIR=.demo/data`
   - optional `SNAPBACK_FRONTEND_URL=http://127.0.0.1:5173` when `-UseVite` is passed
   - optional `SNAPBACK_OVERLAY_TEST=1`
   - optional `SNAPBACK_NOTIFICATION_TEST=1`

The `.demo/data` folder keeps demo sessions, labels, exports, and `focoflow.db`
away from your normal `%APPDATA%\snapback` data.

`-UseVite` accepts only a loopback `http://` URL and probes it before building. With
`-SkipFrontend`, the probe fails loudly unless an existing server is reachable.

## Manual Demo Steps

```powershell
cd frontend
npm ci
npm run typecheck
npm run build
```

In another terminal:

```powershell
cmake -S . -B build-windows-demo -G "Visual Studio 17 2022" -A x64 -DSNAPBACK_BUILD_APP=ON -DSNAPBACK_ONNX=OFF
cmake --build build-windows-demo --config Release --target snapback_tests
ctest --test-dir build-windows-demo -C Release --output-on-failure
cmake --build build-windows-demo --config Release --target snapback

$env:SNAPBACK_DATA_DIR = "$PWD\.demo\data"
.\build-windows-demo\Release\snapback.exe
```

## GUI Smoke

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui_smoke_windows.ps1
```

This launches `snapback.exe`, waits for a real `Snapback` window title, and stops
the process. Use it when you want an automated desktop sanity check without a
full manual demo.

## Unsigned Package

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
```

This builds frontend assets, C++ tests, `snapback.exe`, then creates:

- CPack ZIP (`Snapback-*-win64.zip`)
- CPack NSIS installer — requires `makensis` on `PATH`
- optional Authenticode signing — see [docs/PACKAGING.md](PACKAGING.md)

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 `
  -SignCertificate "YOUR_CERT_THUMBPRINT"
```

- `Snapback-<version>-win64.zip`
- `Snapback-<version>-win64-installer.exe` — the NSIS installer, renamed from the
  `Snapback-<version>-win64.exe` CPack writes

`<version>` is whatever `project(... VERSION x.y.z)` in `CMakeLists.txt` declares — that is
the single source, and `scripts/check_release_tag.py` is what holds a release tag to it. The
packaging and validation scripts derive the filename from the ZIP CPack actually produced
rather than reconstructing it, so nothing here needs updating when the version bumps.

The script fails outright when `makensis` is missing. Pass `-SkipInstaller` to build the ZIP
alone on a machine without NSIS.

Validate the ZIP exactly as a user would run it:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\validate_windows_package.ps1
```

## Demo Flow

1. Launch the app and confirm the dashboard loads.
2. Start a session with a concrete goal, such as `Implement storage parity`.
3. Type or switch windows for a few seconds and watch live predictions update.
4. Add an app rule and confirm the dashboard reflects it.
5. Stop the session and export training data.
6. Open the context timeline; off-task windows should not pollute it.
7. Run with `-OverlayTest` to show the native Windows overlay without staging a
   real distraction.

## How It Works

`src/main.cpp` reads demo/runtime knobs from the environment:

- `SNAPBACK_FRONTEND_URL` selects the webview URL in Debug builds. Release builds always
  use the bundled `frontend/index.html` and fail closed if it is absent.
- `SNAPBACK_DATA_DIR` overrides the SQLite/app-data folder. Without it, Windows
  uses `%APPDATA%\snapback`.
- `SNAPBACK_OVERLAY_TEST=1` shows a sample native overlay on startup.
- `SNAPBACK_NOTIFICATION_TEST=1` shows a sample Windows tray notification on startup.

The app still uses the same production pipeline underneath: Windows input capture
feeds `AppState`, the engine tick produces predictions, `ContextTracker` gates
timeline snapshots, storage writes SQLite rows, and the webview IPC shim exposes
the native command names to the React frontend.
