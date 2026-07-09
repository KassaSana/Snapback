# Code Health Review

Last reviewed: 2026-07-08

Scope: Rust/Tauri backend, React frontend, Python ML pipeline, and cross-language training/deploy path.

Overall grade: **B**. Snapback is a working alpha with a sound core loop. The biggest risks are not algorithmic; they are permission honesty, runtime validation, and smoke-test confidence.

Use this doc as the fix queue after the smoke test. The full planning backlog lives in [BACKLOG.md](BACKLOG.md).

---

## Quick read

| Area | Grade | Summary |
|------|-------|---------|
| Rust backend | B | Good architecture and tests in storage/classifier; weak capture lifecycle and session integrity |
| Frontend | B | Major cleanup landed: `App.tsx` is split into hooks/components with visible error states; remaining gaps are mostly integration coverage and smoke-test confidence |
| ML pipeline | B+ | Best-structured area; good unit tests, but optional dependency path is loose |
| Train/deploy integration | B | Train/deploy states are explicit; remaining work is UX/pipeline polish |
| Security | B | Reasonable for a local desktop app; training repo path is trusted code execution |
| Tests | C | Strong unit coverage in core modules; weak IPC, engine-loop, capture, and frontend coverage |

---

## Fix order

Start here. These are ordered for impact and learning value.

1. **60-minute smoke test**  
   No code first. Run capture → label → snapback → export → train → reload and write down exactly where the app feels unreliable.

2. **Permission honesty**  
   `probe_capture()` still does not prove the global input listener will stay healthy. Make the UI honest about what is confirmed versus inferred.

3. **Probe vs capture-alive mismatch**  
   The app should distinguish "permission probe passed" from "capture listener is alive" so refresh/recovery state is more trustworthy.

4. **CI follow-through**  
   Close the remaining Windows ONNX / build / ML dependency gaps so platform failures show up before the smoke test.

5. **Focused regression tests**  
   Add the next narrow tests in `permissions.rs`, `focus_modes.rs`, and command/session flows.

6. **60-minute smoke test**  
   Once the above is in place, run capture → label → snapback → export → train → reload and note any remaining UX or stability gaps.

---

## High-priority findings

### H1 — macOS permission probe can lie

**Files:** `src-tauri/src/capture/permissions.rs`

`probe_capture()` used to check `active_win_pos_rs::get_active_window()`, which only verified active-window access. That meant Accessibility could pass while Input Monitoring was still denied.

**Why it mattered:** the app could show a healthy permission state, then capture fail after launch.

**Status:** fixed on macOS. The app now preflights Accessibility with `AXIsProcessTrusted()` and Input Monitoring with `CGPreflightListenEventAccess()` instead of inferring both from active-window access.

---

### H2 — Capture restart leaked old threads

**Files:** `src-tauri/src/state.rs`, `src-tauri/src/capture/thread.rs`

`spawn_capture()` used to create a new event channel and start capture threads without stopping the old ones first. A permission refresh after failure could leave old pollers/listeners running and sending to dropped receivers.

**Why it mattered:** long-running app sessions could accumulate wasted threads and confusing duplicate behavior.

**Status:** fixed. Capture now returns a handle with cancellation/join ownership and `AppState` stops old workers before respawn.

---

### H3 — Multiple ACTIVE sessions were allowed

**Files:** `src-tauri/src/storage/mod.rs`, `src-tauri/src/commands.rs`

`start_session()` used to insert a new ACTIVE session without checking for an existing one. `get_active_session()` then returned only the newest.

**Why it mattered:** orphan ACTIVE sessions could accumulate. Predictions, labels, and recaps could attach to a different session than the user expected.

**Status:** fixed. Starting a new session now auto-completes the previous ACTIVE session, and storage tests cover the behavior.

---

### H4 — Training can "succeed" without deployable ONNX

**Files:** `src-tauri/src/training_deploy.rs`, `frontend/src/App.tsx`, `ml/pipeline_cli.py`, `ml/export_onnx.py`

Rust used to collapse "training ran" and "deploy ready" into one success state when ONNX export was skipped.

**Why it mattered:** users could think they trained and deployed a model when the app was still using the heuristic backend.

**Status:** fixed. The result now models `trainingSucceeded` and `deployReady` separately, and the frontend treats "trained but not deployable" as a warning state.

---

### H5 — Frontend errors are often swallowed

**Files:** `frontend/src/App.tsx`

`App.tsx` was roughly 1,000 lines and had many bare `catch {}` blocks. Session start/stop, refresh, training status, and listener paths could fail with little or no feedback.

**Why it mattered:** when something broke, the user saw stale UI instead of an actionable error.

**Status:** largely fixed. The app now uses visible error/warning states and `App.tsx` has been split into focused hooks/components. Remaining frontend work is mostly validation and selective integration coverage.

---

## Medium-priority findings

### M1 — ONNX behavior is not clearly defined

**Files:** `src-tauri/src/engine/classifier.rs`, `src-tauri/src/engine/classifier_eval.rs`

The heuristic probabilities are computed first. If ONNX is loaded, ONNX scores replace them. Then heuristic guardrails still override final `focus_state` for high risk, thrash, drift, and app block rules.

**Decision needed:** is ONNX the source of truth, or is ONNX a signal inside a rule-based policy?

**Good fix:** document the policy in code and align classifier evaluation with production behavior.

---

### M2 — Feature CSV export was not CSV-safe

**Files:** `src-tauri/src/storage/mod.rs`

Feature values used to be written with `values.join(",")`. Text fields such as `app_name` and `window_title` can contain commas, quotes, or newlines.

**Why it mattered:** training data could silently become malformed.

**Status:** fixed. Export now uses shared CSV escaping helpers with regression tests for commas, quotes, and newlines.

---

### M3 — Engine loop has lock churn

**Files:** `src-tauri/src/state.rs`

The loop clones app rules per event, locks storage multiple times per prediction, and holds the classifier lock during prediction/ONNX inference.

**Why it matters:** probably fine now, but it can cause latency under load and makes future concurrency harder.

**Good fix:** fix lifecycle/data bugs first. Later, batch reads and narrow lock scopes.

---

### M4 — Unbounded event channel

**Files:** `src-tauri/src/capture/thread.rs`, `src-tauri/src/state.rs`

Capture uses an unbounded `mpsc` channel. If the engine loop stalls, memory can grow.

**Good fix:** use a bounded channel with a clear drop policy, or keep unbounded but emit metrics/warnings when backlog grows.

---

### M5 — Bad DB rows can disappear silently

**Files:** `src-tauri/src/storage/mod.rs`

Some row readers use `filter_map(Result::ok)`, which drops rows that fail to deserialize.

**Why it matters:** corruption or schema drift becomes invisible.

**Good fix:** collect `Result<Vec<_>, _>` and surface the first row error.

---

### M6 — Foreign keys are not enforced

**Files:** `src-tauri/src/storage/mod.rs`

The schema declares relationships, but SQLite needs `PRAGMA foreign_keys = ON` per connection.

**Good fix:** enable the pragma in `Storage::open()` after connecting. Add a test for orphan prevention if practical.

---

### M7 — Training subprocess has no timeout

**Files:** `src-tauri/src/training_deploy.rs`

`Command::output()` can block indefinitely.

**Good fix:** add a timeout or run training as a tracked background task with status polling.

---

### M8 — IPC inputs needed bounds

**Files:** `src-tauri/src/commands.rs`

Examples included unbounded `limit` in history/timeline commands and no max lengths for session goals or other user-facing strings.

**Status:** partially fixed. Limits are clamped and key user-facing strings are validated at the command boundary; broader command-flow coverage is still worth adding.

---

### M9 — Frontend utility duplication

**Files:** `frontend/src/utils.ts`, `frontend/src/api.ts`, `frontend/tests/utils.test.ts`

The app used to import formatting helpers from `api.ts`, while tests covered `utils.ts`. That meant tests could pass while the app code drifted.

**Status:** fixed. Pure helpers now live in one tested module and `api.ts` re-exports the shared helpers the app imports.

---

### M10 — `ml/labeling.py` is confusing

**Files:** `ml/labeling.py`

The real app stores labels through Rust/SQLite. `Labeler` is a stub used only by tests; enum types are still useful.

**Good fix:** rename to `label_types.py` or clearly document that only the enums are production contract.

---

### M11 — Python requirements are not installable

**Files:** `ml/requirements.txt`

Training deps are commented out. Users can run training and fall into majority-stub / ONNX-skipped paths.

**Good fix:** create a real optional requirements file, such as `ml/requirements-train.txt`, with `xgboost`, `onnx`, and `onnxmltools`.

---

## Low-priority notes

- Startup `expect()` calls in `lib.rs` can abort release builds if app data or Tauri build setup fails.
- Permission status can go stale until refresh or capture failure.
- `let _ = send(...)` patterns hide dropped capture/overlay events.
- `KeyRelease` exists in `types.rs` but capture only emits key press.
- Feature/prediction/context tables have no retention policy.
- Legacy C++ ML modules remain in `ml/`; docs now mark these as historical.

---

## Test gaps to close first

| Test | Why |
|------|-----|
| `permissions.rs` platform messages / probe honesty | Locks health wording and setup guidance |
| `focus_modes.rs` hyperfocus thresholds | Small, easy first Rust test |
| Command/session flow coverage | Protects boundary validation and session lifecycle behavior |

---

## Suggested starter tasks

If an agent is picking this up cold, start with one of these:

### Task A — Fix training false-success

Files:

- `src-tauri/src/training_deploy.rs`
- `frontend/src/App.tsx`
- `frontend/src/api.ts`
- tests around `training_deploy.rs` or frontend training status

Goal:

- A missing `model.onnx` should produce a warning / not-deploy-ready state.
- The app should not imply the ONNX model is active unless reload succeeds.

### Task B — Fix capture lifecycle

Files:

- `src-tauri/src/state.rs`
- `src-tauri/src/capture/thread.rs`

Goal:

- Stop old capture threads before respawn.
- Add a test or other verification that restart does not leak duplicate workers.

### Task C — Make CSV export safe

Files:

- `src-tauri/src/storage/mod.rs`

Goal:

- Use proper CSV escaping for `features.csv`.
- Add a test with a comma/quote in `window_title`.

### Task D — Harden command/session inputs

Files:

- `src-tauri/src/commands.rs`

Goal:

- Clamp user-provided limits for history/timeline commands.
- Validate session goal length and other user-facing strings at the command boundary.

---

## Commit message suggestion

For documenting this review:

```text
Add code health review and implementation queue
```
