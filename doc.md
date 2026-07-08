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

- [x] Training false-success: don't treat skipped ONNX export as deploy-ready (`training_deploy.rs`, `App.tsx`)
- [x] Enforce one ACTIVE session (`storage/mod.rs`, `commands.rs`)
- [x] Capture restart lifecycle: stop/join old workers before respawn (`state.rs`, `capture/thread.rs`)
- [x] CSV-safe export + regression coverage (`storage/mod.rs`)
- [x] Command-boundary validation + focused tests (`commands.rs`)
- [x] Permission honesty + capture-alive health distinction (`capture/permissions.rs`, frontend health UI)
- [x] CI gap fill: Windows ONNX, Python ML deps, Tauri build smoke (`.github/workflows/ci.yml`)
- [x] Focused backend/frontend health tests (`permissions.rs`, `healthHints.ts`)
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
