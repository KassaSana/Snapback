# Benchmarking

Snapback has a standalone C++ benchmark target for repeatable performance checks of
the core engine loop. It intentionally avoids live desktop capture because real
keyboard, mouse, and active-window state make benchmark results noisy and hard to
compare.

## Run

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_benchmarks.ps1
```

By default the benchmark replays a three-hour simulated work session. To change
the trace length:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_benchmarks.ps1 -Minutes 480
```

The script configures CMake with `SNAPBACK_BUILD_BENCHMARKS=ON`, builds
`snapback_benchmarks`, and runs it.

## What It Measures

The generated trace mixes the kinds of events a real demo session sees:

- steady IDE work with keypresses, mouse movement, and title changes
- short browser/Discord distraction bursts
- meeting-like windows with lower input density
- enough elapsed simulated time to exercise 30-second context checkpoints

The benchmark reports total time, operations per second, mean latency, and p50,
p95, p99, and max per-event latency for:

- feature extraction plus heuristic classification
- context-snapshot tracker decisions
- SQLite prediction and feature writes plus recap reads
- full `AppState::process_event_for_test` replay

## Hot-path micro-benchmarks

`snapback_hotpath_benchmarks` (built alongside the replay when `SNAPBACK_BUILD_BENCHMARKS=ON`)
isolates the four critical paths rather than timing a whole session:

1. **Producer** — `RingBuffer::push()` latency, both a pure move (the true ring cost) and
   with event construction (exposing the upstream `std::string` allocation).
2. **Consumer** — drain 5,000 events → feature extraction → heuristic classification.
3. **Lock contention** — `AppState` reads (uncontended vs. a writer holding `mutex_`),
   to size worst-case UI-read stalls.
4. **SQLite persistence** — per-tick insert (prediction + feature snapshot), on disk
   (the real path) and in memory (isolates SQL cost from durability sync).

Run it directly:

```powershell
cmake -S . -B build -DSNAPBACK_BUILD_BENCHMARKS=ON
cmake --build build --config Release --target snapback_hotpath_benchmarks
.\build\Release\snapback_hotpath_benchmarks.exe
```

## Interpreting Results

The most important number for demo readiness is `AppState replay` p95. The app's
engine tick drains captured events on a short interval, so sustained low
per-event latency matters more than a single fastest sample.

Use the benchmark as a regression check after changes to:

- `src/app/state.cpp`
- `src/engine/features.cpp`
- `src/engine/classifier.cpp`
- `src/storage/storage.cpp`
- `src/snapback/tracker.cpp`

For release hardening, capture a baseline on the Windows demo machine and compare
future branches against that same machine/configuration.

## What the numbers mean

Each line reports the cost of **one operation** (one `push`, one event drained + classified,
one UI read, one persisted tick), sampled across the whole run:

- **ops/s** — throughput: operations completed per second over the whole loop (wall time,
  excludes per-sample timer overhead). Higher is better.
- **mean** — average per-op latency. Sensitive to outliers.
- **p50 (median)** — half of ops were faster than this. The "typical" cost; the most honest
  single number for steady-state feel.
- **p95 / p99** — tail latency: 95%/99% of ops were faster. This is what determines whether
  the UI ever stutters — a good mean with a bad p99 still janks occasionally.
- **max** — the single worst sample. Usually an OS scheduling hiccup, a page fault, or (for
  SQLite) a WAL checkpoint; treat a lone high `max` with a healthy p99 as noise.

Rule of thumb for this app: **p99 is the number to watch**, because the engine tick and UI
reads must stay responsive under the *worst* case, not just on average.

## Measured results (12th Gen Intel i5-12500H, Release/MSVC)

These are the deltas from the performance pass (see `docs/system_architecture.md` §7). They
are the reference baseline; re-run and compare after changes to the files listed above.

**Per-tick SQLite write** (prediction + feature snapshot) — the engine's write path:

| Build stage | mean | p50 | p99 | max |
|---|---|---|---|---|
| Original (`synchronous=FULL`, autocommit, prepare-per-call) | 5959 µs | 5785 | 10299 | 20339 |
| + WAL + `synchronous=NORMAL` + per-tick transaction | 125 µs | 71 | 595 | 15171 |
| + prepared-statement cache + PRAGMA tuning | **~75 µs** | **~50** | ~530 | (WAL-checkpoint spikes) |
| in-memory reference (isolates pure SQL cost) | **~12 µs** | ~11 | ~35 | — |

→ **~80× faster** per tick end-to-end. The in-memory number shows statement caching removed
~⅔ of the pure SQL cost (prepare/parse was dominating a simple insert).

**Contended UI read** (`AppState::health()` while a writer holds the engine locks):

| Build stage | mean | p99 | max |
|---|---|---|---|
| Single `mutex_` across the whole tick (incl. disk write) | 34.9 µs | 0.4 µs | **90,043 µs** |
| Split `storage_mutex_`; persist off the state lock | **0.23 µs** | 0.3 µs | **216 µs** |

→ worst-case UI-read stall dropped **~415×** (90 ms → 0.2 ms). The tick now holds `mutex_`
only for the in-memory compute and writes to SQLite under a separate lock, so reads no longer
wait on disk.

**Producer `RingBuffer::push()`** — the OS-hook hot path:

| Variant | mean | p99 | notes |
|---|---|---|---|
| pure move (true ring cost) | ~30 ns | 100 ns | wait-free; p50 sits at the ~100 ns timer floor |
| + event construction | ~100 ns | 100 ns | the extra cost is the `std::string` allocation, not the ring |

→ ~17 M pushes/s — six orders of magnitude above human input; the lock-free design is
massively over-provisioned and never drops under real load.

## Event interning (FeatureExtractor)

The rolling 30 s / 5 min windows previously stored full `CaptureEvent` copies (two
`std::string` fields per event × two deques). `ingest()` now builds a POD `WindowedEvent`
(event type, timestamp, mouse/idle scalars, interned `app_id`) and interns `app_name` into
a session-local `unordered_map<string, uint32_t>`. `unique_apps_5min` counts distinct ids,
which is 1:1 with distinct names — **feature parity is unchanged** (guarded by
`test_engine.cpp`).

Why bother at ~1 prediction/sec? Under heavy mouse-move load the deques can hold thousands
of entries; interning removes per-event string copies/allocs from the consumer-side hot
loop. The hook still constructs one `CaptureEvent` with strings per OS callback; the win is
on the engine side where every event is ingested.

## Caveats

- **Timer floor.** Each per-event sample wraps the work in a `steady_clock` read, whose
  own overhead is on the order of tens of nanoseconds. For the cheapest stages (e.g. the
  context tracker at well under a microsecond per event) the reported `mean`/`p50` sit at
  the timer's resolution — treat those as "effectively free," and use the numbers for
  *relative* comparison between branches rather than as absolute per-op costs.
- **Heuristic backend, in-memory SQLite.** The replay deliberately excludes ONNX and disk
  I/O so results are deterministic. A build with the ONNX backend or an on-disk database
  will show higher `feature + classifier` and `storage` latencies respectively.
- **Reference point.** On the current dev machine a 180-minute trace (~9.2k events) runs
  the full `AppState replay` at ~35–40k events/sec (mean ~25–30 µs, p95 < 50 µs), which is
  orders of magnitude above the human-paced capture rate — SQLite writes dominate.
