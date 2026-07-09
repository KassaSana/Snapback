# Snapback — progress journal

## 2026-06 — Tauri rewrite (v0.2)

- Renamed product to **Snapback** (kept `focoflow.db` for existing installs).
- Replaced the 4-layer stack (C++/ZeroMQ/Python service/Spring Boot) with one Tauri app: `src-tauri/` + `frontend/`.
- Rust: capture via `rdev` + active window, rolling features, heuristic classifier, SQLite, snapback overlay.
- React: `invoke`/`listen` instead of REST/WebSocket.
- Python: offline training only; added `ml/export_onnx.py`.
- CI: dropped Java; added `cargo check` + `cargo test`.
- Benchmarks: `--benchmark` CLI; results in [docs/BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md).

## 2026-07 — Ship-confidence pass

Closed the original P0/P1 backlog:

- Session ↔ `FeatureExtractor` sync (`feature_session_epoch`)
- Stop persisting rows outside active sessions
- ONNX loop end-to-end (export → train → reload in app)
- CI hardening (frontend build, `cargo test --features onnx`, Windows job)
- Release workflow on `v*` tags (NSIS + DMG)
- Global hotkey labeling, tray icon, in-app training deploy panel
- Feature parity in CI; real CV in `train_baseline`

Reorganized docs:

- [`doc.md`](doc.md) — short session tracker
- [docs/BACKLOG.md](docs/BACKLOG.md) — full task list
- [docs/ROADMAP.md](docs/ROADMAP.md) — index + shipped history

## What's next

See [`doc.md`](doc.md) for the current session. Full list: [docs/BACKLOG.md](docs/BACKLOG.md).

Immediate: 60-min smoke test, tagged release dry run, ONNX/heuristic policy call.

Older session notes: [docs/archive/PROGRESS_full.md](docs/archive/PROGRESS_full.md).
