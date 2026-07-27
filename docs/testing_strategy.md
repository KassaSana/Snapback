# Testing Strategy

**Audited against the code 2026-07-23 (Roadmap 12.2).** Four claims in this file were
false: the CI job list was half the real one, the macOS/Linux capture backends were
described as stubs when both are real, NSIS packaging was listed as future work when it is
already configured, and `docs-smoke` was described as checking one thing when it now checks
two. Corrections are inline.

Snapback has three useful test tiers right now. They answer different questions,
so they should stay separate.

> **On a non-Windows host**, several commands below do not run — they are PowerShell, and
> the development machine is macOS. See [running.md](running.md) for the per-OS build, test,
> and launch instructions, and [../scripts/README.md](../scripts/README.md) for which
> scripts run where.

## 1. Local Mock / Headless Tests

Goal: prove the deterministic core works without OS hooks, a webview, or a live
desktop session.

Run:

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1
```

```sh
# macOS / Linux -- same pipeline (Roadmap 12.5)
./scripts/test_local.sh
```

This runs:

- C++ doctest/CTest suite with `SNAPBACK_BUILD_APP=OFF`
- frontend TypeScript typecheck
- frontend unit/component tests
- frontend production build

CI runs `npm run test:ci`, which executes the pure TypeScript unit scripts and the Vitest
component suite with V8 coverage. The global component-suite floors are 76% statements,
66% branches, 74% functions, and 77% lines; `frontend/coverage/` remains generated and
gitignored.

Fast variant (C++ only):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1 -SkipFrontend
```

```sh
./scripts/test_local.sh --skip-frontend
```

The two are equivalent except that the shell version has no `-IncludeWindowsDemo`:
`windows_demo.ps1` needs MSVC and produces `snapback.exe`, so it cannot run off Windows.

The C++ suite is mostly mock/headless by design: synthetic capture events drive
storage, classifier, tracker, app-state, command dispatch, training status, tray,
and overlay formatting. This is where most regressions should be caught.

Feature-vector contract testing:

```powershell
cmake --build build --target snapback_tests --config Release
ctest --test-dir build -C Release --output-on-failure
```

The C++ suite replays `fixtures/feature_parity/scenarios.json` and compares every
feature value with `fixtures/feature_parity/golden.json` within `1e-6`. The golden
fixture is checked in so a feature-order change fails locally and in CI.

## 2. Integrated Cross-System Tests

Goal: prove the portable C++ core builds and behaves on all supported runner OSes,
and prove the Windows desktop shell can be built against WebView2.

GitHub Actions workflow: `.github/workflows/ci.yml`

The current jobs are:

| Job | What it proves |
|-----|----------------|
| `cpp-headless` | CMake + CTest on Windows, macOS, Linux |
| `sanitizers` | ASan + UBSan over the suite |
| `thread-sanitizer` | TSan — the capture/engine thread seam |
| `security-audit` | frontend `npm audit` against the committed lockfile |
| `cpp-headless` | C++ replay plus exact feature-vector comparison via the golden test |
| `onnx-windows` | the `SNAPBACK_ONNX` path builds and runs (not in the default build) |
| `onnx-linux` | same, on Linux |
| `benchmark-smoke` | the benchmark targets still build and run |
| `frontend-mock` | npm install, typecheck, tests, build |
| `windows-desktop-integration` | runs `scripts/windows_demo.ps1 -NoLaunch` |
| `desktop-app-build` | the desktop app **links** on each OS |
| `docs-smoke` | demo runbook linkage, **and** that every path a doc names exists (`scripts/check_doc_paths.py`) |

`windows-desktop-integration` and `desktop-app-build` deliberately have **no `needs:`** —
they run even when the core suite is red, because a broken core is exactly when the desktop
guard's answer matters. Do not give them one (Roadmap 6.3).

Three other workflows exist and are not part of `ci.yml`: `benchmarks.yml`,
`production-smoke.yml`, `release.yml`.

**Correction (2026-07-23):** this section used to say "those platform capture
implementations are still stubs." They are not. `input_hook_macos.mm` is a real
`CGEventTap`, `input_hook_linux.cpp` is real evdev with a polling fallback, and
`active_window.cpp` has real per-OS branches. What the macOS/Linux jobs *do* skip is
**exercising** capture — they run headless, with no live desktop session, so the backends
compile and link but never see an event. Verifying macOS capture on real hardware is
Roadmap 0.3. The stubs that do remain are the **overlay and tray** off Windows
(`overlay_stub.cpp`, `tray_stub.cpp`) — Roadmap 3.1/3.2.

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

In order (`scripts/windows_demo.ps1:77-130`): builds `frontend/dist` (`npm ci`,
`npm run typecheck`, `npm run build`) → configures CMake → builds `snapback_tests` →
**runs CTest** → builds `snapback.exe`, with the frontend bundle copied next to it. The
tests run *before* the app is built, so a red suite stops the demo.

The `-UseVite` variant selects Debug (the only configuration that honors
`SNAPBACK_FRONTEND_URL`), starts or verifies the loopback dev server, and fails before launch
if the server is unreachable:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows_demo.ps1 -UseVite -NoLaunch
```

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
   *(Roadmap 10.1 — the IPC seam is the one place nothing tests.)*
2. ~~Add signing once a certificate is available.~~ **Signing is wired** in `release.yml`;
   what is missing is the certificate itself (Roadmap 0.4b).
3. ~~Add NSIS/WiX installer generation.~~ **NSIS is already configured** —
   `CMakeLists.txt:226` sets `CPACK_GENERATOR "ZIP;NSIS"` on Windows. It only produces an
   installer when NSIS is present on the machine; the IExpress path in
   `scripts/package_windows.ps1` is the fallback, not the only option.
4. ~~Validate macOS/Linux active-window polling on real desktops and then add native
   keyboard/mouse hooks.~~ **The native hooks exist** (`CGEventTap` on macOS, evdev on
   Linux). What remains is validating them on real hardware — Roadmap 0.3.
5. Turn the production-smoke artifact into a real release candidate once signing
   and installer QA are in place.
6. Port the PowerShell scripts, or document the direct `cmake`/`ctest` path, so the suite
   is runnable on the macOS dev host — Roadmap 12.5.
