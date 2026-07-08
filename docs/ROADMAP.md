# Roadmap

Detail lives in [BACKLOG.md](BACKLOG.md). Session work in [`doc.md`](../doc.md).

Alpha works end-to-end. What's left: smoke test, permissions honesty, CI follow-through, focused tests, and final doc cleanup.

## Tiers (summary)

| Tier | Focus |
|------|-------|
| 0 | Smoke test, release dry run, permissions, CI gaps |
| 1 | Training UX, overlay errors, regression tests |
| 2 | `labeling.py`, benchmarks, legacy ML cleanup |
| 3 | `focus_modes`, commands, capture/permission tests |
| 4 | Legacy doc banners and reconciliation |
| 5 | Real blocking, LSTM, Linux packaging, analytics |

## Shipped (2026-06 → 2026-07)

| Item | Notes |
|------|-------|
| Tauri monolith | Replaced C++/Spring stack |
| Session ↔ feature extractor sync | `feature_session_epoch` |
| Session-gated persistence | No idle-row pollution |
| ONNX loop | export → `model.onnx` → reload |
| CI | frontend, Rust + ONNX, Windows, feature parity |
| Release CI | `release.yml` on `v*` tags |
| Training deploy | In-app export → train → reload |
| Correctness-first hardening | Training semantics, capture lifecycle, CSV export, command validation, dependency audits, permission honesty |
| CI hardening | Windows ONNX checks, Python ML deps, Tauri build smoke |
| Global hotkeys | Ctrl+Shift+1–4 |
| Tray | show/hide/quit |
| Feature parity CI | `ml.feature_parity_cli` |
| Real CV | `time_series_splits` in `train_baseline` |
| Frontend modularization | `App.tsx` split into hooks/components with visible error states and helper tests |

21 commands in `src-tauri/src/lib.rs`.

## Archive

Session journal: [archive/PROGRESS_full.md](archive/PROGRESS_full.md)
