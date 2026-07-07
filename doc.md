# Snapback — session tracker

Update this at the start and end of each work session. Keep it short.

**Full backlog:** [docs/BACKLOG.md](docs/BACKLOG.md)  
**Shipped history:** [docs/ROADMAP.md](docs/ROADMAP.md)

## Now

- [ ] **60-min smoke test** — capture → label → export → train → reload ONNX
- [ ] **Tagged release dry run** — `v0.2.x` installer on Windows
- [ ] **ONNX policy** — are heuristic overrides after ONNX intentional? (`classifier.rs:229-234`)

## Next

- [ ] Session-gated persistence regression test (`state.rs`, `storage/mod.rs`)
- [ ] Training deploy: show ONNX-skip + missing Python deps (`training_deploy.rs`, `App.tsx`)
- [ ] App rules UI: clarify that "Block" only affects scoring (`App.tsx`)

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
- [x] Feature parity CI, ONNX tests, real CV in training
