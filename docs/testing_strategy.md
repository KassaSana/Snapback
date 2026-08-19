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
  set; dispatcher tests cover argument validation and error envelopes.
- Storage fixtures cover new, historical, malformed, downgraded, and large databases.
- Ranked-mutex and concurrency tests protect the capture/engine/storage lock boundaries.

The frontend CI command is `npm run test:ci`. It runs TypeScript unit scripts and the Vitest
component suite with V8 coverage floors of 76% statements, 66% branches, 74% functions, and
77% lines.

## Main CI workflow

`.github/workflows/ci.yml` has twelve job definitions; the headless matrix expands across
Windows, macOS, and Linux.

| Job | What it proves |
| --- | --- |
| `security-audit` | The committed frontend lockfile has no high/critical npm advisory |
| `cpp-headless` | CMake + CTest on Windows, macOS, and Linux |
| `sanitizers` | ASan + UBSan over memory/lifetime-sensitive paths |
| `thread-sanitizer` | TSan over capture and engine concurrency |
| `onnx-linux` | Optional ONNX build and fixture inference. Linux only: the one ONNX-gated test case is a single case, and no shipped build sets `SNAPBACK_ONNX=ON` |
| `benchmark-smoke` | The replay benchmark builds and runs; the hot-path benchmark and performance thresholds are not covered |
| `frontend-mock` | Frontend install, typecheck, tests, coverage, and build |
| `windows-desktop-integration` | Windows demo build and native tests without launching |
| `desktop-app-build` | Desktop app links on macOS and Linux |
| `macos-gui-smoke` | macOS app launches, loads its bundle, round-trips a session, and exits |
| `docs-smoke` | Documentation paths and immutable dependency pins remain valid |

The three desktop jobs intentionally do not depend on the headless suite: a broken core must
not hide whether the platform shell still builds or launches.

## Other workflows

- `.github/workflows/production-smoke.yml` builds and validates an unsigned Windows package
  on demand and weekly.
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
- a browser-driven click through the real `webview.bind()` boundary.

Those gaps belong in [ROADMAP.md](ROADMAP.md), not in a second task list here. When a new
test layer lands, update this document to describe what it actually proves.
