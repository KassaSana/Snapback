# Snapback backlog

Last reviewed: 2026-07-08. Alpha is usable; main gaps are confidence (smoke test, permissions, ONNX behavior), polish, tests, and CI follow-through.

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
- ~55 Rust tests, 13 Python test files, 6 frontend test files
- CI: Python (+ ML deps), frontend, Rust (+ ONNX on Ubuntu/Windows), Tauri build smoke, feature parity, dependency audits
- Release: NSIS + DMG on `v*` tags

---

## Tier 0 — Before beta

See [CODE_HEALTH_REVIEW.md](CODE_HEALTH_REVIEW.md) for the latest code review findings behind these items.

### Validation (no code)

- [ ] 60-min smoke test: [SMOKE_TEST.md](SMOKE_TEST.md)
- [ ] Tagged release dry run (`v0.2.x` installer on Windows)
- [ ] Compare heuristic vs ONNX on the same session; note which you trust

### Permissions & capture

| Task | Files | Issue |
|------|-------|-------|
| [x] Fix macOS `probe_capture` | `capture/permissions.rs` | Uses macOS Accessibility + Input Monitoring preflight APIs instead of active-window inference |
| [x] Windows probe heuristic | `capture/permissions.rs` | Windows now preflights with a real `SetWindowsHookExW` install/uninstall probe |
| [x] Capture restart lifecycle | `state.rs`, `capture/thread.rs` | Capture now owns stop/join handles before respawn |
| [x] Probe vs capture-thread mismatch | `state.rs`, `useHealth.ts`, `AppHeader.tsx` | App health now reflects offline/degraded/idle runtime state instead of collapsing listener-down states to online |
| [x] Hotkey registration failures in UI | `label_shortcuts.rs`, `api.ts`, `App.tsx` | Hotkey failures now surface in the UI with warning styling |

### ONNX behavior

| Task | Files | Issue |
|------|-------|-------|
| [x] Decide ONNX + heuristic hybrid | `engine/classifier.rs` | ONNX sets scores; guardrails override `focus_state` (documented + tested) |
| [ ] Align eval with production | `classifier_eval.rs`, `ml/classifier_quality.py` | CV metrics may not match runtime |
| [x] Windows ONNX dev setup | `tools/dev-onnx.mjs`, `DEPLOYMENT.md` | `load-dynamic` + pip `onnxruntime.dll` via `ORT_DYLIB_PATH` |
| [x] Training deploy false success | `training_deploy.rs`, `App.tsx` | Result now distinguishes `trainingSucceeded` from `deployReady` |
| [x] Single ACTIVE session invariant | `storage/mod.rs`, `commands.rs` | Starting a new session now completes any prior ACTIVE session |

### CI & release

| Task | Files | Issue |
|------|-------|-------|
| [x] Windows CI with ONNX | `ci.yml` `rust-windows` | Windows now checks/tests `--features onnx` with `ORT_DYLIB_PATH` |
| [x] Tauri build in CI | `ci.yml` | Ubuntu now runs a non-bundled `tauri build` smoke check |
| [x] Python CI with ML deps | `ci.yml` | Training deps now install in CI before Python tests |
| [x] Classifier quality gate | `tools/benchmark_classifier_quality.py`, `ci.yml` | CI now enforces conservative CV floors + recall lift over heuristic |
| [ ] Feature parity on Windows (optional) | `ci.yml` | Ubuntu only |

---

## Tier 1 — Polish

**Training & model**

- [x] Clear errors for missing Python deps (`training_deploy.rs`, `App.tsx`)
- [ ] Model info in health UI (path, backend shown; train time/CV metrics later)
- [x] Copy trained model to `app_data_dir/model.onnx` after train
- [x] Fail fast on majority-classifier stub (`ml/export_onnx.py`) — `is_majority_stub` blocks ONNX export, `pipeline_cli.py` exits 2, `training_deploy.rs` surfaces a friendly message; tested in `ml/tests/test_export_onnx.py`
- [ ] Short guide: min sessions/labels, when to retrain

**Permissions**

- [ ] First-run permission wizard
- [x] Separate "capture alive" vs "permissions OK" in health
- [ ] Wayland warning before first session (Linux)

**App rules**

- [x] UI copy: "Block" affects scoring only (`App.tsx:882-951`)
- [x] Rule preview before save

**Snapback**

- [x] Surface overlay creation errors (`snapback/overlay.rs`, `state.rs`, frontend health/event handling)
- [x] Type `onSnapback` payload (`api.ts:365-366`)
- [x] Event-driven timeline refresh (30s poll today)

**Data quality**

- [x] CSV-safe feature export (`storage/mod.rs`)
- [x] Test: no DB writes without active session (`state.rs`, `storage/mod.rs`)
- [x] More `FeatureExtractor` tests (idle, mouse, session boundaries)
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

**Has tests:** `classifier.rs`, `app_context.rs`, `storage/mod.rs`, `state.rs`, `features.rs`, `tracker.rs`, `goal_alignment.rs`, `training_deploy.rs`, `onnx_model.rs`, `parity.rs`, `capture/thread.rs`, `title_parser.rs`, `classifier_eval.rs`

**No tests yet:** `label_shortcuts.rs`, `tray.rs`, `overlay.rs`, `bench.rs`

**Worth adding first:**

- [x] `focus_modes.rs` — hyperfocus thresholds
- [x] Training false-success branch
- [x] One ACTIVE session invariant
- [x] CSV escaping in feature export
- [x] Session-gated persistence (`storage/mod.rs`, `state.rs`)
- [x] `FeatureExtractor` edge cases — 30s-window trim vs. 5min retention, longest-active-stretch gap calc, cold-start `extract_features`
- [x] Command-boundary validation helpers
- [x] `permissions.rs` message/probe honesty helpers
- [x] ONNX override policy tests (`engine/classifier.rs`)
- [x] `api.ts` mapper tests
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
| ~~Events dropped on full channel~~ | `capture/thread.rs` | Bounded (`sync_channel`, cap 4096) + counted; surfaced via `HealthStatus.capture_events_dropped` and `PermissionsCard` |
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
- Frontend modularization: `App.tsx` split into hooks/components with visible error states and helper tests
- Feature parity CI; CV in `train_baseline`
