# TODO

Started: 2026-08-19 · Last updated: 2026-08-19 · Estimates assume ~10 h/week.

Execution-level plan. Strategy and rationale live in [ROADMAP.md](ROADMAP.md) (phases) and
[BACKLOG.md](BACKLOG.md) (AUD/FWD tickets); item-level history in
[docs/ROADMAP.md](docs/ROADMAP.md). Task ids here are permanent; AUD/FWD references point
into BACKLOG.md.

| Milestone | Weeks | Core focus | Done when |
|---|---|---|---|
| 1 — Make it true | 1–3 | Fix the shipped-but-broken paths (ROADMAP Phase 0) | A staged distraction → snapback → "Take me back" round-trip works in a Release build, and attended minutes stop growing after a session stops (M1-09 checklist passes) |
| 2 — Ship v0.3.0 | 4–7 | First public Windows release + update awareness (Phase 1) | A public GitHub release v0.3.0 exists with an installable Windows package built by the release workflow, and a fresh machine installs and records a session from it |
| 3 — Honest features | 8–11 | Feature-contract fix batch: idle events, dead inputs, fixtures (Phase 2 first half) | `ctest` green with regenerated `fixtures/feature_parity/golden.json`, and a test proves the hyperfocus nudge re-arms after a real break |

## Milestone 1 — Make it true (weeks 1–3)

Done when: the M1-09 manual checklist passes on a Release build.

- [ ] M1-01 Serialize Logger sink writes (AUD-05): move the `sink_ <<` write under the
      existing mutex; make `min_level_` atomic
      Files: src/util/logger.hpp, tests/test_logger.cpp
      Depends on: —
      Verify: `ctest --test-dir build -R logger --output-on-failure` (with a new
      concurrent-writers test case), then full `ctest --test-dir build`

- [ ] M1-02 Keep the snapback payload restorable (AUD-01): add a `snapback_emitted_` flag so
      the tick emits once without clearing `latest_snapback_`; clear only on
      dismiss/restore/replace
      Files: src/app/state.hpp, src/app/state.cpp, tests/test_app_state.cpp
      Depends on: —
      Verify: new doctest case driving fire → engine-tick drain → `restore_snapback_target()`
      returns ok; `ctest --test-dir build -R app_state --output-on-failure`

- [ ] M1-03 Attach the capability token in the shim's link interceptor (AUD-03)
      Files: src/app/ipc_shim.cpp, tests/test_ipc_shim.cpp
      Depends on: —
      Verify: `ctest --test-dir build -R ipc_shim --output-on-failure` with a new assertion
      that the interceptor's `open_external_url` payload carries `__snapbackToken`

- [ ] M1-04 Refuse spans on non-ACTIVE sessions in storage (AUD-04a): make
      `begin_session_span` a guarded `INSERT ... SELECT` that no-ops when the session is not
      ACTIVE, and report whether it inserted
      Files: src/storage/storage.cpp, src/storage/storage.hpp, tests/test_storage.cpp
      Depends on: —
      Verify: new doctest case: `begin_session_span_now` on a COMPLETED session leaves
      `has_open_span` false; `ctest --test-dir build -R storage --output-on-failure`

- [ ] M1-05 Invalidate the tick's pending span decision on session mutation (AUD-04b):
      capture the session id with the pending decision and have stop/start/delete clear it;
      re-check in phase 2
      Files: src/app/state.cpp, src/app/state.hpp, tests/test_app_state.cpp
      Depends on: M1-04
      Verify: deterministic interleave test via `AppStateTestAccess` (stage a span-open,
      `stop_session`, run the persist phase, assert no open span); `ctest --test-dir build -R
      app_state --output-on-failure`

- [ ] M1-06 Serve `model_deployment_health_` from the live snapshot (AUD-06)
      Files: src/app/state.hpp, src/app/state.cpp
      Depends on: —
      Verify: `ctest --test-dir build --output-on-failure` (behavior-neutral refactor; full
      suite is the check)

- [ ] M1-07 Run the retention prune periodically, not only at startup (AUD-07): once per 24 h
      of uptime from the tick's storage phase, no VACUUM while a session is active
      Files: src/app/state.cpp, src/app/state.hpp, tests/test_app_state.cpp
      Depends on: —
      Verify: doctest case using the injected `ManualClock` to advance 24 h and assert
      `prune_runtime_data` ran (row older than retention is gone); `ctest --test-dir build -R
      app_state --output-on-failure`

- [ ] M1-08 Record the two open decisions (AUD-16, AUD-19): rollback stays user-facing or
      gets dev-gated; no-session predictions are a deliberate live preview or get gated
      Files: src/app/commands.hpp, src/app/state.cpp, docs/ARCHITECTURE.md
      Depends on: —
      Verify: manual — a comment at the `rollback_classifier_model` bind site states the
      decision, and docs/ARCHITECTURE.md's IPC section states the no-session prediction
      semantics

- [ ] M1-09 Release-build soak check on Windows
      Files: — (uses docs/windows_demo.md flow)
      Depends on: M1-01, M1-02, M1-03, M1-04, M1-05, M1-07
      Verify: manual — on a Release build: (1) staged distraction fires a snapback and
      "Take me back" activates the target window; (2) an external link in the dashboard opens
      the system browser; (3) after stop-session then 10+ min of activity, the Review
      surface's attended minutes for the stopped session do not grow

## Milestone 2 — Ship v0.3.0 (weeks 4–7)

Done when: a public GitHub release v0.3.0 exists with an installable Windows package built by
the release workflow, and a fresh machine installs it and records a session.
Covers: FWD-01 (release), FWD-06 (update check), FWD-05 (notification budget), the signing
decision (docs/ROADMAP.md 0.4b), and the dangling-v0.2.0-tag cleanup the changelog warns
about. Estimated 8 tasks. Not expanded yet.

## Milestone 3 — Honest features (weeks 8–11)

Done when: `ctest` is green with regenerated feature-parity goldens and a test proves the
hyperfocus nudge re-arms after a break.
Covers: AUD-02 (synthesize idle events / break reset), AUD-12 (empty app name), AUD-11
(trainer answer on `is_pseudo_productive`), AUD-09 (Windows foreground-event context
refresh), one feature-contract id bump, fixture regeneration. Estimated 10 tasks. Not
expanded yet. **Do not start collecting a training corpus before this lands** (ROADMAP
Phase 2 skew rule).

## Open questions

- [ ] Ship v0.3.0 unsigned with a documented SmartScreen caveat, or wait for code signing
      (docs/ROADMAP.md 0.4b)? — blocks M2 expansion
- [ ] What does the Python trainer do with `is_pseudo_productive` (column 31)? Drop, train
      on, or derive from labels? (AUD-11) — blocks M3 expansion
- [ ] Does the app run long enough unattended on your machine to soak-test M1-09's attended-
      minutes check overnight, or should the check use a shortened idle threshold instead? —
      blocks M1-09 only in wording, not in substance
