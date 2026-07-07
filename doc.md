## Snapback — work tracker

**Session checklist only.** Keep this short — update at the start and end of each work session.

| Doc | Use for |
|-----|---------|
| **This file** | What am I doing *right now*? |
| [docs/BACKLOG.md](docs/BACKLOG.md) | Full prioritized backlog (tiers, files, epics) |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Quick index + shipped history |

### Now (Tier 0 — ship confidence)
- [ ] **60-min smoke test** — capture → label → export → train → reload ONNX
- [ ] **Tagged release dry run** — `v0.2.x` installer on Windows
- [ ] **ONNX policy** — decide if heuristic overrides after ONNX are intentional (`classifier.rs:229-234`)

### Next (Tier 1 — polish)
- [ ] Session-gated persistence regression test (`state.rs`, `storage/mod.rs`)
- [ ] Training deploy: surface ONNX-skip + missing Python deps (`training_deploy.rs`, `App.tsx`)
- [ ] App rules UI: “Block affects scoring only” (`App.tsx`)

### Later
- [ ] Doc reconciliation ([BACKLOG Tier 4](docs/BACKLOG.md#tier-4--documentation-reconciliation))
- [ ] Linux distro packaging smoke test

---

### Done (shipped on `master`)
- [x] Core loop: capture → features → classifier → SQLite → React → snapback overlay
- [x] Session reset + no idle-row DB pollution
- [x] ONNX loop + in-app training deploy (export → train → reload)
- [x] CI hardening + release workflow (Windows/macOS on tag)
- [x] Global hotkey labeling, tray icon, context timeline
- [x] Feature parity CI, ONNX integration tests, real CV in training

*Full shipped list and file paths: [docs/BACKLOG.md#shipped-do-not-re-do](docs/BACKLOG.md#shipped-do-not-re-do)*
