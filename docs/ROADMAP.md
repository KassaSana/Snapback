# Snapback Roadmap

Prioritized backlog from a full codebase review (2026-07). For the short session checklist, see [`doc.md`](../doc.md).

**Current state:** The core loop works — capture → features → heuristic classifier → SQLite → React UI → snapback overlay. The app is usable alpha; release confidence and ML loop closure are the main gaps.

---

## P0 — Data correctness & ship confidence

Fix these before trusting training exports or cutting releases.

### Session ↔ feature extractor sync

**Done (2026-07):** `FeatureExtractor::reset_for_session` runs on session start/stop via `feature_session_epoch` in `AppState`. Engine loop resets tracker and session-relative features at boundaries.

### Stop persisting idle rows

**Done (2026-07):** Predictions, feature snapshots, and snapback records persist only during an active focus session. Live UI still receives prediction events between sessions.

### Close the ONNX loop

ONNX is wired behind `--features onnx` but not validated end-to-end:

- Not in default Cargo features → release builds use heuristics only
- CI never builds/tests with `--features onnx`
- `ml/pipeline_cli.py` stops at `model.json` — no automatic ONNX export or install into app data
- `README.md` roadmap still lists ONNX as unchecked; `doc.md` marks wiring done

- **Touch:** `src-tauri/Cargo.toml`, `.github/workflows/ci.yml`, `ml/pipeline_cli.py`, `ml/export_onnx.py`, `src-tauri/src/engine/onnx_model.rs`

### Release CI

**Done (2026-07):** `.github/workflows/release.yml` builds NSIS (Windows) and DMG (macOS) on `v*` tags and uploads release assets.

### CI hardening

**Done (2026-07):** `ci.yml` adds frontend production build, `cargo check/test --features onnx`, and a Windows `cargo test` job.

---

## P1 — Product polish

| Item | Status | Notes |
|------|--------|-------|
| Global hotkey labeling | Window-only Ctrl+Shift+1–4 | `tauri-plugin-global-shortcut` |
| Remove/gate dev UI | "Send sample prediction" in dashboard | `send_test_prediction` stub |
| Tray icon | `tray-icon` feature enabled, unused | Implement or drop from `Cargo.toml` |
| macOS permission probe | Approximate | May report OK before `rdev` fails — `capture/permissions.rs` |
| Training deploy UX | Export works; train/deploy is manual CLI | Model status UI or deploy-from-export path |

---

## P2 — ML pipeline

| Item | Gap |
|------|-----|
| `ml/labeling.py` | Stub — app labels live in SQLite; Python side disconnected |
| Cross-validation | `time_series_splits()` exists; `train_baseline()` doesn't use it |
| Majority classifier | Cannot export to ONNX |
| ONNX tests | Import-only; no fixture model inference test |
| Feature parity in CI | Rust `--feature-parity` vs Python CLI not compared |
| LSTM / lookahead | Documented in old TDD; not implemented (reactive ~1 Hz only) |

**Pragmatic path:** XGBoost → ONNX → Rust inference with real CV metrics. Defer LSTM until the loop is sticky.

---

## P3 — Tests & benchmarks

**Rust gaps:** `FeatureExtractor`, `focus_modes`, `commands`, `state` engine loop.

**Frontend gaps:** `App.tsx`, `api.ts` mappers — only `utils.test.ts` and `trainingHints.test.ts` today.

**Benchmarks:** `docs/BENCHMARK_RESULTS.md` is Windows-only, heuristic-only, 60s soak (docs describe 3600s). No ONNX numbers, no CI regression.

---

## P4 — Documentation cleanup

Several docs still describe the removed C++/Spring/PostgreSQL/LSTM stack:

| Doc | Issue |
|-----|-------|
| `docs/ARCHITECTURE.md` | Legacy multi-layer sections |
| `docs/TECHNICAL_DESIGN_DOCUMENT.md` | Predictive/LSTM vision vs current Tauri reality |
| `docs/SCHEMAS.md` | PostgreSQL schema; Python examples with `# TODO` fields Rust already implements |
| `docs/CONTEXT_RECOVERY_DESIGN.md` | Checklist `[ ]` though snapback is largely built |
| `docs/METRICS.md` | References old C++ `benchmark.exe` |
| `PROGRESS.md` | "Next milestones" stale |

Add v0.2 banners or archive stale sections; keep `doc.md` as the short tracker.

---

## P5 — Later / vision

- Analytics/charts (weekly trends, intervention history)
- App rules "block" as real intervention (today: scoring boost only)
- Linux/Wayland capture (X11-only warning today)
- Sequence/LSTM models and true 30–60s prediction (large scope)
- Linux distro packaging smoke test

---

## Suggested order

1. Session reset + idle row pollution
2. CI hardening (build, Windows, ONNX flag)
3. Release workflow on tags
4. ONNX pipeline end-to-end
5. Global hotkeys + remove dev stubs
6. Doc reconciliation
7. Benchmarks + ONNX quality pass

---

## What's already wired

All 18 Tauri commands are registered and used. Engine loop, context snapshots, timeline UI, snapback overlay, permissions UX, labeling, and training export are complete.

See [`doc.md`](../doc.md) done list for the session-by-session checklist.
