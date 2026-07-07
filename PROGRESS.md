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

## 2026-07 — Roadmap review

- Full codebase audit captured in [docs/ROADMAP.md](docs/ROADMAP.md) (prioritized P0–P5).
- Short session checklist lives in [`doc.md`](doc.md).
- Key findings: session/feature-extractor desync, idle training rows, ONNX loop not closed in CI, release workflow missing.

## Next milestones

See [`doc.md`](doc.md) for what you're working on *this session*.

See [docs/BACKLOG.md](docs/BACKLOG.md) for the full prioritized backlog (Tier 0–5).

**Immediate (2026-07):** 60-min smoke test, tagged release dry run, ONNX/heuristic policy decision.

## Task tracker

| File | Role |
|------|------|
| [`doc.md`](doc.md) | Short session checklist (Now / Next / Later) |
| [docs/BACKLOG.md](docs/BACKLOG.md) | Master backlog with file paths and checkboxes |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Index + shipped history |

See [docs/archive/PROGRESS_full.md](docs/archive/PROGRESS_full.md) for the earlier session journal.
