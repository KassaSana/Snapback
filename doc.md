## Snapback — work tracker

Single source of truth for what's left. Keep it short; update after each session.

**Details:** see [docs/ROADMAP.md](docs/ROADMAP.md) for full backlog and file paths.

### Done (on `master`)
- [x] Rust toolchain + `cargo test` passing locally
- [x] Persist full prediction fields (thrash / drift / goal alignment) in SQLite
- [x] Feature snapshots during sessions (`save_feature_snapshot` in engine loop)
- [x] App rules CRUD (SQLite + Tauri commands + UI)
- [x] Training data export (`ml/sqlite_export.py`, in-app export command + UI)
- [x] Feature parity fixtures + Rust validation (`--feature-parity`, `engine/parity.rs`)
- [x] ONNX runtime wiring (behind `--features onnx`, heuristic fallback)
- [x] Emit real capture signals: idle events + mouse speed (`capture/thread.rs`)
- [x] Wire context snapshots during active sessions
- [x] Context timeline UI (command + panel in `App.tsx`)
- [x] Snapback overlay UX polish (keyboard dismiss, no-focus-steal, positioning)
- [x] Permission onboarding UX (capture failure status + OS-specific guidance)
- [x] Labeling improvements (hotkeys, end-of-session survey, auto labels)
- [x] Release packaging (icons, cross-platform scripts, `docs/DEPLOYMENT.md`)

### Now (P0 — data & ship confidence)
- [x] **Commit + push** uncommitted work from this machine
- [x] **Reset `FeatureExtractor` on session start/stop** — fix `seconds_since_session_start`
- [x] **Stop saving `session_id = "idle"`** feature/prediction rows
- [x] **CI hardening** — `npm run build`, `cargo test --features onnx`, Windows job
- [x] **Release CI** — GitHub Actions workflow for Windows/macOS installers on tag

### Next (P1 — product)
- [x] **Close ONNX loop** — pipeline → export → reload in app; dev/release builds use `--features onnx`
- [x] **Training deploy UX** — in-app train-from-export + 3-step deploy panel (export → train → reload)
- [x] **Global hotkey labeling** (`tauri-plugin-global-shortcut` — Ctrl+Shift+1–4)
- [x] **Tray icon** — show/hide/quit menu; left-click toggles window

### Shipped (ML / quality)
- [x] ML: real CV in `train_baseline` (`time_series_splits` wired; reports `cv_*` + `in_sample_*` metrics)
- [x] ML: ONNX integration tests
- [x] ML: feature parity in CI (`ml.feature_parity_cli`)
- [x] Benchmark / ONNX model quality pass (`docs/BENCHMARK_RESULTS.md`)

### Later (P4 — defer unless blocked)
- [ ] **Doc cleanup** — archive stale TDD/ARCHITECTURE/SCHEMAS sections ([ROADMAP P4](docs/ROADMAP.md)); **not release-blocking**; do when docs slow you down, not before smoke test
- [ ] Linux distro packaging smoke test
