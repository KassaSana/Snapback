# 60-minute smoke test

**Goal:** Prove the full loop works on your machine without notes.

```
capture → features → classifier → UI
  → label → (optional snapback) → stop session → recap
  → export → train → reload ONNX → classifier shows ONNX
```

**When:** Before calling Snapback beta-ready. Re-run after major changes to capture, classifier, storage, or training.

**Time:** ~60 minutes (45 min session + 15 min ML loop). You can split across two sessions; finish Part B in the same day if possible.

**App data (Windows):** `%APPDATA%\com.snapback.app\`  
**Database:** `focoflow.db`  
**Training export:** `exports\training\` (`features.csv`, `labels.csv`)  
**Model:** `model.onnx` or `exports\training\model.onnx`

---

## Before you start

- [ ] Close other focus trackers / heavy CPU apps
- [ ] Python training deps installed: `pip install -r ml/requirements-train.txt` (includes `onnxruntime` for Windows ONNX dev)
- [ ] Repo path ready (your Snapback clone) for in-app training
- [ ] Terminal for dev build: `npm run tauri:dev` (from repo root; sets `ORT_DYLIB_PATH` on Windows)
- [ ] Optional: [DB Browser for SQLite](https://sqlitebrowser.org/) to spot-check rows

**Record:**

| Field | Value |
|-------|-------|
| Date | |
| OS | Windows 10/11 |
| Build | dev (`tauri:dev`) / installer (version: ___) |
| Classifier at start | Heuristic / ONNX |

---

## Automated smoke harness (~5 min)

Run this first when you want the lowest-effort confidence pass:

```bash
cargo run --features onnx --manifest-path src-tauri/Cargo.toml -- --smoke
```

On Windows, the harness auto-resolves `onnxruntime.dll` from `pip install onnxruntime` (same as `npm run tauri:dev`). If ONNX reload hangs or fails, set `ORT_DYLIB_PATH` explicitly to your `onnxruntime.dll` path.

What it verifies for you:

- session-gated persistence stays blocked outside active sessions
- seeded sessions export to `features.csv` and `labels.csv`
- in-app training deploy path produces `model.onnx`
- ONNX reload succeeds and runtime classifier status flips to `onnx`

What it does **not** verify:

- real capture health on your desktop
- app/window switching behavior
- global hotkeys from another focused app
- snapback overlay UX
- subjective trust in heuristic vs ONNX behavior

Treat the harness as the automated contract check. Use the manual live pass below for the true OS- and UX-dependent slice.

---

## Part A — Manual live pass (~45 min)

### A1. Launch & health (5 min)

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| A1.1 | App opens; header shows **App: online** | [ ] | |
| A1.2 | **Capture** shows running (not `capture_failed`) | [ ] | If probe fails, note permission message |
| A1.3 | **Classifier** shows `Heuristic` (unless you already have a model) | [ ] | |
| A1.4 | Live **Focus score** / **State** update every ~1s (no session yet) | [ ] | |

**Fail if:** capture never starts, UI frozen, or predictions never update.

---

### A2. No-session DB gate (2 min)

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| A2.1 | **Do not** start a session; wait 30s | [ ] | |
| A2.2 | Open `focoflow.db` → `predictions` table row count **unchanged** | [ ] | Count before: ___ after: ___ |
| A2.3 | `feature_snapshots` row count **unchanged** | [ ] | |

**Fail if:** rows appear with no active session (regression on session-gated persistence).

---

### A3. Start session & capture (5 min)

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| A3.1 | Set goal: e.g. `Smoke test session` | [ ] | |
| A3.2 | Focus mode: **normal** | [ ] | |
| A3.3 | Click **Start session** — Session ID appears | [ ] | |
| A3.4 | Switch between 2–3 real apps (IDE, browser, Slack/email) | [ ] | |
| A3.5 | **Signals** card text changes (switches, idle, etc.) | [ ] | |
| A3.6 | **Context timeline** shows window changes | [ ] | |

---

### A4. Classifier behavior (10 min)

Do short blocks of each behavior. Watch **State** and **Distraction risk**.

| # | Activity (~2 min each) | Expected state (approx) | Pass? |
|---|------------------------|---------------------------|-------|
| A4.1 | Steady typing in IDE, one file | `PRODUCTIVE` or `DEEP_FOCUS` | [ ] |
| A4.2 | Rapid tab/window switching | `DISTRACTED` or high risk | [ ] |
| A4.3 | Browser research, many tab title changes | `PSEUDO_PRODUCTIVE` or drift signal | [ ] |
| A4.4 | YouTube / entertainment window | `DISTRACTED` | [ ] |

**Optional ONNX A/B (after Part B):** repeat A4.1–A4.4 and note which backend you trust.

---

### A5. Labeling (5 min)

Label at least **4 moments** — mix UI buttons and hotkeys.

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| A5.1 | UI: tap **Deep**, **Focused**, **Drift**, **Distracted** once each | [ ] | Status line confirms save |
| A5.2 | Hotkey: `Ctrl+Shift+1` (deep) from another app | [ ] | |
| A5.3 | Hotkey: `Ctrl+Shift+4` (distracted) | [ ] | |
| A5.4 | DB: `labels` table has ≥ 4 new rows for this session | [ ] | |

**Fail if:** hotkeys silent-fail (check tray / no status); labels missing in DB.

---

### A6. Snapback (optional, 5 min)

Only if you can naturally trigger distraction for 30+ seconds.

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| A6.1 | Stay on distraction (e.g. social/video) long enough | [ ] | |
| A6.2 | Overlay appears with context summary | [ ] | |
| A6.3 | Dismiss works; app returns to normal | [ ] | |

Skip if not triggered — note “snapback not exercised”.

---

### A7. Stop session (5 min)

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| A7.1 | Click **Stop session** | [ ] | |
| A7.2 | **Session recap** appears (duration, states, etc.) | [ ] | |
| A7.3 | Session survey / auto-label prompt (accept or adjust) | [ ] | |
| A7.4 | Wait 30s after stop — **no new** `predictions` / `feature_snapshots` rows | [ ] | |
| A7.5 | UI may still show live predictions — that's OK | [ ] | |

---

## Part B — ML loop (~15 min)

### B1. Export

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| B1.1 | Focus Feedback → **Export training data** | [ ] | |
| B1.2 | Message shows feature + label counts > 0 | [ ] | features: ___ labels: ___ |
| B1.3 | Files exist: `exports\training\features.csv`, `labels.csv` | [ ] | |
| B1.4 | Deploy panel step **Export** shows done | [ ] | |

**Fail if:** zero features (session too short) or zero labels (forgot A5).

---

### B2. Train

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| B2.1 | Set **repo path** to your Snapback clone (once) | [ ] | |
| B2.2 | Click **Train from export** | [ ] | |
| B2.3 | Success message — **not** a warning-only “ONNX skipped” | [ ] | |
| B2.4 | `exports\training\model.onnx` exists | [ ] | |
| B2.5 | `metrics.json` exists (note accuracy if shown) | [ ] | |

**If ONNX skipped:** install `xgboost` + `onnxmltools`, re-export, re-train. Do **not** count smoke test as passed.

---

### B3. Reload & verify ONNX

| # | Step | Pass? | Notes |
|---|------|-------|-------|
| B3.1 | **Reload model** (or auto-reload after train) | [ ] | |
| B3.2 | Header **Classifier** shows **ONNX** | [ ] | |
| B3.3 | Predictions still update every ~1s | [ ] | |
| B3.4 | Start a **short** second session; label 1–2 times; export again — counts grew | [ ] | |

---

## Part C — Sign-off

| Question | Answer |
|----------|--------|
| Full loop completed without workarounds? | Yes / No |
| Biggest friction point | |
| Heuristic vs ONNX (if compared) | Trust heuristic / trust ONNX / mixed |
| Blockers for beta | |

**Pass criteria (all required):**

1. `cargo run --features onnx --manifest-path src-tauri/Cargo.toml -- --smoke` exits successfully  
2. Session start/stop works; recap appears  
3. Predictions + features persist **only** during active session  
4. ≥ 4 labels saved  
5. Export → train → `model.onnx` → classifier shows **ONNX**  
6. No silent failures you had to discover via logs  

When passed, check off in [`doc.md`](../doc.md):

```markdown
- [x] **60-min smoke test** — capture → label → export → train → reload ONNX
```

---

## Quick troubleshooting

| Symptom | Check |
|---------|--------|
| Capture failed | Windows privacy → input capture; restart app |
| `LNK2019` / `__std_find_first_of` on `tauri:dev` | MSVC + static `ort` mismatch — repo now uses `load-dynamic`; run `cargo clean` then `npm run tauri:dev` again. Fallback: `npm run tauri:dev:heuristic` (no in-app ONNX) |
| Predictions frozen | Capture thread died — Signals card + header |
| Export 0 features | Session was active long enough? (~1 row/sec while running) |
| Export 0 labels | Complete A5 |
| Train “success” but no ONNX | Python deps; read deploy warning text |
| Reload still Heuristic | `model.onnx` path under app data; rebuild with `--features onnx` |
| Hotkeys dead | Another app grabbed shortcuts; restart Snapback |

---

## Related docs

- [DEPLOYMENT.md](DEPLOYMENT.md) — build, ONNX loop, Python deps  
- [BACKLOG.md](BACKLOG.md) — Tier 0 ship-confidence items  
- [CONCEPTS.md](CONCEPTS.md) — features, labels, supervised learning  
