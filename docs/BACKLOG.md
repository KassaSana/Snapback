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
| [x] Align eval with production | `classifier_eval.rs`, `ml/classifier_quality.py`, `tools/benchmark_classifier_quality.py` | Rust `--classifier-eval` is the guardrail-aware production evaluator; Python onnxruntime evals are labeled raw-model (`production_aligned=False`), and the quality gate warns loudly when it judged raw-model numbers instead of silently passing them off as runtime |
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
| [x] Feature parity on Windows (optional) | `ci.yml` | `feature-parity-windows` job runs `ml.feature_parity_cli` on windows-latest |

---

## Tier 1 — Polish

**Training & model**

- [x] Clear errors for missing Python deps (`training_deploy.rs`, `App.tsx`)
- [x] Model info in health UI (path, backend shown; train time/CV metrics later) — `AppHeader.tsx` shows backend, model file, ONNX runtime status, and CV quality label
- [x] Copy trained model to `app_data_dir/model.onnx` after train
- [x] Fail fast on majority-classifier stub (`ml/export_onnx.py`) — `is_majority_stub` blocks ONNX export, `pipeline_cli.py` exits 2, `training_deploy.rs` surfaces a friendly message; tested in `ml/tests/test_export_onnx.py`
- [x] Short guide: min sessions/labels, when to retrain — [TRAINING_GUIDE.md](TRAINING_GUIDE.md)

**Permissions**

- [x] First-run permission wizard — modal on first launch guides capture-permission setup, auto-dismisses once capture works, remembers acknowledgement in localStorage (`PermissionWizard.tsx`, `permissionWizardState.ts`, `App.tsx`)
- [x] Separate "capture alive" vs "permissions OK" in health
- [x] Wayland warning before first session (Linux) — probe emits a Wayland-only hard-blocker that wins over all other permission messages (`capture/permissions.rs`), and session start surfaces a capture-not-ready warning (`healthHints.ts` `sessionStartCaptureWarning`, `useSession.ts`)

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
- [x] Pre-export summary in UI — `TrainingDeployCard` shows exported feature/label counts and label balance before training

---

## Tier 2 — ML pipeline

- [x] Decide what to do with `ml/labeling.py` — kept, documented: enums are the production contract, `Labeler`/`SessionMetadata` are an unused pre-SQLite stub used only by its own test
- [x] Label source parity in export (hotkey / survey / auto) — Rust writes lowercase (`hotkey`/`manual`/`survey`/`auto`), `ml/dataset_builder._parse_source` upper-cases before enum lookup, round-trip verified
- [x] Minimum dataset checks in `pipeline_cli.py` — `training_pipeline.validate_training_dataset` (`MIN_TRAINING_SAMPLES`, per-class minimum) raises `ValueError`, caught in `run_pipeline` as "Training blocked: …" with exit code 1
- [x] ONNX numbers in `BENCHMARK_RESULTS.md` — quality (§4: accuracy/precision@10%/recall vs heuristic + XGBoost) and latency (§5: p50/p95/p99) recorded
- [x] Align soak duration: `BENCHMARKS.md` says 3600s, results are 60s — already reconciled (default 60s documented, note on running longer)
- [x] `--classifier-eval` in CI — `ci.yml` `classifier-eval` job runs both heuristic and ONNX backends
- [x] Document when synthetic vs real labeled data is enough — [TRAINING_GUIDE.md § Synthetic vs. real labeled data](TRAINING_GUIDE.md#synthetic-vs-real-labeled-data)
- [x] Mark or remove legacy C++ ML path (`event_log_reader.py`, etc.) — module-level banners added to `event_log_reader.py` and `event_schema.py` explaining they're unused by the live SQLite-based pipeline

---

## Tier 3 — Tests

**Has tests:** `classifier.rs`, `app_context.rs`, `storage/mod.rs`, `state.rs`, `features.rs`, `tracker.rs`, `goal_alignment.rs`, `training_deploy.rs`, `onnx_model.rs`, `parity.rs`, `capture/thread.rs`, `title_parser.rs`, `classifier_eval.rs`, `label_shortcuts.rs`, `overlay.rs` (pure geometry math extracted into `top_right_position`), `bench.rs` (CLI arg parsing)

**No tests yet:** `tray.rs` — thin Tauri glue (menu/window show-hide-toggle) with no pure logic to extract; would need a real window/tray runtime to exercise meaningfully

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
- [x] Component/integration tests — Vitest + jsdom + React Testing Library; renders the full app against a mocked Tauri boundary and asserts wizard/health (`App.test.tsx`), session start/stop (`sessionFlow.test.tsx`), and training/deploy incl. the H4 "trained but not deploy-ready" warning + reload gating (`trainingDeploy.test.tsx`) (`vite.config.ts` `test`, `tests/setup.ts`)
- [ ] Full WebDriver E2E via `tauri-driver` (later) — drives the built binary in CI

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
| ~~Save failures only warned~~ | `state.rs` | Already surfaced: `persist_session_tick` routes failures through `record_persistence_failure` → `HealthStatus.persistence_failure_reason` → UI |
| ~~Mouse coords zero on key events~~ | `capture/thread.rs` | Fixed: key/click events now read `read_last_mouse`; only window-change poll events send 0 (correct — not pointer events) |
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
