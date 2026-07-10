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

- [x] DB retention policy: `Storage::open()` prunes predictions + context snapshots older than 90d (`DEFAULT_RETENTION_DAYS`), keeps training data/labels (`storage/mod.rs` `prune_runtime_data`)
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
