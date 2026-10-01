# Roadmap planning history

> Historical planning and audit narrative moved from the live roadmap on 2026-09-30.
> Original dates, estimates, claims, and proposed sequencing are preserved below.
> This is not a second backlog or current priority order. Follow the
> [live roadmap](../ROADMAP.md); historical strategy does not override its sequence or ADRs.

**There is no second backlog.** `docs/TODO.md` used to carry open items and drifted out of
sync — it tracked `2.4b` as a task while this file correctly tracked the same work as the
decision in 5.3. It was **deleted** on 2026-07-20; its history is in git and its `[x]`
entries duplicated the [archive](../roadmap_archive.md). Don't reopen a parallel list.

**Reconciled against the code: 2026-08-19.** Five features landed on 2026-08-14 without this
file being touched, so for five days it described work that already existed. Checking them
against the tree rather than against their commit messages changed the answer for four of the
five: only **2.8** was actually complete. **2.18**, **9.15**, and **10.14** each shipped one
half and are now marked as such, with the missing half named — in every case the half was the
*harder* one (rule scoping, the activation channel, moving the three exports onto the new
dialog seam), which is the failure mode worth expecting from a commit message that reads as a
finished feature. **9.15's missing half closed on 2026-08-27**; 2.18's and 10.14's are open. **2.9 was not touched at all**: the goal-history dropdown belongs to
**2.11**'s idle state, and CHANGELOG.md had it filed under 2.15 — a third item, whose own work
was done on 2026-08-06. That label is corrected. Nothing in this pass re-ran the suites.

**Last audited against the code: 2026-08-05.** The July 31 hardening pass added ranked lock
ordering, immutable dependency pins, per-case CTest registration, classifier properties,
the large storage fixture, injected clocks, and private test seams. The August 1 performance
pass moved hot live reads to an immutable snapshot and added contention/lifecycle coverage.
The August 5 pass was read-only and did not rerun the suites; it audited production capture,
session lifecycle, reporting, training, and the full frontend composition. PR #40 earlier ran
the merged hardening baseline through all **15 hosted CI jobs**; all passed, including
`macos-gui-smoke`.

**The August 6 pass closed ten items in two batches.** First the release correction queue —
**7.23, 7.25, 7.12, 10.8, 6.6** — minus the decision it deliberately did not take. Then five
more from the audit batches: **2.15, 8.12, 7.26, 9.16, 10.13**. The local baseline is now
**400/400 C++ cases** (up from 336 after 7.22) and clean frontend unit scripts plus typecheck;
the component suite still cannot run on this machine (**11.11**), so every frontend change went
into a `tsx`-testable pure module rather than into a component.

*Progress 2026-09-18 (CI simplification):* push and pull-request CI now has seven hosted
jobs: the three-platform core matrix, ASan/UBSan, frontend tests, and Windows/macOS desktop
smokes. Repository, documentation, and supply-chain guards run within the Ubuntu core entry.
MinGW, TSan, ONNX, and npm advisory checks moved to weekly/on-demand deep checks; the Linux
desktop-link, benchmark-smoke, and dedicated formatting jobs were removed.

**Three defects in that pass were found by a test rather than by reading**, which is worth
recording because each was a plausible-looking wrong number rather than a crash. 10.13's SQL
credited the time spent *being distracted* to the focused run that followed, reporting every
stretch as exactly twice its length. The same parity check caught focused runs walking across
concurrent sessions. And 8.12's first classification moved support bundles into the delete set;
an existing case asserted otherwise, and on inspection the existing decision was the coherent
one. The pattern this file already records — check the claim before rebuilding around it —
applies to one's own new code too.

The formal v1 blocker list is **five of six verified complete**. Decision session A was
settled on 2026-08-03 by [ADR-0004](../adr/0004-verdict-and-opinion.md), leaving **macOS
packaging (3.3) as the only remaining blocker** — and it is paperwork with external lead
time, not a design question. The broader audit below did find release-hardening work outside
ADR-0002; those items must be closed before publishing even though they do not change the
blocker count.

**A note on trusting this file.** Past audits found items here that were simply wrong: 0.3
described work that was already written (and broken), 2.4 sits in the [archive](../roadmap_archive.md) on the
strength of code that never runs (see 5.3), and a reference path pointed at a directory
that doesn't exist on this machine. **When an item claims something is missing, check
whether it's actually missing before rebuilding it. When an item claims something is done,
check that the code has a caller.**

## The six-month sequence

Derived from the [2026-08-19 audit](audit-2026-08-19.md) (AUD/FWD tickets). This section is the
*strategic* frame: which phase comes before which, and why. The tiers below it remain the
item-level record — when the two disagree on ordering, this section's phase gates
(feature semantics before corpus, eval before deploy, release before demand-driven breadth)
are the tiebreaker.

**The sequencing rule:** each phase exists to make the next one cheaper. Trust-in-the-numbers
fixes come before any feature that displays numbers; feature-semantics fixes come before any
training-data collection; a shipped release comes before anything whose value depends on users
existing.

**The one-sentence plan:** fix the four things that are actually broken, ship the release the
repo is already dressed for, make the weekly data worth looking at, and only then spend on the
personalization loop — with platform breadth explicitly deferred.

Estimates assume ~10 h/week.

### What is already built and waiting (use it, don't rebuild it)

The audit found more finished machinery than open holes. These assets change what "new
feature" costs:

- **Training → quality gate → deploy → rollback pipeline** (`training_deploy.cpp`, dev-gated):
  the whole model lifecycle exists except a user-runnable trainer. FWD-03 is a middle third,
  not a greenfield.
- **Snapback episodes** persisted since 2.15 with start/duration/app/file-hint — surfaced
  almost nowhere. FWD-02's "most expensive distraction" is one query that already exists
  (`list_snapback_episodes`).
- **Attended spans, reflections, auto-labels, goal categories, attended targets** — all in the
  schema with tested write paths. The Review surface uses a fraction of them.
- **Release scaffolding**: CI with launch smokes on Windows/macOS, packaging + validation
  scripts, a changelog discipline, a release workflow gated on CI-green (Tier 9 is mostly
  checked off).
- **`SNAPBACK_FRONTEND_URL` dev seam, benchmark harness, feature-parity fixtures** — the
  infrastructure FWD-09 and FWD-07 need is half-present.

### Phase 0 — Weeks 1–3: make the existing product true

Nothing ships and nothing new gets built until the features the app already claims actually
work. All four fixes are small; their absence undermines every later phase.

| Item | Audit | Why now |
| --- | --- | --- |
| Fix "Take me back" payload drain | AUD-01 (S) | The namesake interaction is dead; FWD-04 builds directly on it |
| Fix external-link token omission | AUD-03 (XS) | Release notes / update links (Phase 1, FWD-06) will be `<a>` tags |
| Guard span-reopen on stopped sessions | AUD-04 (S) | Attendance numbers feed Phase 2's digest; a compounding corruption bug must die before numbers get more visible |
| Lock the Logger sink | AUD-05 (XS) | Cheap UB removal; every later phase adds log calls |
| Periodic retention prune | AUD-07 (S) | Long-uptime installs start existing the moment Phase 1 ships |
| Move `model_deployment_health_` into the live snapshot | AUD-06 (S) | Closes the accidental thread-safety before Phase 3 adds model churn |

Also in this window, because they gate *data semantics* for everything after: decide AUD-19
(no-session predictions: intended preview or bug) and AUD-16 (rollback gating) — both are
one-line decisions that get more expensive to change after v1 users exist.

**Exit criterion:** a staged distraction → snapback → "Take me back" round-trip works in a
Release build, and a soak run (app left recording overnight with a stop/start mid-way) shows
attended minutes that stop growing when the session stops.

#### Phase 0 task breakdown

Task ids are permanent. Verify commands assume the local build directory; see
[`running.md`](../running.md).

- [x] **P0-01** Serialize Logger sink writes (AUD-05): move the `sink_ <<` write under the
      existing mutex; make `min_level_` atomic.
      *Files:* `src/util/logger.hpp`, `tests/test_logger.cpp` — *Depends on:* —
      *Verify:* `ctest -R logger --output-on-failure` with a new concurrent-writers case, then
      the full suite.
- [x] **P0-02** Keep the snapback payload restorable (AUD-01): add a `snapback_emitted_` flag
      so the tick emits once without clearing `latest_snapback_`; clear only on
      dismiss/restore/replace.
      *Files:* `src/app/state.hpp`, `src/app/state.cpp`, `tests/test_app_state.cpp` —
      *Depends on:* —
      *Verify:* new doctest case driving fire → engine-tick drain → `restore_snapback_target()`
      returns ok; `ctest -R app_state --output-on-failure`.
- [x] **P0-03** Attach the capability token in the shim's link interceptor (AUD-03).
      *Files:* `src/app/ipc_shim.cpp`, `tests/test_ipc_shim.cpp` — *Depends on:* —
      *Verify:* `ctest -R ipc_shim --output-on-failure` with a new assertion that the
      interceptor's `open_external_url` payload carries `__snapbackToken`.
- [x] **P0-04** Refuse spans on non-ACTIVE sessions in storage (AUD-04a): make
      `begin_session_span` a guarded `INSERT ... SELECT` that no-ops when the session is not
      ACTIVE, and report whether it inserted.
      *Files:* `src/storage/storage.cpp`, `src/storage/storage.hpp`, `tests/test_storage.cpp` —
      *Depends on:* —
      *Verify:* new doctest case: `begin_session_span_now` on a COMPLETED session leaves
      `has_open_span` false; `ctest -R storage --output-on-failure`.
- [x] **P0-05** Invalidate the tick's pending span decision on session mutation (AUD-04b):
      capture the session id with the pending decision and have stop/start/delete clear it.
      *Files:* `src/app/state.cpp`, `src/app/state.hpp`, `tests/test_app_state.cpp` —
      *Depends on:* P0-04
      *Verify:* deterministic interleave test via `AppStateTestAccess` (stage a span-open,
      `stop_session`, run the persist phase, assert no open span).
- [x] **P0-06** Serve `model_deployment_health_` from the live snapshot (AUD-06).
      *Files:* `src/app/state.hpp`, `src/app/state.cpp` — *Depends on:* —
      *Verify:* full `ctest --output-on-failure` — behaviour-neutral refactor, so the suite is
      the check.
- [x] **P0-07** Run the retention prune periodically, not only at startup (AUD-07): once per
      24 h of uptime from the tick's storage phase, no VACUUM while a session is active.
      *Files:* `src/app/state.cpp`, `src/app/state.hpp`, `tests/test_app_state.cpp` —
      *Depends on:* —
      *Verify:* doctest case using the injected `ManualClock` to advance 24 h and assert
      `prune_runtime_data` ran.
- [x] **P0-08** Record the two open decisions (AUD-16, AUD-19). **Decided:** rollback stays
      user-facing (ADR-0006 gates *producing* a model, not recovering from a bad one — so
      `retry_model_deployment_cleanup` stays ungated for the same reason); no-session
      predictions are a deliberate live preview, so the health field was renamed
      `no_session` → `not_recorded` to stop claiming a suppression that never happened.
      *Files:* `src/app/commands.hpp`, `src/app/state.cpp`, `frontend/src/useDiagnostics.ts`,
      `tests/test_app_state.cpp`, [`ARCHITECTURE.md`](../ARCHITECTURE.md) — *Depends on:* —
      *Verify:* comments at both bind sites state the decision, ARCHITECTURE.md's IPC section
      has a "Two decisions the command surface encodes" subsection, and a new doctest pins the
      preview semantics (predicts with an empty `session_id`, so `persist()` drops it).
- [ ] **P0-09** Release-build soak check on Windows (uses the
      [`windows_demo.md`](../windows_demo.md) flow).
      *Depends on:* P0-01 … P0-05, P0-07
      *Verify:* manual, on a Release build — (1) a staged distraction fires a snapback and
      "Take me back" activates the target window; (2) an external link in the dashboard opens
      the system browser; (3) after stop-session then 10+ min of activity, the Review surface's
      attended minutes for the stopped session do not grow.

### Phase 1 — Weeks 3–6: ship v1 and the channel to v1.1

| Item | Audit | Notes |
| --- | --- | --- |
| ~~Cut Windows v0.3.0~~ | FWD-01 | **Done 2026-08-29** — published unsigned (installer + ZIP); the SmartScreen caveat is in the README; signing stays 0.4b |
| Auto-update check | FWD-06 (S-M) | Native-side fetch; depends on AUD-03 (the "download" link is an external link) |
| Notification budget & quiet hours | FWD-05 (S-M) | Retention insurance *before* strangers install; pure settings-surface work |
| `winget` manifest | FWD-06 rider (S) | Only once signing lands; otherwise defer to v0.4 |

**Exit criterion:** a public GitHub release v0.3.0 exists with an installable Windows package
built by the release workflow, and a fresh machine installs it and records a session. Also
covers the dangling-`v0.2.0`-tag cleanup the changelog warns about (9.13).

*Status 2026-09-22:* the release half is met — `v0.3.0` was published 2026-08-29 by the
release workflow, and 9.13 is done. The fresh-machine install-and-record half has no recorded
result; it is the same manual check as **P0-09**.

**Why before the data work:** every Phase 2+ decision improves with even ten real users'
feedback, and the release machinery is the closest-to-done big item in the repo.

### Phase 2 — Weeks 6–12: fix the feature semantics, then make the data worth opening

Ordering inside this phase is load-bearing: **the feature-vector fixes must land before the
digest advertises the numbers and before any training corpus is collected**, because they
change the input distribution (train/serve skew otherwise). This is one coordinated
feature-contract change, not three drive-by fixes:

1. **DONE 2026-09-20.** Synthesize idle events / reset the break clock (AUD-02, M) — revives
   `idle_time_30s`, `idle_event_count_5min`, `longest_active_stretch_5min`,
   `minutes_since_last_break`, and re-arms the hyperfocus nudge. The wake-context remainder
   now applies the idle edge before processing its input, preserves the latest permitted
   foreground independently of the AFK-frozen event windows, and reconciles it through
   `features.cpp:FeatureExtractor::resynchronize_foreground` before the first post-wake
   prediction. Production-path tests pin IDE → idle → blocked browser, one counted waking
   key, one `IdleEnd`, and excluded-app privacy.
2. Stop counting the empty app name in `unique_apps_5min` (AUD-12, XS) — batched into the same
   contract bump and golden-fixture regeneration.
3. Resolve the `is_pseudo_productive` question with the trainer (AUD-11, XS investigate) —
   the contract bump is the moment to drop or document it.
4. Windows context-gate improvement (AUD-09, M) — foreground-event-driven refresh; improves
   the very features (thrash, keystroke rate) the digest will highlight.

Then, on top of honest features:

| Item | Audit | Notes |
| --- | --- | --- |
| Weekly focus story / digest | FWD-02 (M) | Composition of existing queries + episodes; needs AUD-08's cap honesty for any "all time" claim |
| "All time" cap honesty | AUD-08 (S) | Do together with the digest — same surfaces |
| Snapback recovery upgrade | FWD-04 (M) | Builds on AUD-01; episode-outcome logging here is deliberately *before* Phase 3, because it produces the labels Phase 3 trains on |

**Exit criterion:** `ctest` green with a regenerated `fixtures/feature_parity/golden.json`, and
a test proves the hyperfocus nudge re-arms after a real break. **Do not start collecting a
training corpus before this lands** (the skew rule above).

**Conflict noted:** Tier 2 below contains several ML-depth items that assume the current
31-feature contract. Any of those started before this phase's contract bump would be built on
features that are about to change meaning — sequence them after.

### Phase 3 — Weeks 12–20: the personalization loop

| Item | Audit | Notes |
| --- | --- | --- |
| Offline model evaluation harness | FWD-07 (M) | **First.** No model swap without a replay-based "not worse on your own history" check |
| On-device retrain path | FWD-03 (L) | **Flagged as a partial rewrite**: option (a) re-implements the trainer contract in C++ and revises [ADR-0006](../adr/0006-trainer-is-developer-tooling.md). Timebox a spike (1 week) before committing; option (c) — loop stays dev-only for v1.x — is an acceptable outcome |
| Model observability polish | FWD-07 rider | `state_source` and `explainPrediction` already exist; wire eval results into the TrainingDeploy card |

**Dependency chain that justifies the ordering:** Phase 2's feature fixes → fresh corpus with
correct semantics → FWD-07's harness to judge candidates → FWD-03's trainer has something safe
to do. Starting FWD-03 first (it's the most exciting item) would train on features the fixed
engine never reproduces — the skew would be introduced by roadmap ordering alone.

**Supersession noted:** if FWD-03's spike lands on option (a) (native mini-trainer), the
existing Python-repo plumbing (`set_training_repo_path`, `train_from_export`'s repo checks)
becomes legacy for end users — keep it for developers, but don't invest further in its UX
(several Tier 13 items become dev-only concerns at that point).

### Phase 4 — Weeks 20–26: quality, breadth *decision*, and paydown

| Item | Audit | Notes |
| --- | --- | --- |
| Frontend surface split | AUD-20 (M-L) | Do it *after* the digest reshaped Review — splitting before would split twice |
| File splits, const-correctness, test-runner glob | AUD-21, AUD-15, AUD-22 (each S) | Opportunistic paydown; none blocks anything, which is why they're last |
| Clock seam completion | AUD-14 (S-M) | Do the AppState half; Storage half only if a Phase 2/3 bug demanded it |
| Linux cheap fixes | AUD-10 parts (2)+(3) (S) | Click mapping + wall clock: two small diffs, big honesty gain for Linux users |
| **Platform breadth decision** | — | One platform gets real investment next half: macOS polish (notifications 3.3, signing) *or* Linux capture rework (AUD-10 part 1 + 3.2). Not both. Decide from v0.3 install feedback — whichever platform users actually asked about |
| macOS audit pass | AUD-23.4 | The `.mm` files have not had this audit's scrutiny; do it on a Mac before investing in macOS breadth |

Deliberately **not** scheduled in these six months (from FWD-10): calendar integration,
gamification, browser extension, cloud sync, Linux UI parity. Each is either
differentiator-diluting or sequenced behind evidence of demand.

### Open questions blocking expansion

- [x] Ship v0.3.0 unsigned with a documented SmartScreen caveat, or wait for code signing
      (0.4b)? — **shipped unsigned 2026-08-29**; the caveat is in the README.
- [ ] What does the Python trainer do with `is_pseudo_productive` (column 31)? Drop, train on,
      or derive from labels? (AUD-11) — blocks Phase 2 expansion.
- [ ] Does the app run long enough unattended to soak-test P0-09's attended-minutes check
      overnight, or should the check use a shortened idle threshold instead? — affects P0-09's
      wording, not its substance.

### Standing risks this sequence carries

- **Phase 2's contract bump is the riskiest single change** — it touches golden fixtures, the
  deployed-model assumption, and product behaviour (hyperfocus re-arming) at once. Mitigation:
  it's also the phase with the eval harness *next*, so schedule slip there should push Phase 3
  wholesale rather than letting FWD-03 start early.
- **FWD-03 is the only L-sized item and the only flagged rewrite.** The spike-then-decide gate
  is the plan's main scope valve: if the spike fails, Phases 3–4 compress and the freed time
  goes to the Phase 4 platform decision.

---
