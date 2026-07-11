# Snapback test backlog

Last reviewed: 2026-07-11.

Use this doc to move Snapback's test confidence from alpha-grade to release-grade without turning every change into a giant batch. Each checkbox should be its own small, reviewable increment unless the work is obviously tiny.

## What "one-feature-at-a-time" means here

The list below is an ordered roadmap, not a checklist for every commit.

A single increment should usually do one thing:

- add one focused group of tests,
- run the relevant test command,
- explain what confidence improved,
- suggest one commit message.

Example: "Add Rust session lifecycle tests" is one increment. It does not also need coverage tooling, WebDriver, frontend tests, and doc cleanup in the same change.

## Current assessment

Testing grade today: B / B- for alpha.

Snapback has a solid test backbone:

- Rust unit tests cover much of the core app logic.
- Python tests cover the ML/export/training pipeline.
- Frontend tests cover several important React flows with mocked Tauri boundaries.
- CI runs Python, frontend, Rust, ONNX, feature parity, classifier eval, Tauri build smoke, and the headless smoke harness.

The main weakness is assembled-app confidence. The pieces are fairly well tested, but the real desktop path still needs stronger proof: Tauri commands, OS permissions, global shortcuts, tray behavior, overlay behavior, long-running capture health, and one real app happy path.

Goal: reach B+ by strengthening integration/system coverage, then approach A- by adding one real desktop E2E path.

## Recommended order

### 1. Rust command/session lifecycle tests

- [x] Add or strengthen tests for start session, stop session, and active-session invariants.
- [x] Verify empty or invalid goals are rejected at the command boundary.
- [x] Verify predictions/context/labels persist only when a session is active. (storage `session_gated_prediction_and_feature_persistence`, `prediction_requires_existing_session`)
- [x] Verify labels attach to the intended session.

Status: command-boundary tests now cover trimmed session goals, blank-goal rejection, **too-long-goal rejection with state left untouched**, focus-mode application, automatic completion of the previous active session, stop-session feature-state reset, **stop-unknown-session error**, automatic end-of-session labels, manual/survey label submission, **blank/too-long label-notes rejection**, and **cross-session label isolation** (a label lands only on its intended session) through the command path.

Known limitation: stopping an already-COMPLETED session re-runs `save_auto_session_label`, writing a duplicate AUTO label. Low impact (the UI doesn't offer re-stop); left unasserted and noted for a future fix.

Why first: this protects the product's core promise. If session state is wrong, every downstream feature becomes suspicious.

Concept: contract testing. The command layer is the contract between the UI and the Rust app core.

Suggested test command:

```powershell
cargo test --manifest-path src-tauri/Cargo.toml
```

### 2. Release-grade smoke harness assertions

- [x] Tighten the headless smoke harness so pass/fail checks are explicit and easy to diagnose. (extracted `check_export_thresholds` + `check_train_outcome` pure functions with named-guarantee diagnostics; unit-tested without ONNX)
- [x] Assert seeded sessions, features, and labels exist. (`seed_smoke_sessions_exports_enough_rows`; runtime `run_smoke` seed/export stages)
- [x] Assert export produces the expected training artifacts. (`check_export_thresholds` unit test + `run_smoke` `has_export` check)
- [x] Assert training produces a deployable ONNX model when dependencies are present. (`check_train_outcome` unit-tests the pass/fail logic; the actual train+ONNX run stays in `run_smoke` behind `--features onnx`)
- [x] Assert classifier status flips to ONNX after reload. (`run_smoke` `load_onnx_backend` asserts backend == "onnx" post-reload; requires the ONNX toolchain)

Status: the smoke harness's pass/fail decisions are now pure, named functions covered by `cargo test` (no ONNX/Python needed), so a regression in the check *logic* is caught in normal CI. The full end-to-end train→ONNX→reload path still requires `cargo run --features onnx -- --smoke` with Python + onnxruntime present — that part is exercised on demand, not in the unit suite.

Why second: this verifies the whole product pipeline in one place.

Concept: system testing. It checks that storage, export, training, deploy, and classifier reload work together.

Suggested test command:

```powershell
cargo run --features onnx --manifest-path src-tauri/Cargo.toml -- --smoke
```

### 3. Frontend integration tests for critical flows

- [x] Add or strengthen React tests for session start/stop error handling. (`errorRecovery.test.tsx` start-failure → visible alert; `sessionFlow.test.tsx` success paths)
- [x] Add tests for training failure, training success, and "trained but not deploy-ready" states. (`trainingDeploy.test.tsx`: hard failure → message + no reload, deploy-ready → reload, trained-not-deployable → warning + no reload)
- [x] Add tests for visible action errors and recovery paths. (`errorRecovery.test.tsx`: error banner appears via `role="alert"`, Dismiss clears it)
- [x] Add tests for health and permission messages that block or warn before session start. (`errorRecovery.test.tsx`: capture-unavailable warning surfaces on start without blocking it)

Status: critical UI flows now have integration tests against a mocked Tauri boundary — session start success/failure + dismiss recovery, all three training outcomes (failed / not-deploy-ready / deploy-ready) including the reload-gating guarantee, and the pre-session capture-unavailable warning. Deeper permission-degradation state coverage (denied/unknown/degraded/dropped-event/no-events) is item #4.

Why third: the user experiences failures through the UI. Good backend behavior still feels broken if the frontend hides or mislabels it.

Concept: user-facing integration testing. Mock the boundary, but test the screen as the user sees it.

Suggested test commands:

```powershell
npm run test
npm run typecheck
```

### 4. Health and permission degradation tests

- [ ] Test capture unavailable, denied, unknown, degraded, and unsupported states.
- [ ] Test dropped-event warnings.
- [ ] Test no-events-received warnings.
- [ ] Test active-window unavailable messaging.
- [ ] Test recovery behavior when capture health improves after launch.

Why fourth: OS-facing features fail in messy ways. The app should make those failures visible and actionable.

Concept: poka-yoke. Design tests so silent bad states become hard to ship.

Suggested test commands:

```powershell
cargo test --manifest-path src-tauri/Cargo.toml permissions
npm run test
```

### 5. Coverage reporting

- [ ] Add frontend coverage reporting with Vitest.
- [ ] Consider Python coverage with `coverage.py`.
- [ ] Consider Rust coverage with `cargo llvm-cov`.
- [ ] Start with reporting only, then add thresholds after the numbers are understood.

Why fifth: coverage helps reveal forgotten areas, but it should not lead the strategy.

Concept: measurement as feedback. Coverage is a dashboard, not the definition of quality.

Possible initial targets:

- Frontend: 65-75%.
- Python ML: 75-85%.
- Rust: report first, threshold later.

### 6. One real Tauri/WebDriver E2E happy path

- [ ] Add a Tauri-driver/WebDriver test that launches the built app.
- [ ] Verify the dashboard renders.
- [ ] Start a session.
- [ ] Stop the session.
- [ ] Verify the UI reflects the completed session.

Why sixth: this catches issues that unit and mocked integration tests cannot catch: boot failures, packaging problems, IPC wiring, missing assets, and real-shell UI bugs.

Concept: end-to-end testing. Use sparingly because it is slower and more fragile, but keep one critical path protected.

### 7. Test documentation cleanup

- [ ] Update docs so they clearly distinguish automated smoke, manual smoke, and release smoke.
- [ ] Keep CI descriptions aligned with the workflow file.
- [ ] Add or update a test matrix: local, CI, manual, release-only.
- [ ] Remove stale testing claims from backlog/status docs.

Why last: docs should reflect the test reality after the test reality changes.

Concept: operational documentation. The test suite is easier to trust when the docs say exactly what is automated and what still needs human verification.

## Definition of done for each testing increment

- [ ] The change is isolated to one testing concern.
- [ ] Relevant tests were added or strengthened.
- [ ] The relevant command was actually run.
- [ ] Any known limitation is written down.
- [ ] A short commit message is suggested, but not committed by Codex.

## Target confidence levels

B+ means:

- core state transitions are covered,
- smoke harness gives clear system-level confidence,
- frontend critical flows have integration tests,
- health/permission degradation paths are visible and tested.

A- means:

- B+ items are done,
- one real desktop E2E happy path runs reliably,
- coverage reporting exists,
- docs match the automated/manual testing reality.
