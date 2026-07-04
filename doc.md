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
- [x] Emit real capture signals: idle events + mouse speed (`capture/thread.rs`, local — commit pending)

### Now (highest leverage)
- [ ] **Wire context snapshots** — `save_context_snapshot` exists but nothing calls it.
  - Touch: `src-tauri/src/state.rs`, `src-tauri/src/snapback/tracker.rs`
- [ ] **Commit + push** idle/mouse-speed capture changes from this machine

### Next (product UX)
- [ ] **Context timeline UI** (browse snapshots for the active session)
  - Touch: new command(s) + panel in `frontend/src/App.tsx`
- [ ] **Snapback overlay UX polish**
  - keyboard dismiss, optional no-focus-steal, positioning tweaks
- [ ] **Permission onboarding UX**
  - clearer “capture failed” status if `rdev::listen` stops, OS-specific guidance

### Later
- [ ] **Labeling improvements**
  - hotkey labeling
  - end-of-session survey label
  - simple automatic labels based on session outcome
- [ ] **Release packaging** (icons, versioning, Windows installer notes)
