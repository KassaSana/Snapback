# Snapback backlog

Last reviewed: 2026-07. Alpha is usable; main gaps are confidence (smoke test, permissions, ONNX behavior), polish, tests, and stale docs.

**Planning docs**

| File | Role |
|------|------|
| [`doc.md`](../doc.md) | What you're doing this session |
| **This file** | Everything else, with file paths |
| [`CODE_HEALTH_REVIEW.md`](CODE_HEALTH_REVIEW.md) | Review findings and fix queue |
| [`ROADMAP.md`](ROADMAP.md) | Shipped history + tier summary |

Pick one focus per week — don't try to clear every tier at once.

---

## Where things stand

```
capture → FeatureExtractor → classifier (heuristic / ONNX)
  → SQLite (only during active sessions) → React UI + snapback overlay
  → export → pipeline_cli → model.onnx → reload in app
```

- 21 Tauri commands in `lib.rs`, all used from `frontend/src/api.ts`
- ~42 Rust tests, 13 Python test files, 2 frontend tests
- CI: Python, frontend, Rust (+ ONNX on Ubuntu), Windows `cargo test`, feature parity
- Release: NSIS + DMG on `v*` tags

---

## Tier 0 — Before beta

See [CODE_HEALTH_REVIEW.md](CODE_HEALTH_REVIEW.md) for the latest code review findings behind these items.

### Validation (no code)

- [ ] 60-min smoke test: capture → label → snapback → export → train → reload
- [ ] Tagged release dry run (`v0.2.x` installer on Windows)
- [ ] Compare heuristic vs ONNX on the same session; note which you trust

### Permissions & capture

| Task | Files | Issue |
|------|-------|-------|
| [ ] Fix macOS `probe_capture` | `capture/permissions.rs:128-131` | Checks active window, not `rdev` |
| [ ] Windows/Linux probe | `permissions.rs:133-136` | Always returns `true` |
| [ ] Capture restart lifecycle | `state.rs`, `capture/thread.rs` | Old capture threads are not stopped before respawn |
| [ ] Probe vs capture-thread mismatch | `state.rs`, `App.tsx` | Probe OK but capture died |
| [ ] Hotkey registration failures in UI | `label_shortcuts.rs`, `api.ts`, `App.tsx` | Only logged today |

### ONNX behavior

| Task | Files | Issue |
|------|-------|-------|
| [ ] Decide ONNX + heuristic hybrid | `engine/classifier.rs:222-234` | Heuristic rules override ONNX `focus_state` |
| [ ] Align eval with production | `classifier_eval.rs`, `ml/classifier_quality.py` | CV metrics may not match runtime |
| [ ] Windows ONNX dev setup | `BENCHMARK_RESULTS.md`, `DEPLOYMENT.md` | MSVC + `ort` linker pain |
| [ ] Training deploy false success | `training_deploy.rs:212-217`, `App.tsx` | "Success" when ONNX export skipped |
| [ ] Single ACTIVE session invariant | `storage/mod.rs`, `commands.rs` | Multiple ACTIVE sessions can exist |

### CI & release

| Task | Files | Issue |
|------|-------|-------|
| [ ] Windows CI with ONNX | `ci.yml` `rust-windows` | No `--features onnx` |
| [ ] Tauri build in CI | `ci.yml` | Never runs `tauri build` |
| [ ] Python CI with ML deps | `ci.yml` | No xgboost/onnxmltools |
| [ ] Classifier quality gate | `tools/benchmark_classifier_quality.py` | No regression thresholds |
| [ ] Feature parity on Windows (optional) | `ci.yml` | Ubuntu only |

---

## Tier 1 — Polish

**Training & model**

- [ ] Clear errors for missing Python deps (`training_deploy.rs`, `App.tsx`)
- [ ] Model info in health UI (path, backend, train time, CV metrics)
- [ ] Copy trained model to `app_data_dir/model.onnx` after train
- [ ] Fail fast on majority-classifier stub (`ml/export_onnx.py`)
- [ ] Short guide: min sessions/labels, when to retrain

**Permissions**

- [ ] First-run permission wizard
- [ ] Separate "capture alive" vs "permissions OK" in health
- [ ] Wayland warning before first session (Linux)

**App rules**

- [ ] UI copy: "Block" affects scoring only (`App.tsx:882-951`)
- [ ] Rule preview before save

**Snapback**

- [ ] Surface overlay creation errors (`snapback/overlay.rs:19-31`)
- [ ] Type `onSnapback` payload (`api.ts:365-366`)
- [ ] Event-driven timeline refresh (30s poll today)

**Data quality**

- [ ] CSV-safe feature export (`storage/mod.rs`)
- [x] Test: no DB writes without active session (`state.rs`, `storage/mod.rs`)
- [ ] More `FeatureExtractor` tests (idle, mouse, session boundaries)
- [ ] Pre-export summary in UI

---

## Tier 2 — ML pipeline

- [ ] Decide what to do with `ml/labeling.py` (stub; real labels live in SQLite via Rust)
- [ ] Label source parity in export (hotkey / survey / auto)
- [ ] Minimum dataset checks in `pipeline_cli.py`
- [ ] ONNX numbers in `BENCHMARK_RESULTS.md`
- [ ] Align soak duration: `BENCHMARKS.md` says 3600s, results are 60s
- [ ] `--classifier-eval` in CI
- [ ] Document when synthetic vs real labeled data is enough
- [ ] Mark or remove legacy C++ ML path (`event_log_reader.py`, etc.)

---

## Tier 3 — Tests

**Has tests:** `classifier.rs`, `app_context.rs`, `storage/mod.rs`, `features.rs`, `tracker.rs`, `goal_alignment.rs`, `training_deploy.rs`, `onnx_model.rs`, `parity.rs`, `capture/thread.rs`, `title_parser.rs`, `classifier_eval.rs`

**No tests yet:** `commands.rs`, `state.rs`, `focus_modes.rs`, `permissions.rs`, `label_shortcuts.rs`, `tray.rs`, `overlay.rs`, `bench.rs`

**Worth adding first:**

- [ ] `focus_modes.rs` — hyperfocus thresholds
- [ ] Training false-success branch
- [ ] One ACTIVE session invariant
- [ ] CSV escaping in feature export
- [x] Session-gated persistence (`storage/mod.rs`, `state.rs`)
- [ ] `FeatureExtractor` edge cases
- [ ] Command harness for session start/stop
- [ ] `permissions.rs` platform messages
- [ ] ONNX override tests (after Tier 0 decision)
- [ ] `api.ts` mapper tests
- [ ] E2E (later)

---

## Tier 4 — Documentation

Do this when stale docs slow you down — not before the smoke test.

| Doc | Status | Action |
|-----|--------|--------|
| [x] `docs/README.md` | Added | Doc index |
| [x] `docs/ROADMAP.md` | Updated | Index + shipped history |
| [x] `PROGRESS.md` | Updated | July journal |
| [x] `ARCHITECTURE.md` | Legacy C++ | Banner + v0.2 pointer |
| [x] `TECHNICAL_DESIGN_DOCUMENT.md` | Legacy vision | Banner |
| [x] `SCHEMAS.md` | PostgreSQL + TODOs | Banner; point to `storage/mod.rs` |
| [x] `CONTEXT_RECOVERY_DESIGN.md` | C++ checkboxes | Rust status table + updated roadmap |
| [x] `METRICS.md` | C++ benchmark.exe | Rewritten; points to `bench.rs` |
| [x] `BENCHMARKS.md` | 3600s vs 60s soak | Default 60s; note on longer runs |
| [x] `CONCEPTS.md` | C++ examples | Banner |

---

## Tier 5 — Later

- App rules "block" as real intervention (scoring boost only today)
- Analytics / charts
- LSTM / 30–60s lookahead (~1 Hz reactive engine for now)
- Linux release packaging
- Wayland capture
- VS Code extension / browser domain tracking
- Model versioning

---

## Tech debt

| Issue | Where | Notes |
|-------|-------|-------|
| Events dropped on full channel | `capture/thread.rs` | `let _ = event_tx.send(...)` |
| Save failures only warned | `state.rs:256-300` | No user feedback |
| Mouse coords zero on key events | `capture/thread.rs:88-96` | Low impact |
| 1 Hz prediction | `state.rs:222` | Intentional for now |
| Shortcut capability unclear | `capabilities/default.json` | Verify clean install |

---

## Shipped (don't redo)

- Session ↔ `FeatureExtractor` reset
- No DB writes outside active sessions
- ONNX export → reload
- CI + release on tags
- Hotkeys, tray, in-app training deploy
- Context timeline, snapback overlay, permissions UX
- Feature parity CI; CV in `train_baseline`
