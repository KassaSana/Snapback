# Snapback — Master Backlog

**Last audit:** 2026-07  
**Status:** Usable alpha — core loop ships; remaining work is confidence, polish, tests, and doc reconciliation.

**How to use this file**

| File | Purpose | Update when |
|------|---------|-------------|
| [`doc.md`](../doc.md) | **Session tracker** — what you're doing *now* (5–15 lines) | Start/end of every work session |
| **This file** | **Full backlog** — every gap, tier, and file path | After completing epics or quarterly audit |
| [`docs/ROADMAP.md`](ROADMAP.md) | Short index + link here (legacy detail archived below) | Rarely |

**Workstreams** (pick one per week, not all tiers at once)

- **A — Ship it:** smoke test → Tauri CI → tagged release → permissions honesty
- **B — Trust the model:** ONNX policy → training UX → benchmarks → quality gate
- **C — Can't break silently:** one regression test per session
- **D — Docs stop lying:** when confusion blocks you, not before smoke test

---

## Current state (verified in code)

```text
capture (rdev + active window) → FeatureExtractor → classifier (heuristic + ONNX)
  → SQLite (session-gated) → React UI + snapback overlay
  → export → pipeline_cli → model.onnx → reload in app
```

- **21 Tauri commands** in `src-tauri/src/lib.rs` — all wired in `frontend/src/api.ts`
- **~42 Rust tests**, **13 Python test files**, **2 frontend tests**
- **CI:** Python, frontend build, Rust + ONNX, Windows, feature parity
- **Release:** NSIS + DMG on `v*` tags (`.github/workflows/release.yml`)

---

## Tier 0 — Ship confidence

Do before calling it beta.

### 0.1 Real-world validation

- [ ] **60-min session smoke test** — capture → predictions → label (UI + hotkeys) → snapback → stop → recap → export → train → reload
- [ ] **Tagged release dry run** — `v0.2.x` tag → confirm Windows NSIS + macOS DMG install cleanly
- [ ] **ONNX vs heuristic comparison** — same session, both backends; decide which you trust

### 0.2 Permission & capture honesty

| Task | Files | Problem |
|------|-------|---------|
| [ ] Fix macOS `probe_capture` | `capture/permissions.rs:128-131` | Probe checks active window only, not `rdev` |
| [ ] Windows/Linux probe | `permissions.rs:133-136` | `probe_capture()` always `true` on non-macOS |
| [ ] Probe vs capture-thread mismatch UX | `state.rs`, `App.tsx` | User sees why probe OK but capture died |
| [ ] Hotkey registration failure feedback | `label_shortcuts.rs`, `api.ts`, `App.tsx` | Failure only logged, not shown in UI |

### 0.3 ONNX runtime truth

| Task | Files | Problem |
|------|-------|---------|
| [ ] **Decide ONNX + heuristic hybrid policy** | `engine/classifier.rs:222-234` | ONNX output set, then heuristic rules still override `focus_state` |
| [ ] Align training eval with production | `engine/classifier_eval.rs`, `ml/classifier_quality.py` | CV metrics may not match runtime |
| [ ] Windows ONNX dev ergonomics | `docs/BENCHMARK_RESULTS.md`, `docs/DEPLOYMENT.md` | Local MSVC + `ort` linker issues |
| [ ] Training deploy partial-success clarity | `training_deploy.rs:212-217`, `App.tsx` | Success returned when ONNX export skipped |

### 0.4 CI / release gaps

| Task | Files | Gap |
|------|-------|-----|
| [ ] Windows CI with ONNX | `.github/workflows/ci.yml` `rust-windows` | No `cargo test --features onnx` on Windows |
| [ ] Tauri build smoke in CI | `ci.yml` | Never runs `tauri build` |
| [ ] Python CI with ML deps | `ci.yml` python job | No `xgboost` / `onnxmltools` |
| [ ] Classifier quality gate | `tools/benchmark_classifier_quality.py` | No automated regression thresholds |
| [ ] Feature parity on Windows (optional) | `ci.yml` | Parity job Ubuntu-only |

---

## Tier 1 — Product polish

### 1.1 Training & model UX

- [ ] Surface Python missing deps clearly (`training_deploy.rs`, `App.tsx`)
- [ ] Model metadata in health UI (path, backend, train time, CV metrics)
- [ ] Auto-copy trained model to `app_data_dir/model.onnx`
- [ ] Fail fast when majority-classifier stub (`ml/export_onnx.py`)
- [ ] Real-user training guide (min sessions, labels, when to retrain)

### 1.2 Permissions & onboarding

- [ ] First-run permission wizard (`App.tsx`, `permissions.rs`)
- [ ] Separate “capture thread alive” vs “permissions OK” in health UI
- [ ] Prominent Wayland warning before first session (Linux)

### 1.3 App rules honesty

- [ ] UI copy: “Block” affects scoring only, does not close apps (`App.tsx:882-951`)
- [ ] Rule preview: “this window would match rule X”
- *(vision)* Real block intervention → Tier 5

### 1.4 Snapback & overlay

- [ ] Surface overlay window creation errors (`snapback/overlay.rs:19-31`)
- [ ] Type `onSnapback` payload (`api.ts:365-366`)
- [ ] Event-driven context timeline refresh (today: 30s poll in `App.tsx`)

### 1.5 Session & data quality

- [ ] Regression test: no DB writes without active session (`state.rs:254-265`, `storage/mod.rs`)
- [ ] Extend `FeatureExtractor` tests (idle, mouse, session boundaries)
- [ ] Pre-export summary in UI (session count, label count, feature rows)

---

## Tier 2 — ML pipeline depth

### 2.1 Labeling path

- [ ] **Decide fate of `ml/labeling.py`** — stub `Labeler`; production labels live in Rust SQLite only
  - Option A: keep as Python test types only
  - Option B: bridge to SQLite export in training scripts
  - Option C: delete stub, move types to `ml/types.py`
- [ ] Label source parity (hotkey / survey / auto) in `sqlite_export.py`
- [ ] Minimum dataset checks in `pipeline_cli.py` / `training_pipeline.py`

### 2.2 Model quality & benchmarks

- [ ] ONNX numbers in `docs/BENCHMARK_RESULTS.md`
- [ ] Reconcile soak duration: `BENCHMARKS.md` says 3600s, results are 60s
- [ ] Run `--classifier-eval` in CI (`engine/classifier_eval.rs`)
- [ ] Document synthetic vs real labeled data strategy

### 2.3 Legacy ML cleanup

- [ ] Mark legacy C++ path: `event_log_reader.py`, `event_schema.py`, `metrics_report.py`, `generate_log.py`
- [ ] Archive or delete C++ event log stack when safe
- [ ] Cosmetic: rename drift (“Neural Focus” in `labeling.py`, `focoflow.db` filename)

---

## Tier 3 — Test coverage

### Rust — has tests

`classifier.rs` (8), `app_context.rs` (8), `storage/mod.rs` (7), `features.rs` (2), `tracker.rs` (4), `goal_alignment.rs` (4), `training_deploy.rs` (3), `onnx_model.rs` (3+1), `parity.rs` (1), `capture/thread.rs` (2), `title_parser.rs` (1), `classifier_eval.rs` (1)

### Rust — no tests yet

`commands.rs`, `state.rs`, `focus_modes.rs`, `permissions.rs`, `label_shortcuts.rs`, `tray.rs`, `overlay.rs`, `bench.rs`

### Highest-ROI tests to add

- [ ] `focus_modes.rs` — `check_hyperfocus` thresholds and 600s alert interval
- [ ] Session-gated persistence (`storage/mod.rs`)
- [ ] `FeatureExtractor` — idle, mouse speed, context switches, title changes
- [ ] Command harness — start/stop session syncs `feature_session_epoch`
- [ ] `permissions.rs` — platform messages and setup steps
- [ ] ONNX override policy (after Tier 0.3 decision)
- [ ] Frontend `api.ts` mapper tests
- [ ] E2E / Tauri WebDriver (later)

---

## Tier 4 — Documentation reconciliation

Do when docs slow you down — **not before Tier 0 smoke test**.

| Doc | Issue | Action |
|-----|-------|--------|
| [ ] `docs/ROADMAP.md` | Stale P0/P1 “open” items | Index only; detail lives here |
| [ ] `PROGRESS.md` | “Immediate” items already done | Point to `doc.md` + this file |
| [ ] `docs/ARCHITECTURE.md` | C++/ZeroMQ/Spring diagrams | v0.2 banner + archive legacy |
| [ ] `docs/TECHNICAL_DESIGN_DOCUMENT.md` | LSTM / PostgreSQL vision | Banner + vision vs shipped table |
| [ ] `docs/SCHEMAS.md` | PostgreSQL + Python `# TODO` fields Rust already has | Replace with SQLite schema from `storage/mod.rs` |
| [ ] `docs/CONTEXT_RECOVERY_DESIGN.md` | C++ checkboxes all `[ ]` | Mark Rust items done |
| [ ] `docs/METRICS.md` | C++ `benchmark.exe` | Point to `bench.rs` + `BENCHMARK_RESULTS.md` |
| [ ] `docs/BENCHMARKS.md` | 3600s soak vs 60s reality | Align with actual runs |

---

## Tier 5 — Vision (defer)

- [ ] App rules “block” as real intervention (today: scoring boost only in `classifier.rs:230`)
- [ ] Analytics / charts (weekly trends, intervention history)
- [ ] LSTM / 30–60s lookahead (engine is ~1 Hz reactive in `state.rs:222`)
- [ ] Linux release packaging (AppImage/deb) — `release.yml` is Win + macOS only
- [ ] Linux distro smoke test
- [ ] Wayland capture (global hooks blocked)
- [ ] VS Code extension / browser domain tracking
- [ ] Model versioning (`model.onnx` overwritten; no metadata schema)

---

## Tech debt register

| Issue | Location | Severity |
|-------|----------|----------|
| Capture events dropped on full channel | `capture/thread.rs` (`let _ = event_tx.send`) | Medium |
| Storage save failures only `log::warn!` | `state.rs:256-300` | Medium |
| Mouse x/y/speed = 0 on non-mouse events | `capture/thread.rs:88-96` | Low |
| 1 Hz prediction hardcoded | `state.rs:222` | By design |
| Global shortcut capability not explicit | `capabilities/default.json` | Verify on clean install |

---

## Shipped (do not re-do)

Verified 2026-07 — see [`doc.md`](../doc.md) done list.

- Session ↔ `FeatureExtractor` reset (`feature_session_epoch`)
- No idle-session DB pollution (engine gates on `get_active_session()`)
- ONNX loop: export → `model.onnx` → `reload_classifier_model`
- CI hardening + release workflow on tags
- Global hotkey labeling, tray, in-app training deploy
- Context timeline, snapback overlay polish, permissions UX
- Feature parity fixtures + CI; real CV in `train_baseline`
