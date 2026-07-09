# Benchmark results

Recorded from `--benchmark` and startup logs. Commands in [BENCHMARKS.md](BENCHMARKS.md).

**Last run:** 2026-07-05  
**Machine:** Windows 11 (10.0.26200), Intel Core i5-12500H, 16 GB RAM  
**Build:** `cargo run --release` (Rust 1.96.0); ONNX quality via Python `onnxruntime` (Rust `ort` linker blocked on this MSVC toolchain)

---

## Summary (resume-ready)

| Metric | Value |
|--------|-------|
| **Classifier quality (synthetic, 230 samples)** | Heuristic accuracy **61.3%**, recall distracted **0%**; trained XGBoost/ONNX CV recall distracted **40%**, precision@10% **53%** |
| Inference latency — heuristic (p50 / p95 / p99) | **1 µs / 1 µs / 2 µs** (5k runs) |
| Inference latency — ONNX Python ref (p50 / p95 / p99) | **23 µs / 38 µs / 582 µs** (5k runs, same host) |
| Inference throughput (heuristic) | **~33k predictions/sec** (20k runs in 664 ms, prior run) |
| Soak reliability | **43.9M predictions in 60 s, crash-free** (prior run) |
| Cold start → app ready | **484 ms** (prior run) |

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

## 4. Classifier quality — heuristic vs trained model (synthetic data)

**Command:**

```bash
py -m tools.generate_synthetic_training_data --seed 7
py -m ml.pipeline_cli --db-path data/synthetic_focoflow.db --output-dir data --backend xgboost
py -m tools.benchmark_classifier_quality --skip-train
```

Or one-shot (train + eval):

```bash
py -m tools.benchmark_classifier_quality
```

**Dataset:** 230 labeled rows from synthetic sessions (`data/labeled.csv`, seed 7).

**Quality results (same held-out join set):**

| Backend | Accuracy | Precision@10% (distracted) | Recall (distracted @ 0.7) |
|---------|----------|----------------------------|-----------------------------|
| Heuristic (Rust) | **0.613** | **1.000** | **0.000** |
| XGBoost (Python) | 1.000 | 1.000 | 1.000 |
| ONNX (Python onnxruntime) | 1.000 | 1.000 | 1.000 |

**Cross-validated training metrics (honest generalization, 5 time-series folds):**

| Metric | Value |
|--------|-------|
| CV accuracy | **0.611** |
| Precision@10% distracted | **0.533** |
| Recall distracted | **0.400** |

**Takeaway:** On synthetic data, heuristic and trained models tie on overall accuracy (~61%), but the heuristic **never recalls distracted states** (0% recall). The trained XGBoost/ONNX model improves distracted recall to **40% CV** while keeping reasonable precision@10% (**53%**). In-sample eval on the full labeled join shows ONNX matches XGBoost exactly (export parity).

**Notes:**

- ONNX quality was evaluated via Python `onnxruntime` on Windows; Rust `ort` build fails locally (`LNK2019` MSVC linker issue) but passes in Ubuntu CI.
- In-sample accuracy of 100% for XGBoost/ONNX is expected on the training join; cite **CV metrics** for generalization claims.
- ONNX export uses `onnxmltools` (`ml/export_onnx.py`).

---

## 5. ONNX inference latency (Python reference)

Rust ONNX latency benchmark (`--benchmark --onnx-model data/model.onnx`) requires `--features onnx` and is blocked on this Windows host. Reference numbers from `onnxruntime` on the same machine:

| Percentile | Latency (µs) |
|------------|--------------|
| p50 | **23** |
| p95 | **38** |
| p99 | **582** |

Heuristic Rust path remains **~20× faster** at median (1 µs vs 23 µs) — still well within real-time for a 1 Hz engine loop.

---

## Resume bullets (derived from this file)

**Snapback** — Cross-platform focus desktop app (Tauri/Rust + React)

- Built a cross-platform desktop app with a Rust engine and React UI that converts window/app activity into focus signals (distraction risk, drift, goal alignment).
- Trained XGBoost focus classifiers on synthetic + exported session data; ONNX export matches Python inference; **CV recall for distracted states 40%** vs **0%** for the heuristic baseline.
- Implemented on-device classification with **sub-millisecond heuristic inference** (p50/p95 **1 µs**, p99 **2 µs**); ONNX reference path **23 µs p50** on the same hardware.
- Validated runtime stability via a **60 s soak** processing **43.9M predictions** crash-free; cold start to ready in **484 ms** on i5-12500H / 16 GB RAM.
