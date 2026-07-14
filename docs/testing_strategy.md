# Testing Strategy

Snapback has three useful test tiers right now. They answer different questions,
so they should stay separate.

## 1. Local Mock / Headless Tests

Goal: prove the deterministic core works without OS hooks, a webview, or a live
desktop session.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1
```

This runs:

- C++ doctest/CTest suite with `SNAPBACK_BUILD_APP=OFF`
- frontend TypeScript typecheck
- frontend unit/component tests
- frontend production build

Fast variant:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1 -SkipFrontend
```

The C++ suite is mostly mock/headless by design: synthetic capture events drive
storage, classifier, tracker, app-state, command dispatch, training status, tray,
and overlay formatting. This is where most regressions should be caught.

Feature parity against the Rust source of truth:

```powershell
py .\scripts\run_feature_parity_dual.py
```

This script builds the small `snapback_feature_parity_export` tool, runs the
original Rust/Python parity CLI from `../Snapback`, exports Rust and C++ feature
vectors for the same `fixtures/feature_parity/scenarios.json`, and compares every
training column within `1e-6`. On CI/macOS/Linux, use `python` instead of the
Windows `py` launcher. CI checks out the Rust source-of-truth layout from the
`main-fresh` branch into `rust-source`; the current `master` branch contains the
C++ port, so it cannot be used as the Rust parity input.

## 2. Integrated Cross-System Tests

Goal: prove the portable C++ core builds and behaves on all supported runner OSes,
and prove the Windows desktop shell can be built against WebView2.

GitHub Actions workflow: `.github/workflows/ci.yml`

Jobs:

- `cpp-headless`: Windows, macOS, and Linux CMake + CTest
- `security-audit`: frontend `npm audit` against the committed lockfile
- `feature-parity`: Rust/Python parity plus Rust-vs-C++ vector diff
- `frontend-mock`: npm install, typecheck, tests, and build
- `windows-desktop-integration`: runs `scripts/windows_demo.ps1 -NoLaunch`
- `docs-smoke`: catches broken demo runbook linkage

The macOS/Linux jobs intentionally do not run real capture or webview tests yet.
Those platform capture implementations are still stubs, so their CI value is
compile/runtime parity of the portable core.

## 3. Production Smoke

Goal: prove a Windows demo artifact can be built from a clean runner.

GitHub Actions workflow: `.github/workflows/production-smoke.yml`

Triggers:

- manual `workflow_dispatch`
- weekly scheduled run

It builds:

- frontend `dist`
- C++ tests
- Windows `snapback.exe`
- unsigned CPack ZIP package
- unsigned IExpress self-extracting installer when available
- a GUI smoke launch that verifies a real Snapback window appears
- packaged ZIP validation from an extracted artifact

It uploads a `snapback-windows-unsigned` artifact. This is not signed; it is a
release-readiness smoke artifact.

## Windows Demo Verification

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1
```

No-launch smoke:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -NoLaunch
```

This builds `frontend/dist`, copies the bundled frontend next to `snapback.exe`,
builds the Windows app with MSVC, runs CTest, and builds `snapback.exe`.

GUI smoke:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui_smoke_windows.ps1
```

Unsigned package:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
```

Validate the package as extracted:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\validate_windows_package.ps1
```

## What Is Not Covered Yet

- Real macOS Accessibility/Input Monitoring permission flow.
- Real macOS/Linux input capture and active-window capture.
- End-to-end GUI automation that clicks through the webview.

## Sensible Next Steps

1. Add Playwright or WinAppDriver smoke coverage that clicks through the webview.
2. Add signing once a certificate is available.
3. Add NSIS/WiX installer generation if you want a conventional installer in addition
   to the current IExpress self-extracting installer.
4. Validate macOS/Linux active-window polling on real desktops and then add native
   keyboard/mouse hooks where appropriate.
5. Turn the production-smoke artifact into a real release candidate once signing
   and installer QA are in place.
