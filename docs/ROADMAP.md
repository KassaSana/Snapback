# Roadmap

Detail lives in [BACKLOG.md](BACKLOG.md). Session work in [`doc.md`](../doc.md).

Alpha works end-to-end. What's left: smoke test, permissions honesty, ONNX policy, tests, doc cleanup.

## Tiers (summary)

| Tier | Focus |
|------|-------|
| 0 | Smoke test, release dry run, permissions, ONNX policy, CI gaps |
| 1 | Training UX, app rules copy, overlay errors, regression tests |
| 2 | `labeling.py`, benchmarks, legacy ML cleanup |
| 3 | `focus_modes`, commands, `api.ts` tests |
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
| Global hotkeys | Ctrl+Shift+1–4 |
| Tray | show/hide/quit |
| Feature parity CI | `ml.feature_parity_cli` |
| Real CV | `time_series_splits` in `train_baseline` |

21 commands in `src-tauri/src/lib.rs`.

## Archive

Session journal: [archive/PROGRESS_full.md](archive/PROGRESS_full.md)
