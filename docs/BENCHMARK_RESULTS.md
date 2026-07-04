# Benchmark results

Recorded metrics from the built-in `--benchmark` mode and startup logs. Re-run commands in [BENCHMARKS.md](BENCHMARKS.md) to refresh these numbers.

**Last run:** 2026-06-29  
**Machine:** Windows 11 Home, Intel Core i5-12500H, 16 GB RAM  
**Build:** `cargo run --release` (Rust 1.96.0, heuristic classifier — ONNX feature off)

---

## Summary (resume-ready)

| Metric | Value |
|--------|-------|
| Inference latency (p50 / p95 / p99) | **1 µs / 1 µs / 2 µs** (20k runs) |
| Inference throughput | **~33k predictions/sec** (22k runs in 664 ms) |
| Soak reliability | **43.9M predictions in 60 s, crash-free** |
| Soak throughput | **~732k predictions/sec** sustained |
| Process memory (soak) | **~17.5 MB** stable |
| Cold start → app ready | **484 ms** |
| Cold start → setup | **462 ms** |

---

## 1. Inference latency

**Command:**

```bash
cd src-tauri
cargo run --release -- --benchmark --runs 20000 --warmup 2000
```

**Raw output:**

```text
SNAPBACK_BENCH v1
mode=inference
runs=20000
warmup=2000
goal_present=false
latency_us_p50=1
latency_us_p95=1
latency_us_p99=2
mem_bytes_before=19017728
mem_bytes_after=24002560
cpu_pct_sample=0.00
bench_elapsed_ms=664
```

**Notes:**

- Classifier path: heuristic `Classifier::predict()` (not ONNX).
- Memory fields are **bytes** (~18.1 MB → ~22.9 MB during timed run).
- CPU sample near 0% — workload is microseconds per call.

---

## 2. Reliability soak (60 s)

**Command:**

```bash
cd src-tauri
cargo run --release -- --benchmark --runs 1000 --warmup 100 --soak-seconds 60
```

**Inference preamble:**

```text
latency_us_p50=1
latency_us_p95=1
latency_us_p99=1
bench_elapsed_ms=424
```

**Soak progress (every 5 s):**

| Elapsed (s) | Iterations | Memory (bytes) | CPU % |
|-------------|------------|----------------|-------|
| 5 | 4,090,191 | 18,341,888 | 97.28 |
| 10 | 7,169,481 | 18,358,272 | 98.59 |
| 15 | 10,420,349 | 18,358,272 | 97.56 |
| 20 | 13,690,000 | 18,358,272 | 96.90 |
| 25 | 17,082,496 | 18,358,272 | 97.70 |
| 30 | 20,505,000 | 18,362,368 | 96.54 |
| 35 | 24,004,112 | 18,362,368 | 98.14 |
| 40 | 27,236,056 | 18,362,368 | 96.49 |
| 45 | 30,485,788 | 18,362,368 | 95.99 |
| 50 | 33,924,759 | 18,362,368 | 97.91 |
| 55 | 39,009,367 | 18,362,368 | 97.19 |

**Final:**

```text
mode=soak_done
soak_seconds=60
soak_iters=43920925
```

**Notes:**

- High CPU % is expected: tight-loop soak pegs one core.
- Memory held steady (~17.5 MB); no growth over 60 s.
- Safe resume claim: *processed 43.9M classifier predictions over 60 s without crash*.

---

## 3. Startup timing

**Command:**

```bash
cd src-tauri
RUST_LOG=info cargo run --release
```

**Logs:**

```text
startup_ms_to_setup=462
startup_ms_to_ready=484
```

**Notes:**

- `setup` = Tauri `.setup()` (storage + engine start).
- `ready` = `RunEvent::Ready` (app shell usable).
- Includes full Tauri/WebView2 init on a dev `cargo run` build.

---

## Resume bullets (derived from this file)

**Snapback** — Cross-platform focus desktop app (Tauri/Rust + React)

- Built a cross-platform desktop app with a Rust engine and React UI that converts window/app activity into focus signals (distraction risk, drift, goal alignment).
- Implemented on-device classification with **sub-millisecond inference** (p50/p95 **1 µs**, p99 **2 µs** over 20k runs on Windows release build).
- Validated runtime stability via a **60 s soak** processing **43.9M predictions** crash-free; cold start to ready in **484 ms** on i5-12500H / 16 GB RAM.
