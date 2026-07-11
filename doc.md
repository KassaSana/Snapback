# Snapback — session tracker

Update this at the start and end of each work session. Keep it short.

**Full backlog:** [docs/BACKLOG.md](docs/BACKLOG.md)  
**Code health queue:** [docs/CODE_HEALTH_REVIEW.md](docs/CODE_HEALTH_REVIEW.md)  
**Shipped history:** [docs/ROADMAP.md](docs/ROADMAP.md)

## Now

- [ ] **60-min smoke test** — [checklist](docs/SMOKE_TEST.md): capture → label → export → train → reload ONNX
- [ ] **Tagged release dry run** — `v0.2.x` installer on Windows
- [x] **ONNX policy** — hybrid: ONNX sets scores; guardrails override `focus_state` (`classifier.rs`)

## Next

- [x] Frontend error/recovery tests (TEST_BACKLOG #3): session-start failure → visible alert + dismiss, training hard-failure → message + no reload, capture-unavailable warning before session start (`errorRecovery.test.tsx`, `trainingDeploy.test.tsx`)
- [x] Smoke harness assertions (TEST_BACKLOG #2): extracted `check_export_thresholds` + `check_train_outcome` pure diagnosable checks, unit-tested without ONNX (`smoke.rs`)
- [x] Command/session lifecycle tests (TEST_BACKLOG #1): too-long goal rejection + state untouched, stop-unknown-session error, label notes/blank validation, cross-session label isolation (`commands.rs` `*_command_core_*`)
- [x] Analytics / insights — Insights card (stat tiles + single-series SVG focus-trend bar chart) consuming `get_session_history`; backend `get_session_history` → `SessionSummary[]` + `storage.list_recent_sessions` (`InsightsCard.tsx`, `insightsMetrics.ts`, `useInsights.ts`, `commands.rs`, `storage/mod.rs`)

- [x] Component/integration test layer: Vitest + jsdom + RTL against a mocked Tauri boundary — wizard/health (`App.test.tsx`), session start/stop (`sessionFlow.test.tsx`), training/deploy incl. "trained but not deploy-ready" warning + reload gating (`trainingDeploy.test.tsx`), app-rules add/delete (`appRulesFlow.test.tsx`)
- [x] Emit failures no longer silent: Tauri event emits routed through `events::emit_or_log` (warns on failure) instead of `let _ = app.emit(...)` (`events.rs`, `state.rs`, `lib.rs`, `label_shortcuts.rs`)
- [x] Auto-recover permissions: poll health every 5s while capture isn't running so granting permission after launch is noticed without manual refresh (`healthPoll.ts`, `useAppEffects.ts`)
- [x] DB retention policy: `Storage::open()` prunes predictions + context snapshots older than 90d (`DEFAULT_RETENTION_DAYS`), then `VACUUM`s to reclaim disk after a large prune (`should_vacuum`, ≥500 rows); keeps training data/labels (`storage/mod.rs`)
- [x] First-run permission wizard: modal guides capture-permission setup on first launch, auto-dismisses once capture works, remembers via localStorage (`PermissionWizard.tsx`, `permissionWizardState.ts`)
- [x] Align classifier eval with production: Rust `--classifier-eval` is guardrail-aware; Python onnxruntime evals labeled raw-model (`production_aligned=False`); CI quality gate warns when judging raw-model numbers (`classifier_quality.py`, `tools/benchmark_classifier_quality.py`)
- [x] Windows capture probe honesty: real `SetWindowsHookExW` preflight (`capture/permissions.rs`)
- [x] Storage robustness: propagate dir-creation errors, log (don't silently drop) undecodable rows (`storage/mod.rs`)
- [x] Training subprocess timeout: bounded wait + kill instead of blocking forever (`training_deploy.rs`)
- [x] Bounded capture event channel + drop counting surfaced in health/UI (`capture/thread.rs`, `state.rs`, `PermissionsCard.tsx`)
- [x] Startup errors: propagate/log instead of panicking (`lib.rs`)
- [x] Fix stale `ml/tests/test_train_cli.py` fixtures broken by the new min-sample training guard from the prior commit (`ml/training_pipeline.py` `MIN_TRAINING_SAMPLES`)
- [x] Training false-success: don't treat skipped ONNX export as deploy-ready (`training_deploy.rs`, `App.tsx`)
- [x] Enforce one ACTIVE session (`storage/mod.rs`, `commands.rs`)
- [x] Capture restart lifecycle: stop/join old workers before respawn (`state.rs`, `capture/thread.rs`)
- [x] CSV-safe export + regression coverage (`storage/mod.rs`)
- [x] Command-boundary validation + focused tests (`commands.rs`)
- [x] Permission honesty + capture-alive health distinction (`capture/permissions.rs`, frontend health UI)
- [x] CI gap fill: Windows ONNX, Python ML deps, Tauri build smoke (`.github/workflows/ci.yml`)
- [x] Focused backend/frontend health tests (`permissions.rs`, `healthHints.ts`)
- [x] Classifier quality regression gate in CI (`tools/benchmark_classifier_quality.py`, `.github/workflows/ci.yml`)
- [x] Surface snapback overlay creation failures in the UI (`snapback/overlay.rs`, `state.rs`, action banner plumbing)
- [x] Copy trained ONNX model into app data after successful train (`training_deploy.rs`)
- [x] App rules UI: clarify that "Block" only affects scoring (`App.tsx`)
- [x] Session-gated persistence regression test (`state.rs`, `storage/mod.rs`)

## Later

- [ ] Doc cleanup for remaining legacy cross-refs ([BACKLOG § Tier 4](docs/BACKLOG.md#tier-4--documentation) — mostly done)
- [ ] Linux distro packaging smoke test

---

## Shipped

- [x] Core loop: capture → features → classifier → SQLite → React → snapback overlay
- [x] Session reset; no idle-row DB pollution
- [x] ONNX loop + in-app training deploy
- [x] CI + release workflow (Windows/macOS on tag)
- [x] Global hotkey labeling, tray, context timeline
- [x] Frontend cleanup: `App.tsx` split into hooks/components with clearer UI error feedback and helper tests
- [x] Feature parity CI, ONNX tests, real CV in training
