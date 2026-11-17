# Testing strategy

No single environment can cover deterministic engine behaviour, three native platforms and
real desktop permissions at once, so testing is split into layers.

## Local headless suite

Use the cross-platform entry point for the normal local headless check:

```text
python scripts/verify.py
```

It calls the existing platform wrapper, which runs:

- C++ doctest/CTest cases with `SNAPBACK_BUILD_APP=OFF`;
- frontend TypeScript typechecking;
- frontend unit and component tests; and
- the frontend production build.

It also runs frontend lint and the repo checks from the Ubuntu CI job.
Use `python scripts/verify.py native <CTest name regex>` for a focused native build and test,
or `frontend-unit <file.test.ts>` / `frontend-component <file.test.tsx>` for a frontend file.
The older wrappers still accept `--skip-frontend` (shell) and `-SkipFrontend` (PowerShell).
Most regressions should be caught here with synthetic capture events and in-memory storage
before platform smoke tests are involved.

### Contract tests

- `fixtures/feature_parity/scenarios.json` and `golden.json` pin all 31 feature values by
  name and position.
- `fixtures/ipc_commands.json`, native registration, and frontend calls pin the IPC command
  set; dispatcher tests cover argument validation and error envelopes. The same fixture's
  `events` block pins host-to-frontend events against `src/app/events.hpp`, so a listener with
  no emitter — and an emit site that names an event with a raw string — fail the build.
- Storage fixtures cover new, historical, malformed, downgraded, and large databases.
- Ranked-mutex and concurrency tests protect the capture/engine/storage lock boundaries.
- Hostile window-title tests push malformed and oversized titles through the whole pipeline.

The frontend CI command is `npm run test:ci`. It runs TypeScript unit scripts and the Vitest
component suite with V8 coverage floors of 76% statements, 66% branches, 74% functions, and
77% lines.

## Main CI workflow

`.github/workflows/ci.yml` runs on every push and PR. The headless job runs on Windows,
macOS and Linux, so there are seven jobs in total.

| Job | What it proves |
| --- | --- |
| `cpp-headless` | CMake + CTest on Windows, macOS, and Linux; the Ubuntu run also runs the repo checks |
| `sanitizers` | ASan + UBSan over memory/lifetime-sensitive paths |
| `frontend-mock` | Frontend install, typecheck, lint, tests, coverage, and build |
| `windows-desktop-integration` | Windows app launches; page-side acceptance crosses the real bridge, then a CDP driver clicks navigation and session controls in the real WebView2 UI |
| `macos-gui-smoke` | macOS app launches, loads its bundle, crosses the real webview bridge, and exits |

## Weekly and on-demand deep checks

`.github/workflows/deep-checks.yml` runs the slower checks weekly and on demand:

| Job | What it proves |
| --- | --- |
| `security-audit` | The committed frontend lockfile has no high/critical npm advisory |
| `windows-gcc` | The portable core builds and tests under MinGW-w64 GCC |
| `thread-sanitizer` | TSan over capture and engine concurrency |
| `onnx-linux` | Optional ONNX build and fixture inference |

The desktop jobs don't depend on the headless job, so a broken core doesn't hide whether the
desktop app still builds and launches.

## Other workflows

- `.github/workflows/production-smoke.yml` builds and validates an unsigned Windows package
  on demand and weekly.
- `.github/workflows/benchmarks.yml` runs manual, parameterized benchmarks and uploads the
  raw output. See [benchmarking.md](benchmarking.md).
- `.github/workflows/release.yml` builds the tag-driven Windows package and publishes a
  GitHub release. Signing runs only when a certificate is configured.
- `.github/workflows/demo-pages.yml` builds the hosted demo and publishes it to GitHub Pages.
- `.github/workflows/pin-freshness.yml` checks weekly whether pinned dependencies have newer
  releases and opens an issue if so.

For the interactive Windows path, use [windows_demo.md](windows_demo.md). For per-platform
build and launch commands, use [running.md](running.md).

## Performance evidence

Workloads, commands and results are in [benchmarking.md](benchmarking.md). They're manual
measurements; CI doesn't enforce performance limits.

## What CI doesn't cover

- Real macOS Accessibility and Input Monitoring permission prompts.
- Sustained real input capture on every desktop environment.
- Linux tray and overlay, which are stubs.
- Clicking through the UI on WebKitGTK. Windows drives the real WebView2 UI over CDP, and
  Windows and macOS both run a page-side acceptance script through the real IPC bridge.
