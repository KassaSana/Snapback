# Metrics

> **Current benchmarks:** Run `cargo run --release -- --benchmark` in `src-tauri/`. Latest numbers in [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md). How-to in [BENCHMARKS.md](BENCHMARKS.md).
>
> **Below:** Legacy metrics from the removed C++ ring-buffer stack and Python event-log replay (January 2026). Kept for reference.

---

## Current (v0.2)

| What | Command | Doc |
|------|---------|-----|
| Inference latency (p50/p95/p99) | `cargo run --release -- --benchmark --runs 20000` | [BENCHMARKS.md](BENCHMARKS.md) |
| Soak / crash-free runtime | `cargo run --release -- --benchmark --soak-seconds 60` | [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) |
| Startup time | Run app with `RUST_LOG=info`, read `startup_ms_to_ready` | [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) |
| Classifier quality | `python -m tools.benchmark_classifier_quality` | [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) §4 |

Resume bullets from the July 2026 run (Windows, i5-12500H):

- Heuristic inference p99: **2 µs**
- ~**33k predictions/sec** (20k runs in 664 ms)
- **43.9M predictions in 60 s**, crash-free soak
- Cold start → ready: **484 ms**

---

## Legacy — log replay (C++ event logs)

Generated from a bundled log with:

```powershell
python -m ml.metrics_report --log-path .\events_test_2026-01-02.log --benchmark-features --output-json docs\metrics.json
```

Snapshot from `docs/metrics.json`:

```text
total_events: 100
duration_seconds: 9.90
events_per_second: 10.10
feature_extraction_eps: 6467.01
```

This path used the old binary event log format. Training today goes through SQLite export (`ml/sqlite_export.py`).

---

## Legacy — C++ ring buffer (`benchmark.exe`)

```powershell
.\tools\benchmark.exe
```

Last recorded (2026-01-04):

```text
try_push p99=135.50 ns
try_pop  p99=135.86 ns
throughput=5734371 ev/s (push+pop single-thread)
```

The C++ capture engine is gone. The Rust engine polls capture events on a channel and runs `FeatureExtractor` at ~1 Hz.

---

## Legacy verification runs (2026-01-04)

Log replay (three runs): feature_extraction_eps 6383–6467.  
Ring buffer (three runs): throughput ~5.73–5.75M ev/s.

These numbers are from the old stack. Re-run the v0.2 commands above for current citeable metrics.
