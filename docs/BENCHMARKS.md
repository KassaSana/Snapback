# Benchmarks

Built-in `--benchmark` mode prints copy-paste metrics. Latest results: [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md).

Run from `src-tauri/` unless noted.

## Inference latency benchmark (median / p95 / p99)

Runs the in-app `Classifier::predict()` in a tight loop and prints latency percentiles in **microseconds**.

```powershell
cd src-tauri
cargo run --release -- --benchmark --runs 20000 --warmup 2000
```

Optional: include a goal string (bench will report `goal_present=true`).

```powershell
cd src-tauri
cargo run --release -- --benchmark --runs 20000 --warmup 2000 --goal "implement feature X"
```

Example output (see [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) for latest measured values):

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

Memory fields are **bytes** (divide by 1,048,576 for MB).

## Reliability / soak run

Runs the classifier path continuously and reports every 5 seconds.

```powershell
cd src-tauri
cargo run --release -- --benchmark --soak-seconds 60
```

Use whatever duration you actually ran when citing results. [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) records a **60 s** soak (43.9M iterations). For a longer run, increase `--soak-seconds` — e.g. 3600 for one hour.

## Startup timing (app launch)

When running the normal app, the backend logs startup milestones:

- `startup_ms_to_setup`: process start → Tauri `.setup()` entered
- `startup_ms_to_ready`: process start → Tauri `RunEvent::Ready`

Run the app and capture logs:

```powershell
cd src-tauri
$env:RUST_LOG="info"
cargo run
```

Then cite: “cold start to ready: **X ms** on **<your machine>**”.

## Classifier quality benchmark (heuristic vs ONNX vs XGBoost)

Trains on synthetic data (or reuses `data/` artifacts), exports ONNX, and compares backends on the same labeled CSV.

```powershell
py -m tools.benchmark_classifier_quality
```

Reuse existing artifacts:

```powershell
py -m tools.benchmark_classifier_quality --skip-train
```

Writes `data/benchmark_quality.json` and prints a summary. See [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) §4 for latest numbers.

Individual Rust eval (when `ort` builds on your host):

```powershell
cd src-tauri
cargo run --features onnx -- --classifier-eval ../data/labeled.csv --backend heuristic
cargo run --features onnx -- --classifier-eval ../data/labeled.csv --backend onnx --model-onnx ../data/model.onnx
```

Dual latency benchmark (heuristic + ONNX in one run):

```powershell
cd src-tauri
cargo run --release --features onnx -- --benchmark --runs 5000 --warmup 500 --onnx-model ../data/model.onnx
```

