# Snapback Roadmap

> **Planning detail moved to [`docs/BACKLOG.md`](BACKLOG.md)** (master backlog, last audit 2026-07).  
> **Session tracker:** [`doc.md`](../doc.md) — update that file every work session.

**Current state:** Usable alpha. Core loop works end-to-end. Remaining work is ship confidence (smoke test, permissions, ONNX policy), polish, tests, and doc cleanup.

---

## Quick links

| Tier | Focus | Start here |
|------|-------|------------|
| **0** | Ship confidence | 60-min smoke test, release dry run |
| **1** | Product polish | Training UX, app rules copy, overlay errors |
| **2** | ML pipeline | `labeling.py` fate, benchmarks, legacy cleanup |
| **3** | Tests | `focus_modes`, session persistence, `api.ts` |
| **4** | Docs | SCHEMAS, ARCHITECTURE, PROGRESS reconciliation |
| **5** | Vision | Real blocking, LSTM, Linux packaging, analytics |

See [BACKLOG.md](BACKLOG.md) for every task, file path, and checkbox.

---

## Shipped history (2026-06 → 2026-07)

These were the original P0/P1 items — all done in code; see BACKLOG “Shipped” section for evidence.

| Item | Status |
|------|--------|
| Session ↔ `FeatureExtractor` reset | **Done** — `feature_session_epoch` in `AppState` |
| Stop persisting idle rows | **Done** — engine gates on `get_active_session()` |
| Close ONNX loop | **Done** — `pipeline_cli` → `model.onnx` → `reload_classifier_model` |
| CI hardening | **Done** — frontend build, Rust + ONNX, Windows job |
| Release CI | **Done** — `release.yml` on `v*` tags |
| Training deploy UX | **Done** — in-app export → train → reload panel |
| Global hotkey labeling | **Done** — Ctrl+Shift+1–4 |
| Tray icon | **Done** — show/hide/quit |
| Feature parity in CI | **Done** — `ml.feature_parity_cli` |
| Real CV in training | **Done** — `time_series_splits` in `train_baseline` |

**Commands:** 21 registered in `src-tauri/src/lib.rs` (not 18).

---

## Archive

The detailed P0–P5 sections from the July codebase review live in [`docs/BACKLOG.md`](BACKLOG.md). Older session journal: [`docs/archive/PROGRESS_full.md`](archive/PROGRESS_full.md).
