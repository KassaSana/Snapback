# Testing strategy

Snapback uses separate layers because no single test environment can exercise deterministic
engine behavior, three native platforms, and real desktop permissions at once.

## Local headless suite

Use the platform wrapper for the normal full check:

```sh
# macOS / Linux
./scripts/test_local.sh
```

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File .\scripts\test_local.ps1
```

The wrappers run:

- C++ doctest/CTest cases with `SNAPBACK_BUILD_APP=OFF`;
- frontend TypeScript typechecking;
- frontend unit and component tests; and
- the frontend production build.

Use `--skip-frontend` on the shell wrapper or `-SkipFrontend` on PowerShell for a native-only
iteration. Most regressions should be caught here with synthetic capture events and in-memory
storage before platform smoke tests are involved.

### Contract tests

- `fixtures/feature_parity/scenarios.json` and `golden.json` pin all 31 feature values by
  name and position.
- `fixtures/ipc_commands.json`, native registration, and frontend calls pin the IPC command
  set; dispatcher tests cover argument validation and error envelopes. The same fixture's
  `events` block pins host-to-frontend events against `src/app/events.hpp`, so a listener with
  no emitter — and an emit site that names an event with a raw string — fail the build.
- Storage fixtures cover new, historical, malformed, downgraded, and large databases.
- Ranked-mutex and concurrency tests protect the capture/engine/storage lock boundaries.

The frontend CI command is `npm run test:ci`. It runs TypeScript unit scripts and the Vitest
component suite with V8 coverage floors of 76% statements, 66% branches, 74% functions, and
77% lines.

## Main CI workflow

`.github/workflows/ci.yml` has five job definitions. The headless matrix expands across
Windows, macOS, and Linux, yielding seven hosted jobs. Costlier and optional checks run weekly
or on demand in `.github/workflows/deep-checks.yml`.

| Job | What it proves |
| --- | --- |
| `cpp-headless` | CMake + CTest on Windows, macOS, and Linux; its Ubuntu entry also runs documentation, repository, and supply-chain guards |
| `sanitizers` | ASan + UBSan over memory/lifetime-sensitive paths |
| `frontend-mock` | Frontend install, typecheck, lint, tests, coverage, and build |
| `windows-desktop-integration` | Windows app launches; page-side acceptance crosses the real bridge, then a CDP driver clicks navigation and session controls in the real WebView2 UI |
| `macos-gui-smoke` | macOS app launches, loads its bundle, crosses the real webview bridge, and exits |

## Weekly and on-demand deep checks

`.github/workflows/deep-checks.yml` keeps slower or optional coverage without charging every
push for it:

| Job | What it proves |
| --- | --- |
| `security-audit` | The committed frontend lockfile has no high/critical npm advisory |
| `windows-gcc` | The portable core builds and tests under MinGW-w64 GCC, which previously exposed a file-replacement defect |
| `thread-sanitizer` | TSan over capture and engine concurrency |
| `onnx-linux` | Optional ONNX build and fixture inference |

The two desktop jobs intentionally do not depend on the headless suite: a broken core must
not hide whether the platform shell still builds or launches.

## Other workflows

- `.github/workflows/production-smoke.yml` builds and validates an unsigned Windows package
  on demand and weekly.
- `.github/workflows/deep-checks.yml` runs the slower compiler, concurrency, ONNX, and npm
  advisory checks weekly and on demand.
- `.github/workflows/benchmarks.yml` runs manual, parameterized benchmarks and uploads the
  raw output. See [benchmarking.md](benchmarking.md).
- `.github/workflows/release.yml` builds the tag-driven Windows package and publishes a
  GitHub release; signing remains conditional on certificate provisioning.

For the interactive Windows path, use [windows_demo.md](windows_demo.md). For per-platform
build and launch commands, use [running.md](running.md).

## Deliberate coverage boundaries

Headless CI does not prove:

- real macOS Accessibility/Input Monitoring permission prompts;
- sustained real input capture on every desktop environment;
- Linux tray/overlay behavior, which is still stubbed; or
- browser-driven clicking remains open on WebKitGTK, but Windows now drives the real WebView2
  UI through CDP. Both Windows and macOS also run a page-side acceptance program through the
  real shim, `webview.bind()`, command registry, async worker, and error envelope (Roadmap
  10.1).

Those gaps belong in [ROADMAP.md](ROADMAP.md), not in a second task list here. When a new
test layer lands, update this document to describe what it actually proves.
