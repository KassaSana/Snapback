# Windows demo runbook

Use this for an isolated native demonstration. See [Running](running.md) for prerequisites,
manual builds, permissions, environment variables, and troubleshooting; see
[Packaging](PACKAGING.md) for artifacts, signing, and package validation.

## Launch

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1
```

The script typechecks/builds the frontend, configures the desktop target with ONNX off,
builds and runs native tests, stages the frontend beside the executable, and launches
Release by default. Demo data, including `focoflow.db`, goes to `.demo/data` rather than
your normal `%APPDATA%\snapback` directory. These are real captured sessions, unlike
the [hosted sample-data demo](../frontend/README.md#the-hosted-demo).

## Useful switches

```powershell
# Build and test without launching.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -NoLaunch

# Show a sample native recovery overlay on launch.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -OverlayTest

# Build Debug and launch with a live Vite server.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -UseVite

# Reuse an existing Vite server.
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -UseVite -SkipFrontend

# Show a sample Windows notification for this launch.
$env:SNAPBACK_NOTIFICATION_TEST = "1"
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1
Remove-Item Env:SNAPBACK_NOTIFICATION_TEST
```

`-UseVite` accepts and probes a loopback HTTP URL; `-SkipFrontend` requires an already
reachable server. Release builds always use the bundled frontend. Script parameters and
platform applicability are indexed in [scripts](../scripts/README.md).

## Walkthrough

1. Launch and confirm the dashboard loads.
2. Start a session with a concrete goal, such as `Implement storage parity`.
3. Type and switch windows; confirm live readings update.
4. Add a classification rule and confirm the verdict reflects it.
5. Stop; inspect the recap and the selected session in Review. Recording starts only on Start.
6. Use `-OverlayTest` to inspect a native overlay without staging a distraction.

For release verification, run **P0-09** in the [roadmap](ROADMAP.md): stage a real
distraction, verify Take me back, open an external link, and verify stopped attendance
does not grow. A sample overlay alone does not prove recovery works.

## Automated desktop checks

The Windows GUI smoke crosses the real bridge and can drive the UI through WebView2 CDP;
see [Testing strategy](testing_strategy.md) for what it proves and
[scripts](../scripts/README.md) for the command inventory. Package validation and install
checks belong to [Packaging](PACKAGING.md), not this walkthrough.
