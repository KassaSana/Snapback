# Code Health Review

Last reviewed: 2026-07-06

Scope: Rust/Tauri backend, React frontend, Python ML pipeline, and cross-language training/deploy path.

Overall grade: **B-**. Snapback is a working alpha with a sound core loop. The biggest risks are not algorithmic; they are lifecycle, data integrity, frontend maintainability, and training success semantics.

Use this doc as the fix queue after the smoke test. The full planning backlog lives in [BACKLOG.md](BACKLOG.md).

---

## Quick read

| Area | Grade | Summary |
|------|-------|---------|
| Rust backend | B | Good architecture and tests in storage/classifier; weak capture lifecycle and session integrity |
| Frontend | C+ | Functional but too centralized in `App.tsx`; many silent errors and thin tests |
| ML pipeline | B+ | Best-structured area; good unit tests, but optional dependency path is loose |
| Train/deploy integration | C | Can report success when `model.onnx` was not produced |
| Security | B | Reasonable for a local desktop app; training repo path is trusted code execution |
| Tests | C | Strong unit coverage in core modules; weak IPC, engine-loop, capture, and frontend coverage |

---

## Fix order

Start here. These are ordered for impact and learning value.

1. **60-minute smoke test**  
   No code first. Run capture → label → snapback → export → train → reload and write down exactly where the app feels unreliable.

2. **Training false-success**  
   `training_deploy.rs` can return `success: true` when Python exits 0 but `model.onnx` was skipped. The UI should treat this as "training ran, deploy not ready" and make the warning obvious.

3. **Single ACTIVE session invariant**  
   `storage.start_session()` always inserts a new ACTIVE session. Enforce one active session at a time or explicitly complete the old one.

4. **Capture lifecycle**  
   `restart_capture_if_needed()` can spawn new capture threads without stopping old ones. Add cancellation/handles before respawn.

5. **Permission honesty**  
   macOS `probe_capture()` checks active window access, not actual `rdev` input capture. The health UI can say capture is OK when Input Monitoring is blocked.

6. **Training CSV escaping**  
   Feature export joins values with commas. Window titles with commas can corrupt training CSVs.

7. **Frontend error handling and split**  
   `App.tsx` is large and has many bare `catch {}` blocks. Pull out hooks/components gradually and show user-visible errors.

8. **Regression tests**  
   Add one narrow test per fix: active-session invariant, CSV escaping, training false-success branch, `api.ts` mappers.

---

## High-priority findings

### H1 — macOS permission probe can lie

**Files:** `src-tauri/src/capture/permissions.rs`

`probe_capture()` on macOS checks `active_win_pos_rs::get_active_window()`. That verifies active-window access, not global input capture through `rdev`. Accessibility permission can pass while Input Monitoring is denied.

**Why it matters:** the app can show a healthy permission state, then capture fails after launch.

**Good fix:** separate active-window status from input-capture status. If a real short `rdev` probe is impractical, make the UI honest: "capture status unknown until listener starts" and rely on the capture-failed event.

---

### H2 — Capture restart leaks old threads

**Files:** `src-tauri/src/state.rs`, `src-tauri/src/capture/thread.rs`

`spawn_capture()` creates a new event channel and starts capture threads. It replaces `event_rx`, but old capture threads are not stopped or joined. A permission refresh after failure can leave old pollers/listeners running and sending to dropped receivers.

**Why it matters:** long-running app sessions can accumulate wasted threads and confusing duplicate behavior.

**Good fix:** return a capture handle from `start_capture_thread()` with a cancellation flag and join handles for poller/idle threads. Store it in `AppState` and stop it before spawning a new capture.

---

### H3 — Multiple ACTIVE sessions are allowed

**Files:** `src-tauri/src/storage/mod.rs`, `src-tauri/src/commands.rs`

`start_session()` inserts a new ACTIVE session without checking for an existing one. `get_active_session()` returns only the newest.

**Why it matters:** orphan ACTIVE sessions can accumulate. Predictions, labels, and recaps can attach to a different session than the user expects.

**Good fix:** enforce one ACTIVE session. Either reject `start_session()` when one exists, or auto-complete the previous session before inserting a new one. Add a storage test.

---

### H4 — Training can "succeed" without deployable ONNX

**Files:** `src-tauri/src/training_deploy.rs`, `frontend/src/App.tsx`, `ml/pipeline_cli.py`, `ml/export_onnx.py`

If Python exits 0 but ONNX export is skipped, Rust returns `success: true` and `onnx_exported: false`.

**Why it matters:** users can think they trained and deployed a model when the app is still using the heuristic backend.

**Good fix:** model the result as two states: `trainingSucceeded` and `deployReady`. In the UI, success without ONNX should be a warning, not a green state. Consider non-zero exit or explicit `deploy_ready: false`.

---

### H5 — Frontend errors are often swallowed

**Files:** `frontend/src/App.tsx`

`App.tsx` is roughly 1,000 lines and has many bare `catch {}` blocks. Session start/stop, refresh, training status, and listener paths can fail with little or no feedback.

**Why it matters:** when something breaks, the user sees stale UI instead of an actionable error.

**Good fix:** add a small `setErrorBanner()` pattern first. Then split by feature: `useHealth`, `useSession`, `useTrainingDeploy`, and presentational cards.

---

## Medium-priority findings

### M1 — ONNX behavior is not clearly defined

**Files:** `src-tauri/src/engine/classifier.rs`, `src-tauri/src/engine/classifier_eval.rs`

The heuristic probabilities are computed first. If ONNX is loaded, ONNX scores replace them. Then heuristic guardrails still override final `focus_state` for high risk, thrash, drift, and app block rules.

**Decision needed:** is ONNX the source of truth, or is ONNX a signal inside a rule-based policy?

**Good fix:** document the policy in code and align classifier evaluation with production behavior.

---

### M2 — Feature CSV export is not CSV-safe

**Files:** `src-tauri/src/storage/mod.rs`

Feature values are written with `values.join(",")`. Text fields such as `app_name` and `window_title` can contain commas, quotes, or newlines.

**Why it matters:** training data can silently become malformed.

**Good fix:** use a CSV writer crate or implement RFC 4180 escaping in one helper and test window titles with commas/quotes.

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

### M8 — IPC inputs need bounds

**Files:** `src-tauri/src/commands.rs`

Examples: unbounded `limit` in history/timeline commands, no max session goal length, no active-session invariant at command boundary.

**Good fix:** clamp limits and validate user-facing strings.

---

### M9 — Frontend utility duplication

**Files:** `frontend/src/utils.ts`, `frontend/src/api.ts`, `frontend/tests/utils.test.ts`

The app imports formatting helpers from `api.ts`, while tests cover `utils.ts`. That means tests can pass while the app code drifts.

**Good fix:** keep one source of truth. Move pure helpers out of `api.ts` into one tested module, or delete `utils.ts` and test `api.ts` helpers directly.

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
| `storage.start_session()` with existing active session | Locks session invariant |
| Training result with `success=true`, `onnx_exported=false` | Locks deploy semantics |
| Feature CSV title with comma/quote/newline | Protects ML exports |
| `api.ts` mapper tests | Tests what frontend actually imports |
| `focus_modes.rs` hyperfocus thresholds | Small, easy first Rust test |
| Capture restart behavior | Prevents thread leak regressions |

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

### Task B — Enforce one active session

Files:

- `src-tauri/src/storage/mod.rs`
- `src-tauri/src/commands.rs`

Goal:

- Starting a session while another is ACTIVE should either reject or complete the prior one.
- Add a unit test documenting the chosen behavior.

### Task C — Make CSV export safe

Files:

- `src-tauri/src/storage/mod.rs`

Goal:

- Use proper CSV escaping for `features.csv`.
- Add a test with a comma/quote in `window_title`.

### Task D — Begin frontend cleanup

Files:

- `frontend/src/App.tsx`
- `frontend/src/api.ts`
- `frontend/src/utils.ts`
- `frontend/tests/*`

Goal:

- Remove dead utility duplication.
- Add tests for the helpers the app actually imports.
- Add visible error state for failed session/training actions.

---

## Commit message suggestion

For documenting this review:

```text
Add code health review and implementation queue
```
