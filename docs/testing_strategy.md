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
Measured after **14.13** made the window predicates index-seekable; the "before" column for the
two queries that changed is in that item. Re-run on 2026-09-22 alongside the concurrency
measurement below and unchanged within noise, so the figures are one database's, not two.

The Review presets are **today / 7d / 30d / all** (`frontend/src/reviewRange.ts`). `all` passes
no cutoff at all, so it is the unfiltered scan and 14.13 did not change it. The 90-day column
below is not a preset — it is there because it is the retention limit, and so it is the shape
of the most expensive thing the storage layer can be asked for.

| query | day | 7d | 30d | 90d (= retention limit) |
| --- | --- | --- | --- | --- |
| `storage.cpp:Storage::prediction_stats` | 55 ms (56) | 435 ms (438) | 1,921 ms (1,953) | **5,867 ms** (5,901) |
| `storage.cpp:Storage::hourly_focus_buckets` | 18 ms (19) | 152 ms (155) | 669 ms (675) | 1,994 ms (2,046) |
| `storage.cpp:Storage::recent_session_summaries` | 1,088 ms (1,102) | 1,101 ms (1,123) | 1,077 ms (1,100) | 1,069 ms (1,091) |
| `storage.cpp:Storage::daily_summary` | 99 ms (103) | 543 ms (549) | 2,307 ms (2,319) | **6,830 ms** (6,840) |

**Three things this says that reading the code did not.**

1. **Cost is now proportional to the window, and it was not before.** Until 14.13 every one of
   these read the whole table whichever window was asked for, so `day` on a mature database
   cost nearly what `30d` did. It now costs what a day should.
2. **`recent_session_summaries` is flat across every window** — about 1.1 s whether it is
   answering for one day or ninety, and 14.13 did not touch it because it does not filter on
   `predictions` at all. It is bounded by the 500-session cap and by per-session work, so 180
   sessions cost 6 ms each. A window filter that does not make the query cheaper is the next
   thing here worth reading.
3. **The wide windows still cost seconds, and they hold `storage_mutex_` while doing it.**
   `daily_summary` at the retention limit is 6.8 s and overtakes `prediction_stats` past 30
   days; its per-day recursive axis is what grows. This is the contention **14.1** asks to have
   measured before deciding whether to build a separate read lane, and "immaterial" is not what
   it says.

### What a report in flight costs a persist

`storage_mutex_` serializes every storage-backed UI report against the engine's persist
phase. **14.1** proposes a separate read lane and forbids building it from structure alone,
so `benchmarks/bench_budgets.cpp` now runs the heaviest Review queries concurrently with a
paced writer, in the shape `state.cpp:AppState::health` and the engine really use: one
`Storage`, one lock, two reader threads, 200 persists at 100 ms intervals.

| writer's wait for the lock | alone | with two readers |
| --- | --- | --- |
| p50 | 0.00 ms | 0.00 ms |
| p95 | 0.00 ms | 0.00 ms |
| **max** | **0.04 ms** | **29,191 ms** |
| the write itself (p95) | 0.67 ms | 0.20 ms |

**Read this as a tail, not an average.** Seven of 200 persists ever found the lock held.
The other 193 took it for free, which is why p50 and p95 do not move at all — an average, or
a p95-only report, would have called this immaterial and been exactly wrong. What happened to
the seven is that one waited **29 seconds**.

Three things that number is, and one it is not:

- It is **not one long query's duration**. `daily_summary` at the retention limit is 6.8 s,
  and 29 s is four of them. `RankedMutex` wraps a `std::mutex`, which offers no fairness
  guarantee, so under continuous read load a waiting writer can be passed over repeatedly
  while two readers hand the lock back and forth.
- The readers here are **heavier than a real Review load**, which issues five commands once
  and then stops. They loop with no think time. The number is a worst case, not a typical one.
- **Dropped events are not the risk.** 29 s of capture at 50 events/s is ~1,460 of the ring's
  65,536 slots (`capture_thread.hpp:kCapacity`), so the buffer absorbs it.
  What is at risk is 29 s of unpersisted work and a UI that cannot get an answer.

The lock instrument reports a longer worst wait than the writer saw — 37.4 s against 29.2 s —
because it counts every rank-`Storage` acquisition, including readers waiting on each other.
Both figures are of the same run; they measure different waiters.

### Lock hold and wait, in the running app

`ranked_mutex.hpp:lock_metrics` instruments every acquisition of every rank, and
`state.cpp:AppState::runtime_metrics` folds the figures into `get_health`, so they travel in
a support bundle from a real install rather than only from a benchmark.

**Percentiles here are histogram bucket upper bounds, never measured values.** `p95 <= 512us`
is what the data supports; the exact tail is the maximum beside it. Anything that renders one
has to say `<=`.

Idle, over 60 s with the engine and capture thread running and no session
(`benchmarks/bench_idle.cpp`):

| | measured |
| --- | --- |
| CPU | 124 ms — **0.21% of one core**, 2.06 ms per wall second |
| engine wakeups | 542 — **9.02/s**, 229 µs of CPU each |
| `State` lock | 1,086 acquisitions, 0 contended, hold p50 ≤ 0 µs, p95 ≤ 7 µs, max 97 µs |
| `Storage` lock | 2 acquisitions, 0 contended, max hold 12 µs |
| ring high-water | 0 of 65,536 slots |
| SQLite busy waits / exhausted | 0 / 0 |

The wakeup rate is not a discovery — `state.hpp:kEngineTickIntervalMs` is 100, so the loop
wakes ten times a second whether or not anything is happening. What those wakeups cost is the
figure nobody had. **Recorded, not fixed:** a poll interval is a product decision, and 0.21%
of a core is not obviously one worth spending a change on.

One caveat the benchmark states itself: its input hook is a silent fake, so no OS-level hook
or message-pump cost is in that figure. It is what the *engine* costs when idle, not what the
*product* costs. And `GetProcessTimes` counts in ~15.6 ms scheduler ticks, so a 20 s window
reports zero CPU; the run above is 60 s for that reason.

### SQLite busy waits

`storage.cpp:Storage::busy_stats` counts both halves, through a `sqlite3_busy_handler` that
reproduces SQLite's own delay schedule so only the counting is new: `waits` is the pressure
(the handler ran at all) and `exhausted` is the failure (`SQLITE_BUSY` reached the caller,
which for the engine means a discarded persistence batch). Both are zero in every run above —
nothing external contends for the file here, which is the expected result and the reason the
counter exists in the field rather than in a benchmark.

### Still not measured

- **Ring high-water and `captureEventsDropped` over a working day.** The counters exist and
  ship on `get_health`; what is missing is a day of real use to read them from. This one
  closes with a support bundle, not with code.
- **The real input hook's idle cost**, per the caveat above.

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
