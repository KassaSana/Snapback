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

## Measured budgets

Roadmap **14.11**. **14.1**, **14.5**, **14.7** and **9.10** each begin with "measure first",
and for a long time none of them had a number. These are the numbers. There is deliberately
**no CI ceiling** on any of them: hosted runners are too noisy to carry a performance gate, and
a flaky gate gets disabled within a month and then lies by omission for as long as it stays
off. Reproduce them by hand and re-record them when the host changes.

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DSNAPBACK_BUILD_BENCHMARKS=ON
cmake --build build-bench --target snapback_budget_benchmarks
SNAPBACK_BUDGET_DAYS=90 ./build-bench/snapback_budget_benchmarks      # the ceiling
SNAPBACK_BUDGET_DUTY_PCT=60 ./build-bench/snapback_budget_benchmarks  # a realistic day
```

[`benchmarks/bench_budgets.cpp`](../benchmarks/bench_budgets.cpp) exists separately from
`bench_snapback.cpp` and `bench_hotpaths.cpp` because both of those run
`Storage::open_memory()` with a fixed timestamp, and `bench_snapback` writes a prediction on
every *other* capture event. They measure how fast the code is, which is the right question for
them. They cannot measure what a real install holds, because the on-disk WAL and the real write
cadence are exactly what they leave out.

**The cadence everything follows from.** `state.cpp:AppState::compute_event` persists at most
one prediction per second, and only while a session is attended, not idle, and receiving input;
every persisted prediction carries exactly one `feature_snapshots` row. The ceiling is therefore
7,200 rows per attended hour, and a real day is lower by however much of it had no input.

**Host, 2026-09-22** — the numbers mean nothing without it. Intel Core i5-12500H (12C/16T),
15.6 GB RAM, WD Blue SN5100 NVMe SSD, Windows 11 26200, GCC 16.1.0 (UCRT64), SQLite 3.45.3,
`CMAKE_BUILD_TYPE=Release`. 90 days of history, 6 attended hours per day, 2 sessions per day.

### Footprint

| | ceiling (every attended second writes) | realistic (60% of seconds saw input) |
| --- | --- | --- |
| rows per attended hour | 7,200 | 4,321 |
| bytes per prediction + its feature row | 427 B | 425 B |
| bytes per attended hour | 1.47 MB | 897 KB |
| bytes per day | 8.80 MB | 5.26 MB |
| **database at the 90-day retention limit** | **792 MB** | **473 MB** |

The per-attended-hour figure confirms the estimate 14.11 was opened with (~1.5 MB). The
steady-state total is the number nobody had: a heavy user's database settles near **0.8 GB**,
not a few tens of megabytes.

### Read latency, p50 (p95), against that database

Every one of these runs under `storage_mutex_` — the same lock the engine takes to persist.

| query | day | 7d | 30d | 90d |
| --- | --- | --- | --- | --- |
| `storage.cpp:Storage::prediction_stats` | 476 ms (491) | 784 ms (799) | 1,974 ms (1,991) | **5,168 ms** (5,214) |
| `storage.cpp:Storage::hourly_focus_buckets` | 226 ms (232) | 316 ms (322) | 684 ms (685) | 1,674 ms (1,692) |
| `storage.cpp:Storage::recent_session_summaries` | 1,118 ms (1,137) | 1,094 ms (1,101) | 1,091 ms (1,129) | 1,105 ms (1,119) |
| `storage.cpp:Storage::daily_summary` | 90 ms (95) | 549 ms (558) | 2,297 ms (2,303) | **6,867 ms** (6,925) |

At the realistic 60% duty cycle each falls roughly with the row count — 90-day
`prediction_stats` 3,055 ms, `daily_summary` 4,074 ms, `hourly_focus_buckets` 988 ms,
`recent_session_summaries` 649 ms — so the shape is the same and only the constant moves.

**Three things this says that reading the code did not.**

1. **The Review surfaces cost seconds, not milliseconds, on a mature database, and they hold
   `storage_mutex_` while doing it.** A 90-day Review is the worst case and it is not close to
   free. This is the contention **14.1** asks to have measured before deciding whether to build
   a separate read lane; it is the single-threaded half of that measurement, and "immaterial"
   is not what it says.
2. **`recent_session_summaries` is flat across every window** — about 1.1 s whether it is
   answering for one day or ninety. It is bounded by the 500-session cap and by per-session
   work, not by the window, so 180 sessions cost 6 ms each. A window filter that does not make
   the query cheaper is worth a look on its own.
3. **`daily_summary` overtakes `prediction_stats`** past 30 days despite being the cheapest
   query at one day. Its per-day recursive axis is what grows.

### Not measured yet

Named so nobody reads the table above as complete. Each needs instrumentation or a running app,
neither of which this benchmark is:

- **`storage_mutex_` hold time, p50/p95, and engine persist-phase wait.** There is no timing
  hook inside the lock. The table above is single-threaded query wall time, which bounds the
  hold but is not the same measurement, and it is not labelled as if it were.
- **How often the `storage.hpp:kSqliteBusyTimeoutMs` wait is hit.** Needs a
  `sqlite3_busy_handler` counter; nothing counts it today.
- **Idle CPU and wakeups per second with no session**, and **ring high-water mark and
  `captureEventsDropped` over a working day.** Both need the running app over real time, read
  through `get_diagnostics` (`state.cpp:AppState::diagnostics`), not a benchmark binary.

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
