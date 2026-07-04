## Snapback — work tracker

Single source of truth for what's left. Keep it short; update after each session.

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
- [x] Release packaging (icons, NSIS config, `docs/DEPLOYMENT.md`, build scripts)

### Now (highest leverage)
- [ ] **Commit + push** session work from this machine (multiple short commits OK)

### Next (product)
- [ ] **Global hotkey labeling** (`tauri-plugin-global-shortcut` — window hotkeys work today)
- [ ] **Release CI** — GitHub Actions workflow to build Windows/macOS installers on tag

### Later
- [ ] Benchmark / ONNX model quality pass
- [ ] Linux distro packaging smoke test
