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

The table above was taken before the fixture wrote any title history. With one
`context_snapshots` row every 24 attended seconds (81,000 rows over the 90 days) the ceiling
database is **805 MB** closed instead of 788–791 MB, about 2% more — titles are a rounding
error beside the per-second prediction and feature rows.

### Read latency, p50 (p95), against that database

Every one of these runs under `storage_mutex_` — the same lock the engine takes to persist.
Measured after **14.13** made the window predicates index-seekable; the "before" column for the
two queries that changed is in that item. Re-run on 2026-09-22 alongside the concurrency
measurement below and unchanged within noise, so the figures are one database's, not two.

**Correction, 2026-09-22.** Until then the fixture dated every *prediction* correctly but
stamped every *session* with the wall clock at generation, so all 180 sessions fell inside
every window. Rows that filter on `predictions.timestamp` were unaffected. Rows that filter on
`sessions.started_at` measured the whole history for every window, and read as flat across
windows — a property of the fixture, not of the query. `recent_session_summaries` below is
re-measured on the corrected fixture, and the rows after it are new.

The Review presets are **today / 7d / 30d / all** (`frontend/src/reviewRange.ts`). `all` passes
no cutoff at all, so it is the unfiltered scan and 14.13 did not change it. The 90-day column
below is not a preset — it is there because it is the retention limit, and so it is the shape
of the most expensive thing the storage layer can be asked for.

| query | day | 7d | 30d | 90d (= retention limit) |
| --- | --- | --- | --- | --- |
| `storage.cpp:Storage::prediction_stats` | 55 ms (56) | 435 ms (438) | 1,921 ms (1,953) | **5,867 ms** (5,901) |
| `storage.cpp:Storage::hourly_focus_buckets` | 18 ms (19) | 152 ms (155) | 669 ms (675) | 1,994 ms (2,046) |
| `storage.cpp:Storage::recent_session_summaries` | 286 ms (358) | 372 ms (405) | 578 ms (619) | 1,076 ms (1,156) |
| `storage.cpp:Storage::daily_summary` | 99 ms (103) | 543 ms (549) | 2,307 ms (2,319) | **6,830 ms** (6,840) |
| `storage.cpp:Storage::productive_session_streak` | 7 ms (8) | 78 ms (79) | 345 ms (390) | 1,030 ms (1,093) |
| `storage.cpp:Storage::context_app_counts` | 5 ms (6) | 10 ms (11) | 22 ms (25) | 63 ms (65) |

`storage.cpp:Storage::session_window_totals` and `storage.cpp:Storage::attended_secs_since`
are under 0.1 ms in every window and are left out of the table for that reason.

**Three things this says that reading the code did not.**

1. **Cost is now proportional to the window, and it was not before.** Until 14.13 every one of
   these read the whole table whichever window was asked for, so `day` on a mature database
   cost nearly what `30d` did. It now costs what a day should.
2. **The `sessions`-windowed reads scale with the window too** — once the fixture dates its
   sessions (see the correction above). `productive_session_streak` grows 150× from `day` to
   `90d` and `context_app_counts` 12×. Their `(?N IS NULL OR started_at >= ?N)` spelling costs
   a scan of `sessions`, which is 180 rows; the work is the per-session join into
   `predictions`, which already seeks `idx_predictions_session_ts`. So **14.14**'s bar is not
   met: rewriting the idiom would buy nothing. `recent_session_summaries` has a ~0.29 s floor
   at `day` (two sessions) that the row counts do not explain; it is recorded, not diagnosed.
3. **The wide windows still cost seconds, and they hold `storage_mutex_` while doing it.**
   `daily_summary` at the retention limit is 6.8 s and overtakes `prediction_stats` past 30
   days; its per-day recursive axis is what grows. This is the contention **14.1** asks to have
   measured before deciding whether to build a separate read lane, and "immaterial" is not what
   it says.

### What a report in flight costs a persist

`storage_mutex_` serializes every storage-backed UI report against the engine's persist
phase. **14.1** proposes a separate read lane and forbids building it from structure alone,
so `benchmarks/bench_budgets.cpp` runs Review's reads concurrently with a paced writer (one
persist every 100 ms), in the shape `state.cpp:AppState::health` and the engine really use:
one `Storage`, one lock. It runs the writer three times, against three loads:

- **alone** — the baseline;
- **one Review reader** — `bench_budgets.cpp:review_load`: the five commands
  `useReviewWorkflow.ts` issues for one load, in order, on one thread, over the 7-day preset
  Review opens on, then 5 s of think time, for 60 s. One thread because none of the five is
  registered `add_async`, so the bridge runs them inline on the webview's thread and a
  `Promise.all` over them is sequential in fact. Each command takes and releases the lock
  once, at the granularity `state.cpp` uses;
- **two hot readers** — the heaviest queries looping with no think time. A bound, not a
  workload.

Measured on the corrected fixture (sessions dated to their blocks; see the correction under
read latency). The first publication of this table, commit `89d931d`, ran on the old one and
overstated the Review column by the ~2 s of whole-history session reads it could not window.

| writer's wait for the lock | alone | one Review reader | two hot readers |
| --- | --- | --- | --- |
| persists | 100 | 600 | 100 |
| p50 | 0.00 ms | 0.00 ms | 0.00 ms |
| p95 | 0.00 ms | 0.00 ms | 0.00 ms |
| **max** | **0.06 ms** | **2,551 ms** | **40,577 ms** |
| the write itself (p95) | 3.42 ms | 0.53 ms | 0.15 ms |

**Where one Review load's time goes.** Seven loads in the minute; one load takes **2.55 s**
(p50; max 2.58 s), and it is five lock holds laid end to end:

| command | hold p50 (max) | what it holds the lock for |
| --- | --- | --- |
| `get_analytics` | 682 ms (704) | `prediction_stats` 441 + `hourly_focus_buckets` 149 + `productive_session_streak` 78 + `context_app_counts` 10 |
| `get_daily_summary` | 593 ms (606) | `daily_summary` |
| `get_summary_report` | 449 ms (469) | `prediction_stats` 441 + `context_app_counts` ~10; the other two reads are under 0.1 ms |
| `get_focus_summary` | 433 ms (442) | `prediction_stats` |
| `get_session_history` | 377 ms (395) | `recent_session_summaries` |

The per-command holds sum to the load time (2,534 ms against 2,552 ms), and each matches the
read-latency table's 7-day column, so nothing here is unaccounted for. **The same
`prediction_stats` over the same window is ~1.3 s of the 2.55 s** — three commands compute it
independently. That is **14.12**, and on this fixture it is the largest single share of a load.

**The middle column is the answer to 14.1, and it is a tail, not a load.** Eight of 635
acquisitions of the storage lock were contended in that phase — about one per Review load.
Every other persist took the lock for free, which is why p50 and p95 do not move in any
column; an average, or a p95-only report, would call all three immaterial and be wrong. What
happens is narrower: **each Review load stalls about one persist, and that persist waits out
the whole load.**

- **The wait is the load, not a query.** The worst wait is **2,551 ms** against a
  **2,552 ms** median load and a longest single hold of 704 ms (3.62×). The reader releases
  the lock between commands, but `RankedMutex` wraps a `std::mutex`, which promises no
  fairness, and the releasing thread reacquires before the waiting writer is scheduled. Every
  run so far has shown the same shape — worst wait ≈ one whole load — on both fixtures.
  This separates the fixes: a lock that handed over at command boundaries would bound the
  wait at one hold (~0.7 s); computing `prediction_stats` once per load would shorten the
  load by ~0.9 s; a read connection would remove the wait.
- **Not a dropped-event risk.** 2.55 s at 50 events/s is ~128 of the ring's 65,536 slots
  (`capture_thread.hpp:kCapacity`); the hot-loop ceiling is ~2,029. The buffer absorbs both.
  What is at stake is seconds of unpersisted work, and — because the UI's other storage reads
  queue on the same lock — seconds in which nothing else backed by storage can answer.
- **Measured on the ceiling fixture.** 90 days with every attended second written; a
  duty-cycled day holds fewer rows and reads proportionally faster over a window.

**With the writer let through first (14.1's fix).** `util/writer_priority.hpp` is the gate
`AppState` now uses: the engine announces its persist while it waits for the lock, and each
of the five Review commands yields to an announced persist before taking the lock. The
benchmark runs the Review phase twice in one run, without and with the gate, so the
comparison has one database, one host, and one machine state. That run was noisier than the
table above (holds 25–30% longer, and one 3.8 s `get_session_history` outlier), so its absolute
figures are higher; the comparison between its two columns is the point.

| same run, 7-day Review reader | lock as it was | with writer priority |
| --- | --- | --- |
| Review loads in 60 s | 6 | 7 |
| one load, p50 (max) | 3,704 ms (7,436) | 3,242 ms (3,439) |
| longest single command hold | 3,802 ms | 861 ms |
| writer's wait, p95 | 0.00 ms | 428 ms |
| **writer's wait, max** | **7,423 ms** | **845 ms** |
| worst wait ÷ longest hold | 1.95× | **0.98×** |
| contended acquisitions | 6 of 630 | 64 of 635 |

**The bound holds: no persist sat out a whole load.** The worst wait is under the longest
single hold, which is the most a gate at command boundaries can promise, since the command
already holding the lock finishes first.

The p95 and the contended count **went up, and that is the fix working, not a cost of it.**
The writer persists every 100 ms. Without the gate, one persist per load blocks and the ~35
persists due during that load cannot even be attempted until it gets through, so they queue
behind it and are timed as uncontended once it does. Their lateness is real but invisible to
a lock-wait figure. With the gate the writer gets in at every command boundary, so the waits
are spread across up to five short ones per load instead of one long one. A lock-wait p95 of
428 ms against a schedule that used to slip by seconds is the trade this item asked for.

**The hot-loop column is unstable, which is itself the finding.** Four runs of the same phase
have recorded worst waits of 29.2 s, 65.9 s, 11.2 s and 40.6 s. Two readers passing an unfair
lock between themselves can starve a writer for as long as they keep running, so the ceiling
has no ceiling: it scales with the run, not with a query. Keep it as the bound on what an
unfair lock permits, and do not quote it as what Review costs.

The lock instrument reports a slightly longer worst wait than the writer saw in the hot phase
— 41.0 s against 40.6 s — because it counts every rank-`Storage` acquisition, including
readers waiting on each other. Both figures are of the same run; they measure different
waiters.

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
