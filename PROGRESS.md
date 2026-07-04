# Snapback: Progress

## 2026-06 — Tauri overhaul (v0.2)

- **Rename / branding:** switched the product name to **Snapback** across UI, docs, and app metadata (kept `focoflow.db` for backwards compatibility).
- Collapsed 4-layer architecture into **one Tauri desktop app** (`src-tauri/` + `frontend/`).
- **Removed:** Java/Spring Boot backend, C++/ZeroMQ capture stack, Docker compose demo, inference bridge service.
- **Rust core:** cross-platform capture (`rdev` + active window), rolling-window features, heuristic classifier, SQLite, snapback state machine + overlay window.
- **React UI:** Tauri `invoke`/`listen` instead of REST/WebSocket; focus modes, feedback labels, session recap.
- **Python:** kept as offline training tool; added `ml/export_onnx.py`.
- **CI:** dropped Java job; added `cargo check` + `cargo test`.
- **Benchmarks:** added `--benchmark` CLI mode; recorded results in [docs/BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md) (inference latency, 60 s soak, startup timing).

## Next milestones

- Release packaging polish (icons, versioning, Windows installer notes).
- Permissions UX polish (especially macOS onboarding + restart loop).
- ONNX inference wiring in Rust (`--features onnx`) + fallback behavior.
- Minimal local logs + export for debugging (no network tier).

## Task tracker

See [`doc.md`](doc.md) for the up-to-date implementation checklist.

See [docs/archive/PROGRESS_full.md](docs/archive/PROGRESS_full.md) for the earlier session journal.
