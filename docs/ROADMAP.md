# ROADMAP.md — the single source of truth for what to build next

**This file is the live backlog, and it holds open work only.** Completed items live in
[`roadmap_archive.md`](roadmap_archive.md), including the ones whose explanation still teaches
a useful constraint — the lesson is not lost, it is one file over. This is the source of
truth for what is open; when another status document disagrees with this one, this one wins.

**Every open item carries a status**, so a fresh finding cannot be mistaken for a plan.
`scripts/check_roadmap_status.py` fails the build if one is missing. See
[How to read an item](#how-to-read-an-item).

**There is no second backlog.** `docs/TODO.md` used to carry open items and drifted out of
sync — it tracked `2.4b` as a task while this file correctly tracked the same work as the
decision in 5.3. It was **deleted** on 2026-07-20; its history is in git and its `[x]`
entries duplicated the [archive](roadmap_archive.md). Don't reopen a parallel list.

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
settled on 2026-08-03 by [ADR-0004](adr/0004-verdict-and-opinion.md), leaving **macOS
packaging (3.3) as the only remaining blocker** — and it is paperwork with external lead
time, not a design question. The broader audit below did find release-hardening work outside
ADR-0002; those items must be closed before publishing even though they do not change the
blocker count.

**A note on trusting this file.** Past audits found items here that were simply wrong: 0.3
described work that was already written (and broken), 2.4 sits in the [archive](roadmap_archive.md) on the
strength of code that never runs (see 5.3), and a reference path pointed at a directory
that doesn't exist on this machine. **When an item claims something is missing, check
whether it's actually missing before rebuilding it. When an item claims something is done,
check that the code has a caller.**

## How to read an item

- **Status**, first tag on every item — exactly one of:
  - **`proposed`** — a finding or an idea. Evidence may be solid; *nobody has agreed to build
    it*. This is the default, and most of the backlog is here. A review that lands new items
    lands them `proposed`.
  - **`accepted`** — agreed to build, with the agreement citable: a row in
    [Start here](#start-here--the-current-sequence), or a release blocker in an ADR. Promotion
    is a decision someone makes on purpose, never a side effect of writing the item well.
  - **`in progress`** — part of it has landed and the item says which part and what remains.
- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **`decision`** — do **not** write code for this until the question is answered. Roughly a
  third of the open backlog is decisions mistaken for bugs; that mistake has been made
  repeatedly here and has twice produced a "fix" that had to be reverted.

Work each item on the standard loop: code → test → senior-to-junior explanation → commit
(terse one-liner, Kassa's identity, zero AI attribution). Local work may be committed with
Kassa's configured identity; only Kassa pushes.

**Attribution is enforced, not trusted.** Every commit in this repository is Kassa's own
work: no `Co-Authored-By:` trailer, no "Generated with …" footer, no vendor address, ever —
in commit messages and PR bodies alike. `scripts/check_commit_attribution.py` checks the full
history of every ref on each CI run and allows exactly three author identities
(`kassasana03@`, `kassaplayz@`, and the `users.noreply.github.com` one), plus `dependabot[bot]`
for lockfile bumps.

This is a guard rather than a note because several tools append attribution automatically, at
commit time, when nobody is reading — and such a commit is permanent in a way an ordinary
mistake is not. Rewriting it changes every SHA after it, which would invalidate release tags
and the CI-conclusion check 9.11's release gate depends on. It must be caught before it lands.

The guard deliberately does **not** flag prose that merely names a tool: an existing commit
explains a filename decision by referring to `CLAUDE.md`, and naming a thing is not claiming
it wrote the code. It matches trailers, footers, and author/committer addresses only. It also
refuses to run against a shallow clone rather than report success for the one commit it can
see, which is why the Ubuntu `cpp-headless` entry checks out with `fetch-depth: 0`.

---

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
[`running.md`](running.md).

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
      `tests/test_app_state.cpp`, [`ARCHITECTURE.md`](ARCHITECTURE.md) — *Depends on:* —
      *Verify:* comments at both bind sites state the decision, ARCHITECTURE.md's IPC section
      has a "Two decisions the command surface encodes" subsection, and a new doctest pins the
      preview semantics (predicts with an empty `session_id`, so `persist()` drops it).
- [ ] **P0-09** Release-build soak check on Windows (uses the
      [`windows_demo.md`](windows_demo.md) flow).
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
| On-device retrain path | FWD-03 (L) | **Flagged as a partial rewrite**: option (a) re-implements the trainer contract in C++ and revises [ADR-0006](adr/0006-trainer-is-developer-tooling.md). Timebox a spike (1 week) before committing; option (c) — loop stays dev-only for v1.x — is an acceptable outcome |
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
## Start here — the current sequence

Ordered by dependency, not severity. This replaces every previous "suggested sequence."
Struck rows are done; the numbers renumber as they close, so "next" is always row 1.

| # | Item | Why now |
|---|------|---------|
| 1 | **3.3** macOS packaging + notarization | Formal v1 blocker with external lead time; start the Apple Developer account work first |
| — | ~~**13.7** settle the trainer's product boundary~~ | **Done 2026-08-07** — [ADR-0006](adr/0006-trainer-is-developer-tooling.md): developer-only tooling; consumer Settings keeps focus labels only |
| — | ~~**7.24** split monotonic and calendar time~~ | **Done 2026-08-05** |
| — | ~~**8.10** make release builds network-silent~~ | **Done 2026-08-05** |
| — | ~~**7.23 / 7.25** attended-time + atomic lifecycle~~ | **Done 2026-08-06** — crash hydration, shutdown close, configurable threshold; persist-before-mutate, one label, restored focus mode |
| — | ~~**7.12** finish the SQL aggregation~~ | **Done 2026-08-06** — four aggregates, no materialized predictions, no recap loop, query count pinned |
| — | ~~**10.8** make Review charts truthful~~ | **Done 2026-08-06** — fixed 0–100 axis, distinct no-data state, sampled-context labels |
| — | ~~**6.2** red-master rule~~ | **Done 2026-08-27** — [ADR-0008](adr/0008-protect-master-from-red-ci.md); `master` is protected, all fifteen CI contexts required, no review requirement |
| 3 | **Decision session B**: 4.11 | Settle title-parser behavior |
| — | ~~**9.13** orphaned `v0.2.0` tag~~ | **Done 2026-08-27** — first release is `v0.3.0`; orphaned tag left in place |
| — | ~~**7.16** timestamp representation~~ | **Done 2026-08-24** — schema v7, the IPC contract, and the frontend; 5.5/7.1/7.2 close with it |
| — | ~~**8.5** threat model~~ | **Done 2026-08-27** — [ADR-0009](adr/0009-local-first-threat-model.md) |
| 4 | **10.1 / 14.3** webview + command contract | Cover the real bridge and remove its parallel hand-maintained descriptions |
| 5 | **4.4 / 14.1 / 14.5** performance gates | Remove avoidable query work, then measure the storage lane and engine scheduler |
| 6 | **2.3 / Tier 13** model retraining | Resume only after a packaged trainer lands (ADR-0006); until then this is repository tooling |

**Eight items were opened on 2026-08-04** and are deliberately *not* in the table above,
because none of them displaces anything in it. They are listed here so they are findable:

| Item | `S`/`M` | One line |
|---|---|---|
| ~~**6.6** GCC-on-Windows CI job~~ | `S` | **Done 2026-08-06** — `windows-gcc`, verified locally at 376/376 on MinGW-w64 UCRT |
| ~~**9.13** orphaned `v0.2.0` tag~~ | `S` `decision` | **Done 2026-08-27** — `v0.3.0` is the first release tag; orphaned `v0.2.0` unchanged |
| ~~**12.7** ADR-0002's dead link~~ | `S` | **Done 2026-08-08** — Darwin-dev fact is inline; guard forbids citing the gitignored file |
| ~~**4.13** nothing watches the ONNX pin~~ | `S` | **Done 2026-08-08** — weekly pin-freshness job opens an issue, never a digest PR |
| ~~**11.9** capture invariant unverified~~ | `S` | **Done 2026-08-08** — second-thread sampler fails on inverted stores (MinGW 1157/200) |
| ~~**11.10** stale test registry key~~ | `S` | **Done 2026-08-08** — fixture sweeps `test-<pid>-*` whose process is gone |
| ~~**7.21** settings durability~~ | `S` | **Done 2026-08-07** — temp + directory durable flush after 7.19's atomic rename |
| **4.12** formatter + static analysis | `M` | Neither exists for either language |

**6.6 was the one worth doing early, and it is now done.** The others are hygiene; 6.6 was the
only one that would have *prevented* a defect that actually shipped to `master`, and 11.9 and
11.10 both get easier now that it exists.

**Three more were opened on 2026-08-05**, from reading the architecture rather than from
fixing anything. These are not hygiene — each is a hole in something the app already promises:

| Item | `S`/`M` | The hole |
|---|---|---|
| **2.7** missed-session nudge | `M` **decided** | ADR-0005 keeps declaration manual and answers forgetting with one latched prompt per active stretch |
| **9.14** no import path | `M` | Four exports, zero imports; local-only means nothing else holds a copy, so a new laptop loses everything |
| ~~**7.22** no pre-migration backup~~ | `S` | **Done 2026-08-05** — `VACUUM INTO` now creates a consistent pre-migration recovery file |

**ADR-0005 has now settled 2.7:** declaration stays manual, presence is measured, and forgetting
gets a nudge rather than an auto-started untagged session. **7.22 was the cheapest real safety
win and is now closed**: the backup is paid once per schema bump and gives a bad but
successfully-committed migration a recovery path.

**Two more on 2026-08-05, from reading the product rather than the plumbing:**

| Item | `S`/`M` | The hole |
|---|---|---|
| ~~**2.8** snapback has no "take me back"~~ | `M` | **Done 2026-08-14** — a "Take me back" action beside Dismiss raises the recorded window |
| **7.23** attended session time | `M` **decided / in progress** | ADR-0005 chose idle-driven spans; the schema/storage slice has landed, wiring and UI remain |

**ADR-0005 answers the shared 2.7/7.23 question:** a session is declared by the user and real
only while attended. The nudge preserves declaration; idle transitions open/close durable
active-time spans; elapsed time keeps its old meaning. **2.8 was independent** and acts
only on the user's click; it closed on 2026-08-14. The `session_spans` migration/storage API has landed; engine wiring,
reporting, and running/paused UI are still open until 7.23 closes.

**The 2026-08-05 cross-cutting audit opened eighteen assignable items and corrected stale
claims.** It reviewed production paths rather than counting files: each item below
has a concrete user failure, acceptance boundary, and dependency in its owning tier.

| Area | Items | What the pass found |
|---|---|---|
| Correctness | ~~**7.24–7.26**~~, **7.27**, ~~reopened **7.12**~~ | ~~Clock domains are mixed~~, ~~session/settings commands are not failure-atomic~~, capture semantics differ by OS, ~~and analytics still has unbounded/N+1 work~~ — only **7.27** remains |
| Release/privacy truth | **8.10**, **13.7** | A runtime font request contradicts local-only, while consumer Settings exposes a trainer absent from both this tree and an installed app |
| Architecture/performance | **14.5–14.6**, expanded **14.4** | Deadline wake and owned-job heartbeat remain; drain bound and Review-at-startup gating have landed |
| Product depth | **2.9–2.14** | History is not explorable, recording state is hard to see, repeat work is slow, onboarding stops before first value, Pomodoro is skeletal, and sessions cannot hold a reflection |
| Frontend/visual quality | **10.8–10.11** | Review charts mislead, Settings leads with internals, CSS tokens are incomplete/light-only, and Review cards describe different periods |

**A second 2026-08-05 pass opened ten additional assignable items without reusing those
eighteen.** This pass followed concrete user journeys through the live implementation: a
snapback firing, correcting a verdict, hiding and reopening the desktop app, locking the
machine, deleting private history, and starting against a mature database.

| Area | Items | What the second pass found |
|---|---|---|
| Product truth & control | ~~**2.15**~~, ~~**2.16**~~, **2.17** | ~~Snapback episodes are never persisted~~ (**done 2026-08-06**), ~~interventions have no delivery policy~~ (**done 2026-08-27**), and append-only labels cannot express an authoritative correction |
| Correctness & lifecycle | **7.28–7.29**, ~~**9.15**~~ | Editable goal-category names secretly change semantics, OS lock/sleep is treated as ordinary idle, and ~~the single-instance tray app has no activation/close contract~~ (**done 2026-08-27**) |
| Privacy completeness | **8.11**, ~~**8.12**~~ | App-only exclusions cannot redact one sensitive browser context; ~~“delete all” leaves personal exports plus full migration backups behind~~ (**done 2026-08-06**) |
| Desktop quality | **10.12** | Windows overlay placement ignores the tested multi-monitor geometry and fixed pixels ignore per-monitor DPI |
| Startup performance | **14.7** | Retention and a full `VACUUM` can block first paint before the webview even exists |

The ordering signal inside this batch was **2.15 → 8.12 → 14.7**: the first repairs a metric
already shown to the user, the second repairs a privacy action already promised to the user,
and the third removes repeat launch work after measuring it. **2.15's persistence half and 8.12
are done (2026-08-06); 14.7 remains**, and it is the one of the three that cannot start yet —
its acceptance names 14.5's deadline scheduler or 14.6's owned jobs, and neither exists. The
remaining items in this batch are parallel product/desktop candidates.

**A strict third pass opened eight more and then stopped.** Each survived a direct overlap
check against the first twenty-eight additions and against the implementation that landed
while this audit was running.

| Area | Items | What the strict pass found |
|---|---|---|
| Product action | **2.18–2.19** | Rules are global substrings typed from memory, and attended-time reporting has no optional plan to compare against |
| Local security | **8.13–8.14** | App-owned files inherit ambient permissions, while privileged webview commands survive top-level navigation |
| Data ownership | ~~**9.16**~~ | ~~“Export my data” silently caps history and can report no truncation after omitting windows~~ — **done 2026-08-06** |
| UX/data truth | ~~**10.13**~~, **10.14** | ~~Three incompatible row/session counts are called streaks~~ (**done 2026-08-06**); every document export/import still lacks a native picker |
| Model availability | **13.8** | Optional model-cleanup debris can prevent the core heuristic app from opening at all |

The correction order inside this group was **8.13 → 8.14 → 9.16 → 10.13**; **9.16 and 10.13 are
done (2026-08-06)**, leaving 8.13 and 8.14 at the front of it. **2.18–2.19** are
product candidates, **10.14** is desktop polish shared by several workflows, and **13.8**
either lands or disappears when **13.7** settles the trainer boundary. Generic scheduled
backups, cohort comparison, localization, and a separate notification-action item were
deliberately not opened: their useful scope is already owned, conditional on a later product
decision, or belongs as acceptance inside an existing item.

**Do not read the first pass as eighteen equal priorities.** The release-sized correction
queue is **7.24 → 8.10 → 7.23/7.25 → 7.12 → 13.7 → 10.8**. The rest are deliberately
assignable in parallel after their stated dependencies, with the Tier 2 additions serving as
product candidates rather than excuses to delay shipping.

**A rejected idea, recorded so it is not re-proposed.** "Hyperfocus nudges must be firing
falsely on overnight sessions" looked obviously true and is **false**: `update_break_state`
resets the break clock on any idle event past the threshold, so the nudge path is already
idle-aware. Only the duration is not. Checking it took one grep and would otherwise have
become a fix for a bug that does not exist — the third time this file has recorded that
pattern.

Beyond the correction queue above, most feature work is opportunistic. **Tier 9 is what turns
this from a correct program into a shippable product** — if the goal is "someone else uses
this," its remaining release-readiness items outrank most product-depth work. 9.1 was that
argument's headline item and is now done, which is what makes the blocker table below
meaningful.

**Next up is 3.3's external paperwork.** The Apple Developer account has independent lead
time, so its application should start now; it is the **only** formal blocker left, and the
small release security and data-integrity findings that used to run in parallel with it
(8.8, 8.9, 7.19, 7.20, 9.11) are all closed as of 2026-08-04.

The formal count is unchanged, but the August 5 audit added three **ship-before-publish**
findings outside ADR-0002: **7.24** (wrong production model inputs), **7.23/7.25** (attended
time and incomplete session failure semantics), and **8.10** (an undisclosed runtime network
request). **13.7**
must also stop the normal Settings UI from promising an impossible training path. The other
release work is **0.4b** (buy the signing certificate; the packaging defect itself is fixed).
**9.12** (project license and third-party notices) is done 2026-08-27.

**ADR-0002 release-blocker status as of 2026-08-01:**

| # | Blocker | State |
|---|---------|-------|
| 1 | **0.3** live-Mac capture | ✅ Done 2026-07-25 |
| 2 | **3.1** macOS tray + native `NSPanel` overlay | ✅ Done 2026-07-28 — verified by running the app |
| 3 | **3.3** macOS packaging + notarization | ⬜ **Next.** Longest lead time, needs an Apple Developer account. **Start the account application now**, since it gates nothing else but takes the longest — and it is what unblocks macOS notifications |
| 4 | macOS launch smoke in CI | ✅ Done 2026-08-01 — PR #40's hosted `macos-gui-smoke` passed with the other 14 CI jobs |
| 5 | **Decision session A** (5.3, 5.4, 1.2, 7.7, 7.18) | ✅ Done 2026-08-03 — [ADR-0004](adr/0004-verdict-and-opinion.md) |
| 6 | **7.3** schema migrations | ✅ Done 2026-07-29 — `user_version`, an ordered migration list, and a downgrade guard |

Note the shape of the accepted ADR: **five of six are verified done and the sixth is
paperwork.** No design question remains *inside that formal list*. The code audit findings
above are separate release gates discovered later; recording them here does not rewrite an
accepted ADR or pretend the newly observed defects were part of its original six.

---

## Tier 6 — CI health

Opened by the 2026-07-20 staff review against run `29728565319`, when this tier was titled
"CI is red (blocking)". **It is not red any more:** 6.1, 6.3, and 6.4 are all done and
CI-confirmed, and run `30168981559` (2026-07-25) was green on all three OSes. Retitled
2026-07-29 so the heading stops claiming a blocking outage that ended three days earlier.
**This tier is now closed:** 6.2 was the last item, and it was a process decision — what to
do when master goes red — rather than a CI failure. It closed 2026-08-27.

- **6.3 — The `desktop-app-build` guard silently stops running when CI is red.** `proposed` `S`

  In run `29728565319`, `Desktop app build` and `Windows desktop integration smoke` both
  show **skipped**, because they `needs:` jobs that failed.

  That job was added on 2026-07-20 specifically because the desktop app had *never* been
  linked off Windows and no CI job built it (see the P0 entry in the [archive](roadmap_archive.md)). It has
  therefore barely run since it was created. **A guard that only executes when everything
  else is already green does not guard the case it exists for.**

  The desktop build doesn't depend on the headless suite passing — it depends on the code
  compiling. Decouple the `needs:` graph.

  > **Decoupled 2026-07-22, awaiting CI confirmation.** Both `windows-desktop-integration`
  > and `desktop-app-build` lost their `needs:` — they now run unconditionally, with a
  > comment in `ci.yml` explaining why they must never regain one. Cost: they burn runner
  > minutes even when the core is broken; that is the point — broken core is exactly when
  > the desktop guard's answer matters.
  >
  > **And the guard's first real run immediately earned its keep:** once 6.1 unblocked it,
  > `desktop-app-build / ubuntu-latest` failed for the first time ever — X11 headers
  > (pulled in via webview → GTK → GDK) `#define KeyPress`, `KeyRelease`, `None`, `Status`
  > as bare macros, clobbering `EventType::KeyPress`, `snapback::Status`, and every
  > `::None` enumerator at parse time. Fixed the same day: `src/app/webview_compat.hpp` is
  > now the only legal include site for `webview.h` and scrubs the macro pollution right
  > after the include (same pattern as `tests/doctest_wrapper.hpp`). Verified to link on
  > macOS; Ubuntu is CI-verified only, so the next master run is the proof.
  >
  > **CI-confirmed 2026-07-25** by run `30168981559`, green on all three OSes for every job
  > but `docs-smoke` — which covers both halves: the decoupled jobs ran, and the Ubuntu
  > desktop build linked. This item is **done**; it stays here rather than moving to the
  > archive because the X11 lesson above is still the reason `webview_compat.hpp` exists.

- **6.5 — MSVC warning noise obscures real diagnostics.** `proposed` `S`

  Every Windows build emits C5285 (`cannot declare a specialization for 'std::tuple'`) from
  `doctest.h`, once per translation unit. Third-party, not ours — but it buries our own
  warnings, which is part of why 6.1 took a crash to surface rather than inspection.
  Suppress at the include site.

  > **Done in code 2026-07-22, awaiting a Windows CI log to confirm the spam is gone.**
  > All 24 test TUs now include `tests/doctest_wrapper.hpp`, which wraps
  > `<doctest/doctest.h>` in a `#pragma warning(disable : 5285)` push/pop under `_MSC_VER`.
  > One include site, third-party noise only — our own C5285s would still fire.

---

## Tier 0 — Finish the port's last gaps

- **0.4b — Provision the signing certificate.** `proposed` `S` (external dependency)
  **Still open, but only on the external half.** The code defect described below was fixed on
  2026-08-04: `package_windows.ps1` now signs `snapback.exe` immediately after the build and
  before CPack, signs the IExpress installer after it exists, and then **verifies the artifact
  it is about to upload** — it extracts the ZIP and requires the `snapback.exe` inside to be
  validly signed *by the passed-in thumbprint*. [PACKAGING.md](PACKAGING.md) documents the
  order and the verification path.

  **Checking the thumbprint, not just the status, is the part that matters.** A
  `Get-AuthenticodeSignature` status check alone passes for anything validly signed by
  anyone; a stray Microsoft-signed binary satisfies it. That is not hypothetical — the first
  version of this verification was written status-only and its own negative test passed
  against `where.exe`, which is Microsoft-signed. Both failure modes are now exercised
  against the real script text: an unsigned binary in the ZIP is rejected as `NotSigned`, and
  a validly-signed binary from a different certificate is rejected on the thumbprint.

  **What remains is the certificate itself.** The success path has never executed, because no
  EV certificate exists to run it with. Until a signed build has been produced and verified
  end to end, README must keep describing Windows signing as wired but incomplete — it
  currently does. Buy the cert, set `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT`, cut one release,
  and confirm the verification step passes; only then is this done.

  The original finding was:

  `package_windows.ps1` creates the CPack ZIP and embeds that ZIP in the IExpress installer
  *before* it signs the build-tree `snapback.exe`. The uploaded ZIP and installer payload
  therefore still contain the unsigned executable even when the secret exists; only the
  loose build-tree binary and the outer installer receive signatures.

---

## Tier 7 — Correctness & product findings (2026-07-20 staff review)

Covered in the review: `state.cpp`, `classifier.cpp`, `storage.cpp`, `capture_thread.cpp` +
`ring_buffer.hpp`, `tracker.cpp`, `title_parser.cpp`, the IPC/eval boundary, `main.cpp`, and
the frontend XSS surface. **Not covered — un-reviewed, not clean:** `features.cpp` extraction
maths, ONNX internals, the Windows overlay/tray implementations, frontend component
internals, and the benchmark harness.

### Correctness

- **7.2 — PARTLY STALE, corrected 2026-07-30.** `in progress` `S` The UTC-bucketing half **was already
  fixed** and this entry never said so: `AppState::analytics()` calls
  `local_hour_from_rfc3339(prediction.timestamp)`, not the character-slicing `timestamp_hour()`
  this text describes. Found while picking work off this file — the third time an item here has
  described a gap that the code had already closed (see 0.3 and the note on trusting this file).

  **Still open** is the second half below: `cutoff_unix_ms()` treats "1 day" as a rolling
  24 hours rather than the user's calendar day. The Review surface now *labels* its windows
  "Last 24 hours" / "Last 7 days" (9.7), so the UI is honest about it; whether the underlying
  window should change is a product decision, not a bug.

  The original finding was:

  `timestamp_hour()` (since removed; see the correction above) sliced characters 11–12 out of strings built by
  `now_rfc3339()` (`state.cpp:now_rfc3339`), which uses `gmtime_r`/`gmtime_s` and appends `Z` — UTC.
  So `AnalyticsHour::hour` is a UTC hour rendered as the user's hour. In US Pacific that is
  an 8-hour lie: "you focus best at 14:00" means 06:00 local.

  Storing UTC is correct; *presenting* it is the bug. Recommend converting in the frontend —
  timestamps are ISO-8601 with `Z`, and `new Date(ts).getHours()` is exactly right.

  Related: `cutoff_unix_ms()` (`state.cpp:cutoff_unix_ms`) computes "1 day ago" as "24 hours ago," so the
  "daily" summary is a rolling 24 h window, not the user's calendar day. Possibly intended,
  nowhere written down, and users read "day" as "today."

- **7.27 — Define and test one capture-event contract across platforms.** `in progress` `M` for Windows +
  macOS; `L` including Linux
  Opened 2026-08-05. The extractor assumes `CaptureEvent` has portable meaning, but each
  backend currently invents a different one. Windows treats every non-move mouse message as a
  click, including button-up and wheel traffic. macOS writes mouse speed as zero. Linux maps
  every `EV_KEY` press, including mouse buttons, to `KeyPress`, leaves kinematics empty, and
  can query foreground context through `sh`/`xdotool`/`ps` inside the input loop.

  The result is model drift by operating system: macOS has effectively dead mouse features,
  Windows over-counts clicks, and Linux can mix mouse buttons into typing while spawning work
  proportional to input volume. **0.3** proved live macOS delivery and **11.3** starts after a
  normalized event already exists; neither tests this boundary.

  **Windows mouse-speed conversion landed 2026-09-21** (Astra review slice 6). `mouse_proc`
  now measures movement with a steady-clock duration instead of reusing the millisecond event
  timestamp. The pure translation seam widens coordinates before subtraction, floors a zero or
  invalid interval, and clamps non-finite or out-of-range speeds before converting to the
  `CaptureEvent` `uint32_t` field. Tests cover ordinary movement, equal timestamps, large
  displacement, and the signed coordinate extremes. The remaining 7.27 work is the
  cross-platform event contract and provider-boundary work described below.

  Introduce pure per-platform translation fixtures against one documented event contract:
  one click per button-down, explicit wheel semantics, consistent speed units, and identical
  key/button classification. No raw callback or device-read loop may launch a child process;
  foreground context must be an injectable, cadence-bounded provider. Add live platform
  distribution smokes (expected nonzero fields). Over a fixed-duration 10,000-event burst,
  context-probe count must stay within the provider's cadence bound (plus initialization) and
  remain effectively the same as a zero-event control of equal duration; event count itself
  cannot increase probe count.

- **7.28 — Separate goal-category identity from editable display text.** `proposed` `M/L`
  Opened 2026-08-05. `GoalCategoriesCard` can edit only the rows it receives: there is no Add,
  Remove, Disable, or explicit Reset. Saving an empty list is also not stable — the getter and
  scorer silently resurrect the built-in defaults. More seriously, context compatibility
  infers semantics from category-name substrings such as `research`, `read`, `commun`, and
  `meeting`. Renaming a visible “Research” category to “Study” can change classification even
  when every keyword stays the same.

  Give built-in semantic categories stable ids/kinds independent of their display names, and
  make custom-category behavior explicit rather than guessing it from text. The UI must support
  add, remove, disable all, and **Restore defaults**, with a live preview explaining how a sample
  goal/context will score. Empty/disabled must remain empty/disabled after restart; defaults
  return only through the explicit reset action.

  Migrate recognized existing names without changing their behavior and surface ambiguous
  custom names for review. A rename-only test must leave scores byte-for-byte unchanged; add
  C++/JSON/frontend round trips for create/remove/disable/reset and include the new stable fields
  in feature-parity/model-contract review. Settings publication follows **7.26** so disk and live
  classification cannot disagree.

- **7.29 — Treat screen lock, suspend, wake, and unlock as first-class lifecycle events.** `proposed` `M/L`
  Opened 2026-08-05. The only lifecycle signal today is ordinary input inactivity, so a locked
  or sleeping machine remains “attended” until the five-minute idle threshold expires. There
  are no Windows session/power or macOS workspace sleep/wake adapters. Capture permissions,
  hooks, Pomodoro deadlines, and notification delivery are therefore all left to whatever the
  next 100 ms tick happens to observe after resume.

  Normalize native events into **Locked**, **Suspending**, **Resumed**, and **Unlocked**. Lock or
  suspend immediately closes **7.23**'s attended span, suppresses context/prediction processing
  and every intervention, and checkpoints the minimum safe state. Wake revalidates/re-arms the
  capture backend but remains paused until genuine post-unlock input; it must not manufacture
  an attended interval or replay a burst of expired notices.

  Injected lifecycle tests must cover duplicate and out-of-order events, sleep spanning a day/
  DST boundary, lock during persistence, shutdown while suspended, and each Pomodoro policy in
  **2.13**. The platform adapters stay thin over one testable state machine. Build on **7.23**,
  **7.24**, and preferably **14.2**; Linux may begin with a truthful unsupported/stub state if
  its desktop-session contract is deferred beyond v1.

### Decisions — do not code these yet

- **7.8 — `set_focus_mode` permanently rewrites the user's default.** `proposed` `S` `decision`

  `set_focus_mode()` (`state.cpp:set_focus_mode`) sets the live mode *and* writes
  `settings_.default_focus_mode` to disk on every call. Switching to Recovery once for a
  rough afternoon makes Recovery the startup default forever — silently overwriting the
  answer the onboarding wizard (1.1) explicitly asked for.

  Decide whether "current mode" and "default mode" are one setting or two. They're currently
  one; the wizard's existence implies two.

### Product gaps

### Observability & test coverage

### Performance

### Hygiene

---

## Tier 8 — Security hardening (2026-07-20 staff review)

**No exploitable vulnerability was found.** Except for 8.1 — a real availability bug in
normal use — these are defense-in-depth and fragility items.

**What is already right**, recorded so a future review doesn't re-derive it: every SQL
statement is parameterized (no string-built queries in `storage.cpp`); no `innerHTML`,
`dangerouslySetInnerHTML`, or `eval()` in the frontend; the `popen`/`std::system` call site
in `permissions.cpp:command_available` takes a compile-time literal;
`training_deploy.cpp` quotes the user-supplied repo path; `npm audit --production` reports 0
vulnerabilities and the `security-audit` job is green; and the hook callback correctly
swallows all exceptions (`capture_thread.cpp:record_failure`) since unwinding through an OS callback is UB.

> **This list went stale in the unsafe direction on 2026-07-25, which is worth recording as
> a pattern.** It used to say `active_window.cpp`'s `run_command()` also took a compile-time literal. It no
> longer does: `626ad87` changed `run_command()` from `const char*` to `const std::string&`
> so it could build a per-browser `osascript` command, and that command is chosen by the
> foreground **app name, which comes from the OS**. The code is still safe — it interpolates
> the matched allowlist *literal* rather than the caller's string, with a comment saying
> exactly why — but the property holding it safe changed from "the type system makes this
> impossible" to "a loop is careful," and nothing but that comment now enforces it.
>
> The general lesson: **a "what is already right" list is a claim with an expiry date.** It
> reads as reassurance, so it is the least likely thing in this file to be re-checked, and
> `check_doc_paths.py` cannot catch it — the path still exists, only the claim about it
> died. Re-verify this block whenever a signature it names changes.

- **8.11 — Add scoped capture rules before sensitive window titles reach the pipeline.** `proposed` `M/L`
  Opened 2026-08-05. Privacy currently offers one global switch and a list of app-name
  exclusions. `is_private_event_unlocked()` checks only `event.app_name`; a user who wants to
  hide one banking, health, password-manager, or client browser context must therefore exclude
  the entire browser. Otherwise the raw title can flow through features, snapback summaries,
  context storage, exports, and notification copy.

  Add ordered rules scoped to an app plus an optional title/context matcher, with explicit
  actions **Capture full context**, **Capture app only**, and **Exclude**. Match at the capture
  boundary and pass only the sanitized event downstream; a redacted title must never survive
  in a queued event waiting to be persisted later. Use bounded literal/glob matching rather
  than an unbounded regex engine. Domain matching is allowed only on platforms where **7.27**
  establishes a real domain field — do not infer a URL from a tab title.

  Migrate today's `excluded_apps` entries to app-scoped Exclude rules. The editor must preview
  scope and precedence without writing new raw examples to disk. A secret-string fixture must
  prove that a sensitive tab is dropped or app-only while an ordinary tab in the same browser
  still records, and that the secret appears nowhere in the database, emitted events, logs,
  notifications, or every export. Build after **7.26** makes the setting atomic and align the
  storage boundary with the threat model in **8.5**.

- **8.14 — Confine privileged webview commands to the trusted packaged UI.** `proposed` `M/L`
  Opened 2026-08-05. **8.4** locks down the initial release URL and **8.3** constrains scripts
  loaded by the bundled page. Neither handles a later top-level navigation. `kIpcShim` is
  installed with `webview.init()`, whose own comment says it runs before scripts on **every**
  navigation, and every native command is bound globally. A remote page reached by a redirect
  or future Help link can therefore inherit deletion, export, settings, and training commands.

  Define one trusted main-frame origin for release assets and enforce it at navigation and
  command invocation boundaries. Block remote top-level navigation inside the privileged
  webview; explicit external links open in the system browser with no native bridge. A Debug
  loopback exception is allowed only behind the existing build-time debug gate. Do not rely on
  CSP as an origin check — CSP governs resources inside the trusted document, not which
  document owns the native bindings.

  Use **10.1**'s real-webview harness for an adversarial test: navigate/redirect to a malicious
  page and prove it cannot invoke a sentinel mutation, then prove the packaged page still can.
  Cover subframes, redirects, `file:` path aliases, navigation races, and user-initiated
  external links. Align the external opener with **2.8**'s no-shell platform adapters and the
  security policy with **8.5**.

---

## Tier 1 — Ship a polished Windows-first v1

No open items. Completed work is in the [archive](roadmap_archive.md).

---

## Tier 2 — Product & ML depth

- **2.9 — Turn session history into a real session explorer.** `in progress` `M/L`
  Opened 2026-08-05. Past sessions currently appear chiefly inside the destructive "Delete a
  session" area, history is capped at 20, and the context timeline is tied to the current
  session. The app records the detail needed to explain a workday, but Review cannot answer
  the basic question "what happened in that session last Tuesday?"

  Add ordinary selectable history, separate from deletion. A detail view should combine the
  goal/mode/times, recap, focus curve, snapbacks, captured context, and labels for any chosen
  session; support goal/app/date search, mode/verdict filters, and cursor-based pagination.
  Deletion stays a secondary confirmed action, and "repeat this goal" should hand off to the
  deliberate start flow rather than begin recording on selection. Do not load the entire
  database into the browser. This needs per-session query commands and should follow **7.16**
  and align with **14.3**'s command contract.

  *Progress 2026-09-22 — first version, scoped with Kassa after comparing Rize, Timing,
  ActivityWatch and Session, which all browse history as a list or timeline with a detail
  panel and none of which lead with search:*
  - **A Sessions card on Review** (`frontend/src/SessionExplorerCard.tsx`) lists the range's
    sessions newest first, grouped by day. Choosing one opens its detail: mode and times,
    attended time, average focus, deep-work share, snapbacks, the reflection, a focus curve
    with its data table, and the apps it was spent in.
  - **One new native command,** `get_session_focus_curve`: that session's predictions folded
    into up to 240 equal slices of its own span, one grouped statement over
    `idx_predictions_session_ts`. Context comes from the existing per-session
    `get_context_timeline` (first 500 rows, said so when capped).
  - **"Start this again"** fills the start form on Now and goes there; nothing records until
    Start (ADR-0005), and it is disabled while a session runs. **Delete** is in the detail
    behind a confirmation; Session management keeps its own delete and reflection editing.
  - Tests: `frontend/tests/sessionExplorerFlow.test.tsx`, and the storage slicing in
    `tests/test_storage.cpp`.

  Still open from the item as written: goal/app/date search, mode/verdict filters, paging past
  a range's 500-session cap, and labels (with **2.17**).

  *Progress 2026-09-25:* The Windows QA polish pass moves the existing Sessions explorer
  ahead of the charts on Review. The same bounded history and selected-session detail remain;
  search, filters, pagination, and the editable label ledger remain open.

  *Progress 2026-09-25:* Review now selects the newest completed session in its loaded range
  and shows its longest recorded snapback detour, with a return destination only when recorded.
  The selected session's context timeline lives in its detail instead of the range-wide story;
  live prediction history moves to Settings → Advanced. These reads add no schema migration.
  Search, filters, pagination, and the label ledger remain open.


- **2.17 — Give feedback an authoritative, editable label ledger.** `proposed` `M/L`
  Opened 2026-08-05. Auto labels, the end-session check-in, and live verdict corrections all
  call append-only `insert_label()`. There is no list/update/supersede command, live feedback
  is attached only to a session rather than an exact prediction, and training export writes
  every row. A user-facing “Override” can therefore add a conflicting label without defining
  which one is truth.

  Define label scope and precedence: prediction-scoped corrections versus session-scoped auto
  and survey labels. Keep an append-only audit trail with stable ids and `supersedes` links,
  but expose one effective-label view; the survey supersedes the automatic session label and
  retrying the same submission is idempotent. Existing conflicts need a deterministic migration
  rule rather than “last row happened to win.” This fulfills the label-idempotence prerequisite
  identified by **13.5** without pre-deciding whether there is enough data to train.

  In **2.9** show label, scope, source, time, and provenance with Amend/Undo actions. Live
  feedback must target the prediction the user actually saw, even if a newer prediction has
  arrived. Training export emits effective labels only, while the personal export may retain
  the audit trail. Tests must cover repeated clicks, auto→survey supersession, two corrections,
  undo, session deletion, and export. Coordinate lifecycle writes with **7.25**.

- **2.18 — PARTIALLY LANDED 2026-08-14; the scoping half stays open.** `in progress` `M`
  What landed: one-click `+ Allow` / `+ Block` beside observed apps in the Now context timeline
  and the Review top-apps breakdown, with a badge where a rule already matches. That closes the
  "navigate to Settings and type a substring from memory" complaint.
  **What did not:** `AppRuleRecord` still carries only `pattern`, `rule_type`, and `note`, so
  every rule created this way is still a global substring — the actual defect below. No explicit
  scope, no match-count preview, no Undo, no conflict precedence. The goal-scoped variant cannot
  be built until **7.28** supplies a stable goal-category id, and 7.28 is open.
  Opened 2026-08-05. Personal Rules currently asks the user to navigate to Settings and type a
  substring from memory. That substring matches both app name and title, and every rule is
  global. Meanwhile verdict feedback and the Review timeline already hold the exact app,
  title, file, project, and session goal that caused a wrong reading.

  Add **Usually on task** / **Usually distracting** actions beside a live correction,
  distraction episode, and context row. Before saving, require an explicit scope — exact app,
  title pattern, or app plus stable goal-category id from **7.28** — and preview how many stored
  examples would match. Existing rules remain global; this adds a narrower tool rather than
  silently changing their meaning. One-click means one place to start, not an unreviewed rule.

  Save applies to the next classification tick and offers Undo that restores the exact prior
  rule set. Pin conflict precedence when a global and goal-scoped allow/block both match, and
  prove the same Slack/Chrome context can resolve differently for Coding and Communication.
  State clearly that this tunes classification; it neither blocks applications nor redacts
  capture (that is **8.11**). Depends on stable context identity in **7.27**, stable category
  identity in **7.28**, and title-parser policy in **4.11** for title-derived suggestions.

- **2.3 — Model retraining loop.** `accepted` `L` — **blocked on 13.7.**
  The intended loop is exported CSV + the user's labels → a fresh `model.onnx`, opening the
  door to on-device personalization. The earlier entry assumed an `ml/` trainer was present;
  the 2026-08-05 audit found that directory absent while the UI still requires it. **13.7 must
  first decide whether this is a packaged user feature or developer tooling.**

  **Do not start this as one item — it is at least seven.** The operational half (versioning,
  evaluation gates, rollback, drift, and whether enough labelled data even exists) is broken
  out in **Tier 13**, and **13.5 may rescope 2.3 entirely** if the label corpus turns out too
  small to train on. Also: bundle **5.6** here (it needs both extractors changed together
  plus a retrain), and fix **7.5** first or the corpus stays biased toward
  deliberately-ended sessions.

---

## Tier 3 — Cross-platform breadth & packaging

- **macOS launch smoke in CI — DONE, HOSTED 2026-08-01.** `S` — fourth ADR-0002
  blocker. PR #40 ran `macos-gui-smoke` successfully on GitHub's macOS runner, alongside 14
  other passing jobs.
  `scripts/gui_smoke_macos.sh`, wired as the `macos-gui-smoke` job. `desktop-app-build`
  proved the macOS binary *links*; nothing proved it *starts*, and both ways it can fail to
  start are invisible at link time (a webview that cannot create its window, and a missing
  frontend bundle, which renders an empty window rather than an error).

  The script reuses `main.cpp`'s existing `SNAPBACK_GUI_SESSION_SMOKE` hook, so it is a real
  round trip — a session started and stopped through `AppState` and SQLite from the UI
  thread — and then requires the run loop to exit on its own, which is the same path the
  tray's Quit item drives. It first passed on Kassa's Mac and is now confirmed in hosted CI.

- **3.2 — Linux tray + overlay.** `proposed` `M`
  `libappindicator` tray + an overlay window (X11/Wayland caveats noted). Since 3.1 landed,
  Linux is the **only** remaining user of `src/app/tray_stub.cpp` and
  `src/snapback/overlay_stub.cpp`; both now guard on `!_WIN32 && !__APPLE__`. Read
  `src/app/tray_macos.mm` and `src/snapback/overlay_macos.mm` first — they are the worked
  example of replacing these two stubs, including the shared `tray_menu_entries()` model a
  third platform should reuse rather than re-list.

- **3.3 — macOS packaging.** `accepted` `L` — `.app` bundle + notarization + DMG.

- **3.4 — Linux packaging.** `proposed` `M` — AppImage and/or Flatpak.

- **3.5 — In-app "check for updates".** `proposed` `M`
  Fetch a version manifest and offer a download link — no silent install. The lightweight
  variant of the auto-updater deferred in [PACKAGING.md](PACKAGING.md).

- **3.7 — Snapback as a real web product.** `proposed` `XL` `decision` **stated goal, not scheduled**
  Opened 2026-08-27 because it is a direction the project is aimed at, and an undocumented
  ambition turns into an accidental architecture. **3.6's demo is not a step toward this** —
  it is a static page with invented data, deliberately so.

  The blocker is not effort, it is physics: a browser tab cannot enumerate other applications'
  windows or read OS-level input idle time, and those two signals are the entire input to the
  feature vector. So there is no version of this where the browser replaces the capture agent.
  Every real design keeps a native agent on the machine and moves *storage and presentation*
  off it, which is a different product with a different privacy contract.

  What has to be settled before any code, and why each is load-bearing:

  - **It contradicts an accepted promise.** The Privacy card says "Nothing leaves this device",
    [ADR-0002](adr/0002-v1-supports-windows-and-macos.md) scopes v1 to two desktops, and 8.10
    made release builds network-silent on purpose. A sync target is a new ADR that supersedes
    part of that, not a feature added underneath it.
  - **Accounts and a server** mean auth, multi-tenancy, and an operational cost this project
    has never had. It also makes 8.5's threat model a prerequisite rather than a parallel item.
  - **What actually syncs.** Window titles are the most sensitive rows in the database. A
    defensible first version might sync only aggregates — scores, durations, session goals —
    and keep raw context local. That choice defines the whole schema.
  - **Which half owns the verdict.** Classification currently runs in-process against live
    capture. Moving it server-side changes latency, the snapback path, and what an offline
    machine can still do.

  A staged path exists if this is wanted sooner: read-only first. The desktop agent pushes the
  aggregates 7.12 already computes; the web app renders Review only, with no live Now surface
  and no controls. That version needs no capture in the browser, no round trip on the alert
  path, and can be switched off without the desktop app noticing. Depends on **8.5**, a new
  ADR, and — realistically — on v1 shipping first.

---

## Tier 5 — Open findings from the 2026-07-20 engine/storage audit

**Checking each one before fixing it changed the answer twice.** 5.4 and 5.6 turned out to be
deliberate behaviour rather than defects — and 5.6 would have failed the feature-parity
golden test had it been "fixed" unilaterally. Both are now decision items. **An audit
finding is a hypothesis; verify it before writing code.**

Done: 5.1, 5.2, 5.7, 5.8, 5.9 (details in the [archive](roadmap_archive.md)).

- **5.6 — `longest_active_stretch_5min` reports 300s for brand-new sessions.** `proposed` `M` `decision`
  — **do not "just fix" this; it will fail CI. Bundle into 2.3.**

  `features.cpp:extract` defaults to the whole 5-minute window when it holds no idle events, so
  ten seconds into a session the extractor claims a five-minute unbroken active stretch.

  1. It is long-standing, deliberate behaviour, not an accident.
  2. **The feature-parity golden test pins every key of the feature vector**
     (`tests/test_feature_parity.cpp`). Changing this without updating the golden fails it.

  Defensible as-is: the feature is defined over a fixed window, not the session. The real
  question is whether a feature that is constant-300 for most users carries signal at all —
  which is a 2.3 question, since answering it means changing both extractors and retraining.

---

## Tier 4 — Engineering quality & hardening (cross-cutting)

- **4.11 — `title_parser` fabricates filenames.** `proposed` `M` `decision`

  Two distinct defects, one root cause — `parse_title` has no notion of "does this look like
  a filename?":

  1. **Separator case.** It splits on `" — "` / `" - "` and treats segment 0 as a filename
     with no check. `"Some Article - Google Chrome"` yields `file_hint = "Some Article"`, and
     `tracker.cpp:make_snapshot` turns that into **"Editing Some Article"**.
  2. **No-separator case (worse).** `title_parser.cpp:parse_title`:
     `if (hints.file_hint.empty()) hints.file_hint = window_title;` — with no separator at
     all, **the entire title becomes the file hint.** `build_snapback()` (`tracker.cpp:build_snapback`)
     then renders `"Return to " + file_hint`, so a fullscreen video titled
     `Top 10 Productivity Fails` produces the overlay **"Return to Top 10 Productivity
     Fails"** — the product's namesake feature telling you to go back to the distraction.

  **Needs a decision first:** this behaviour is long-standing, so changing it is a deliberate
  break with how the app has always worked, not a bug fix. Cheapest fix is to consult `title_is_distracting`, which `app_context.cpp:classify_app_context`
  already computes and `make_snapshot` ignores.
  *Note: the parser takes no `app_name`, so per-app title conventions are
  currently unimplementable. The decision should settle whether to add it.*

- **4.2 — Fuzz the untrusted boundaries.** `proposed` `M`
  libFuzzer targets for `title_parser`, the JSON IPC arg parsing, **and the Windows shell
  quoting in `training_deploy.cpp`** — `cmd.exe` metacharacter handling (`^`, `%VAR%`, `&`,
  embedded quotes) is a genuinely different escaping problem from POSIX `sh`, and the same
  `quote()` serves both (`training_deploy.cpp:quote`). `%` in particular is not neutralized by
  double-quoting in `cmd.exe`. Low severity (self-injection from a user-entered path), but
  it's the natural third target.

  **Consider instead:** building the process directly (`CreateProcessW` / `posix_spawn`) with
  an argv array removes the entire quoting problem class rather than escaping it correctly.
  *The parser's manual index math is exactly what fuzzing should hammer.*

- **4.3 — Opt-in crash reporting.** `proposed` `M`
  Windows minidump capture on unhandled exceptions, written locally, opt-in only. Note 8.1
  reduces how often this fires; do 8.1 first.

- **4.4 — Perf regression gate.** `accepted` `M`
  Profile `engine_tick` allocations (the compute path already defers work — measure it), then
  add a threshold to the benchmark harness so a regression fails CI. **7.12 is its natural
  first benchmark.** *See [benchmarking.md](benchmarking.md).*

  > **Partly advanced 2026-08-01.** The hot-path harness now measures both `health()` and a
  > complete live-read set under adversarial engine contention, with same-host baselines in
  > `benchmarking.md`. The item remains open because CI still checks only that benchmarks run;
  > it does not compare results to a threshold.

- **4.5 — Optional encryption at rest.** `proposed` `M`
  Optional SQLCipher for the local DB. *(The schema-versioning half of this item was split
  out and promoted to 7.3.)* **Whether this is required at all is decided by 8.5** — don't
  build it before the threat model exists.

- **4.12 — There is no formatter and no static analysis, for either language.** `proposed` `M`
  Opened 2026-08-04. The repository contains no `.clang-format`, no `.clang-tidy`, no
  `.editorconfig`, and no linter gate for the frontend. Style is currently maintained by
  attention alone across ~40 C++ translation units and a React app.

  **Do the formatter first and expect one enormous diff.** That is the whole cost, and it is
  paid once: every subsequent diff stops carrying whitespace noise, which is what makes small
  behavioural changes reviewable at a glance. Pick the settings to match what the code already
  looks like (100-column, 4-space, attached braces) so the reformat is close to a no-op.

  `clang-tidy` is the more valuable half and the more disruptive one, so scope it
  deliberately: start with `bugprone-*` and `performance-*` on `src/` only, warnings not
  errors, and promote to a gate once the backlog is empty. Turning on everything at once
  produces thousands of findings and teaches everyone to ignore the tool.

  **Not release-blocking**, and worth weighing against 6.6 — a compiler that actually catches
  real portability defects has already proven more valuable here than a style checker would
  have.

---

## Tier 9 — Ship a v1 (release readiness)

**The gap this tier closes: there is no written definition of "shipped."** Tiers 0–8 are all
"make the thing correct." This is "make the thing releasable to a stranger." Every item was
scoped by walking the lifecycle a real user goes through — install, first run, daily use,
upgrade, failure, uninstall — and asking what's missing at each step. Most of these are
small; the tier is large because nobody has walked that path yet.

- **9.4 — Walk the upgrade path once, deliberately.** `proposed` `M`
  Nobody has ever installed version A and then upgraded to version B. Unknowns worth
  resolving before a stranger hits them: does the DB survive through 7.3's migration runner;
  does the HKCU Run key survive a reinstall to a new path, or does autostart silently point
  at a deleted binary; do settings persist; does a running instance get replaced cleanly?

  **The ZIP install path works now; the upgrade walk is still unwalked.**
  `install_windows_package.ps1` used to pass a wildcard to `Copy-Item -LiteralPath`, the one
  parameter that does not expand one — it copied nothing, so every install failed its own
  `snapback.exe` check. Fixed 2026-08-29 by enumerating the extracted root and copying each
  entry literally. **CI still validates the ZIP without ever installing it**, which is why a
  script that could never have worked sat here unnoticed; an install smoke test belongs with
  this item. Then install v0.2.0, create recognizable
  data, upgrade to the candidate, and prove the executable, autostart path, settings, and
  `focoflow.db` are discovered and migrated rather than appearing lost under the C++ app's
  current data-directory rules.

  **The installer to walk is now the NSIS one.** The IExpress self-extractor that wrapped
  `install_windows_package.ps1` is gone — it never built on a GitHub-hosted runner (see
  [docs/PACKAGING.md](PACKAGING.md)), so there was never anything to install. NSIS installs the
  package directly and brings its own uninstaller, which is also what 9.5's open wiring needs.
  `install_windows_package.ps1` still stands for people who take the ZIP instead.

- **9.5 — DECIDED AND IMPLEMENTED 2026-08-09; wiring the installer stays open.** `in progress` `S`
  Decide and implement what uninstall removes. Today it plausibly leaves behind: the
  `focoflow.db` with full window-title history, the HKCU Run key (a startup entry pointing at
  a deleted binary), the log files and rotated backups, and the exported training CSVs. For a
  keystroke-recording app, **leaving the database behind after uninstall is the worst of the
  four** — the user believes they removed it. Ties to 7.6 and 8.5.

  **The decision: uninstall removes all of it, database included.** Not for tidiness — because
  a person who uninstalls an application that recorded their window titles believes they have
  removed what it recorded, and leaving `focoflow.db` makes that belief false without telling
  them. Everything else follows from the same rule: settings, logs and their rotations, every
  export, the model, SQLite's `-wal`/`-shm` companions (which hold recent writes, and so recent
  window titles), every pre-migration backup, and the start-on-login entry.

  **Done:** `src/app/uninstall.hpp` enumerates it and `purge_app_data` removes it, reporting
  per-item what went and what did not through 8.12's `ActivityDeletionResult` rather than a
  parallel shape — the question afterwards is identical. Reachable as `snapback --purge`, which
  exits non-zero on a partial purge so a caller cannot report a clean removal it did not
  achieve. Deliberately **not** `delete_all_activity_data`: that one keeps the database file,
  the settings and the model because the app keeps running: uninstall has no afterwards.
  Two boundaries are pinned by test: files that merely look like ours (`snapback.log.bak`) are
  left alone, and the data directory itself is removed only if empty, so someone who pointed
  `SNAPBACK_DATA_DIR` at a folder of their own keeps what is theirs. An empty data directory
  reports a failure rather than a silent success, because "nowhere to look" is not "nothing to
  remove".

  **Still open:** the Windows uninstaller does not call it yet — `package_windows.ps1` builds
  an IExpress installer, and wiring an uninstall hook to run `snapback --purge` before deleting
  the binary is packaging work that belongs with **3.3**/**3.4**. macOS and Linux have no
  uninstaller at all yet. The command exists and is the single implementation each will use.

- **9.6 — Failure UX: what does the user actually see when it breaks?** `in progress` `M`
  The backend reports several rich failure states (7.4, 7.10, 8.1), but there is no designed
  response to any of them. Specify what the UI does when: capture permission is revoked
  *mid-session* (macOS lets the user do this at any time); the hook dies; the disk is full so
  writes fail; the DB is locked by another instance; predictions have gone stale. Right now
  most of these render as a dashboard that simply stops updating, which is
  indistinguishable from "you're doing great."

  **Concrete persistence gap:** exceptions escaping the engine persistence phase are logged,
  but no durable failure state reaches `HealthStatus` and no retry policy stops the engine
  from repeating the same failed write. The frontend already has a `persistence-failed`
  event shape. Wire the native state and event, degrade health truthfully, avoid a hot retry
  loop, and test disk-full/locked failures.

  **Attendance recovery landed 2026-09-20.** `AppState::engine_tick` now keeps ordered span
  transitions pending until their transaction commits, retains the first Storage-clock
  boundary across retries, and advances committed attendance only after acknowledgement.
  A wake that arrives behind a failed idle-close is preserved rather than overwriting it;
  stop, replace, and delete still discard transitions for dead sessions. Snapback's one-shot
  emission is acknowledged after the same persistence phase, so a failed transaction cannot
  consume the alert. Tests cover real SQLite `BEGIN` contention plus injected begin/write/
  commit-stage failures. The wider health state, event, backoff policy, and user-facing
  failure treatment above remain open.

---

## Tier 10 — Frontend & UX

The frontend was inventoried in July and reviewed end-to-end on 2026-08-05: composition,
loading behavior, chart semantics, session controls, Settings hierarchy, privacy copy, and
the CSS token layer. Tests still mock IPC, so **10.1** remains the real-browser boundary.

- **10.1 — Nothing tests the real binary against the real UI.** `accepted` `L`
  There is **no E2E framework** — no Playwright, no Cypress, nothing in
  `frontend/package.json`. Frontend tests mock `invoke()`; C++ tests run headless.

  Be precise about what *is* covered, because this entry used to overstate the gap.
  `test_ipc_contract` pins command names three ways (the registered `CommandRegistry`, the
  frontend's `invoke` calls, and `fixtures/ipc_commands.json`), `test_command_bridge` covers
  the dispatcher itself — arg unwrapping, the error envelope, the escaped-JSON event boundary,
  the validation helpers — and since 14.3 `test_command_registry` invokes the real handlers
  by name through that same envelope.

  **What nothing exercises is the real `webview.bind()` round trip in a running process.**
  Every test above calls the handler layer directly, so a break *between* `bind()` and the
  browser — the injected shim, promise resolution, a webview API change — passes CI. A
  command whose payload shape drifted in a handler *without* a bridge test is the same
  story, and both violate the IPC synchronization contract.

  > *Corrected 2026-07-29:* this entry said `windows-desktop-integration` "is currently
  > skipped (6.3)". It is not — 6.3 removed its `needs:` on 2026-07-22 and it now runs
  > unconditionally. It also said the seam is tested "only by `test_ipc_contract`'s name
  > matching", which ignored `test_command_bridge` entirely.

  Plan (2026-09-16). Two of the three legs of a real-binary test already exist: the macOS
  and Windows GUI smokes launch the real app, and `SNAPBACK_GUI_SESSION_SMOKE` runs a
  session *from C++* via `w.dispatch` and writes a marker. What they do not do is cross the
  bridge from the page side. Close that in two steps, the first of which needs no new
  tooling:

  1. **An in-page acceptance script.** A debug-only env var (`SNAPBACK_ACCEPTANCE_SCRIPT`,
     gated like `SNAPBACK_FRONTEND_URL`) names a JavaScript file the host evaluates after
     the bundle loads. The script calls `window.__snapback.invoke` -- the real shim, the
     real `webview.bind`, the real token -- for a fixed list of commands (health, start and
     stop a session, one async export, one deliberate error) and reports each result back
     through one new command that writes a JSON verdict file; the existing smokes assert
     on that file the way they assert on the marker today. This is the "break between
     `bind()` and the browser" this item names, on all three OSes, inside jobs that already
     run.
  2. **A driven browser on the OSes that expose one.** WebView2 speaks CDP
     (`--remote-debugging-port` via `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS`), and
     WebKitGTK has `WEBKIT_INSPECTOR_SERVER`; Playwright can attach to both and click the
     real UI. WKWebView has no equivalent, so macOS keeps step 1 only. This is where
     14.6's "deliberately slow fake job keeps the heartbeat responsive" belongs.

  Since 14.3 the handler layer is covered by name in native tests, so step 1's list can be
  short: it tests the transport, not the commands.

  **Step 1 implemented 2026-09-16.** `scripts/gui_acceptance.js` is injected only into a
  desktop target explicitly compiled with `SNAPBACK_ENABLE_ACCEPTANCE_HARNESS=ON`; ordinary
  Debug and Release builds cannot enable arbitrary JavaScript with an environment variable.
  It crosses the real shim and `webview.bind()` for health, session start/stop, an async
  support export, and a deliberate native error, then reports five structured results through
  `report_acceptance_verdict`. The Windows CI desktop job and macOS GUI smoke assert the JSON
  verdict and require the app to terminate through its run loop. Step 2 -- driven clicks via
  WebView2/WebKitGTK -- remains.

  **Step 2 Windows slice implemented 2026-09-16.** The Windows smoke chooses an ephemeral
  loopback CDP port, attaches directly to the running WebView2 with Node's built-in WebSocket,
  and clicks Review, Settings, Start session, and Stop session in the real React UI. No
  Playwright/browser download is needed. The structured verdict identifies the CDP driver and
  the app still has to exit through its run loop. WebKitGTK-driven clicks remain; WKWebView
  stays on step 1 because it exposes no equivalent automation endpoint.

  *Progress 2026-09-24:* the page-side acceptance script now checks Settings and summary
  result casing and defaults, then reads the stopped session back through history. This
  covers payloads crossing the real bridge in addition to command resolution. The
  WebKitGTK-driven click slice remains open.

- **10.3 — Accessibility has never been assessed.** `in progress` `M`
  No audit has been done. Specifically worth checking: keyboard navigation through the card
  grid; focus management when the snapback overlay appears (it steals attention by design —
  does it trap focus?); screen-reader labelling of the score/state tiles; whether the
  distraction states are distinguishable without color; and whether the always-on-top
  overlay respects reduced-motion and OS contrast settings. A focus tool that fights
  assistive tech is a bad look.

  *Progress 2026-09-22 — the two defects `ASTRA_REVIEW.md` named for this item:*
  - **The permission wizard now does what its `aria-modal` says.** Focus moves to the primary
    action on open, the page behind is `inert`, Tab and Shift+Tab wrap inside, and focus
    returns on close (`frontend/src/PermissionWizard.tsx`). Escape is deliberately unbound:
    "Skip for now" records the first-run acknowledgement permanently, and a stray keypress
    should not end onboarding for good. The backdrop scrolls, so at a 1100×480 window every
    button is reachable (checked in a browser, as well as in
    `frontend/tests/permissionWizardFocus.test.tsx`).
  - **Settings' tabpanel holds its controls.** It used to close after the heading and blurb;
    it now wraps the section and is a CSS subgrid, so the cards keep the surface grid's
    columns (`frontend/tests/settingsNavFlow.test.tsx`).

  - **The Review charts have a data view.** All three are `<svg role="img">`, which hides
    each bar's `<title>` from assistive tech, so their numbers were unreachable without sight
    and a mouse. Each now has a closed-by-default "Show data" disclosure holding a real table
    (`frontend/src/ChartDataTable.tsx`), built from the same bar arrays the chart draws, with
    `name`/`detail` fields on the bar types so the table and the tooltip cannot disagree
    (`frontend/tests/chartDataTables.test.tsx`). Checked in the demo in light and dark.

  - **An audit pass over Now and Review in the demo** (accessibility tree, tab order,
    colour-coded elements). Passed as they are: the hero states its verdict in words and
    hides its dot as decoration, the session tiles are labelled, the verdict buttons carry
    `aria-label`s, and no click handler sits on an element a keyboard cannot reach. Fixed:
    Recent Predictions read out "61.0 41.0%" with the risk level only in the chip's colour; it
    now says "Focus score 61.0, Distraction risk 41.0%, medium risk" through visually hidden
    text, with the level on hover (`frontend/tests/predictionHistoryLabels.test.tsx`). And the
    goal field's suggestion list announced nothing: it is now a full ARIA combobox, with
    `aria-expanded` and `aria-activedescendant` following the arrow keys.

  - **Zoom and short windows, checked 2026-09-23** in the demo at 550×380 (an 1100×760
    window at 200% zoom) and at 320×640 (the WCAG reflow width), every surface and every
    Settings section, with all disclosures open: nothing extends past the viewport, nothing
    sits in an `overflow: hidden` clip, and the page never scrolls sideways — cards stack,
    the session explorer drops to one column, the chart tables wrap. Keyboard focus is visible
    throughout: the components with their own `:focus-visible` rule draw the accent outline,
    and the rest draw the browser's ring, which no rule suppresses. No change was needed.

  Still open: the snapback overlay's focus, reduced-motion and contrast (native code on both
  platforms, not reachable from the web demo); and one design decision — **the risk chip's level is still colour-only on
  screen** (amber vs green for 41% vs 38%). A screen reader now hears it, but a sighted user
  who cannot tell the colours apart does not see it. A visible cue (a word, an icon, a
  pattern) changes the card's look, so it is Kassa's call.

- **10.6 — No C++ coverage measurement at all.** `proposed` `M`
  The frontend can measure coverage; the C++ side cannot. Given how many bugs in Tiers 5/7
  were "the tests never exercised the production branch" (`seconds_since_session_start`, 7.1,
  5.3), a coverage report is the cheapest tool for finding the next one. `gcov`/`llvm-cov` on
  the Linux CI job.

- **10.10 — Build a complete visual-token and appearance system.** `in progress` `M`
  **PARTIAL — frontend cleanup implemented 2026-09-14; comprehensive visual/contrast
  coverage remains open.** Opened 2026-08-05.

  Complete in the frontend: semantic canvas, surface, border, text, control, chart, focus,
  and error tokens; persisted **System / Light / Dark** appearance (System by default).
  Explicit and system dark appearance now share one resolved token block. Risk/rules badges,
  hero/status dots, controls, and helper states use tokens instead of light-only literals;
  stale blue fallbacks and duplicate overriding rules are removed. The CSS guard now rejects
  undefined tokens, raw hex/RGB/HSL colors outside the token area, and repeated selector
  lists in the same at-rule context, with regression fixtures for the guard itself.

  Review now has a full-width range bar and shared stat-tile overview, paired session/hour
  charts, Top apps beside Recent Predictions, then full-width Context Timeline and a closed
  Session management disclosure. Reflections and two-step deletion share one bounded list.
  Unsaved reflections must be saved or cancelled before deletion becomes available.
  Long lists scroll internally; chart heights and axis labels are consistent. Card-rise and
  stagger delays are removed, reduced-motion support remains, and background glow/shadows
  are quieter. ADR-0003's cream/coral palette, serif headings, rounded section cards, and
  surface assignments remain intact; the App.tsx architectural split is still separate.

  Verification: Review inspected in the isolated sample-data browser demo at 1100 x 760 in
  light and dark appearance, including scrolling lists, expanded session management, and
  surface switching; single-column layout checked at 700px. This is frontend evidence only:
  the running native/C++ soak task was not touched.

  *Progress 2026-09-25:* The Windows QA pass gives each surface an accurate heading, moves
  Now's session controls above live feedback, leads Review with Summary and Sessions, and
  gives its session chart full width. Settings cards use one consistent measure; disabled
  primary actions have a distinct non-hover state. The stopped-session check-in reads and
  names the stored automatic label, with an honest unavailable state. Cross-surface session
  duration displays now share the seconds-aware formatter. Visual snapshot, contrast, and
  native overlay coverage listed below remain open.

  *Progress 2026-09-25:* A browser walkthrough of idle and active Now, Review, and Settings
  prompted a visual pass across light and dark appearances. The shell now uses aligned
  navigation, a quieter canvas, stronger headings, and restrained card borders. Review's
  summary and Now's live metrics use aligned values with separators for faster comparison;
  Settings places the walkthrough in a compact help row and names its controls Preferences.
  The three surfaces and their data behavior are unchanged. The sample-data demo was checked
  at desktop, 700px, and 375px widths, with tighter phone spacing and wrapped report metrics;
  native-window and automated visual coverage remain open.

  *Progress 2026-09-25:* The first session-story pass removes idle Now's Ready card and
  second Start action, leaves one small correction control during work, and puts healthy
  technical details in Advanced. Actionable failure routes remain visible.

  Remaining: automated light/dark visual snapshots for **all three surfaces and the native
  overlay**, comprehensive contrast assertions coordinated with **10.3**, and a native
  window/overlay smoke check after the ongoing soak is finished. Repo-wide Prettier cleanup
  remains separate from this functional change.

- **10.11 — Give the whole Review surface one shared time range.** `in progress` `M/L`
  Opened 2026-08-05. Trends describes all retained predictions, Summary chooses 24 hours or
  seven days, Recent Focus is framed as a sample count, and Insights uses its own recent-row
  limit. Placing those cards together implies comparison even though they describe different
  populations.

  Add a Review-level **Today / 7d / 30d / All / custom** range owned by the Review workflow.
  Every card must query and display that exact interval; no card may apply a hidden row cap.
  Loading, empty, error, and stale-result behavior belong to the range as one unit. Add
  workflow tests proving one change invalidates every Review dataset once and an older slow
  response cannot overwrite the newer range. Land after **7.16** defines calendar boundaries
  and **7.12** makes those queries bounded; implement through **14.4**, not another set of
  cross-card callbacks in `App.tsx`.

  **Interval provenance landed 2026-09-21** (Astra review slice 5). The shared range, the
  request-generation guard, and the "Last …" labels were already in place; what was not is
  that a card's pill came from the *selected* range, so pressing "Last 30 days" relabelled
  the 7-day numbers on screen for as long as the load ran — and forever if it failed. The
  workflow now carries `loadedRange` beside the data and exposes `displayedRange` /
  `staleInterval` (`useReviewWorkflow.ts:useReviewWorkflow`); every pill reads the loaded
  interval, and the range bar says "Showing Last 7 days until this loads" or, on failure,
  "Still showing Last 7 days" with a Retry that re-asks for the pressed selection. Recent
  Predictions and Context Timeline are marked *live* and the bar's "every card below uses
  this exact interval" promise is corrected to name which cards do and which do not. The
  per-session chart now shows the summary report's 500-session cap, since it reads the same
  capped list. Hook tests pin stale-during-load, stale-after-failure, Retry, and an older
  response never overwriting a newer one; App tests pin the pill, the alert, and the live
  markers. The live views' placement was resolved by the first session-story iteration below.

  *Progress 2026-09-25:* Custom now sends local midnight as a whole-second UTC timestamp,
  matching the native Review parser. Time-zone regression coverage pins the conversion; the
  shared range's existing loading and stale-data behavior is unchanged.

  *Progress 2026-09-25:* Recent Predictions moved to Advanced diagnostics, and the context
  timeline now reads the session selected in Review. Its insight and detail retain the label
  of the loaded range while a different range is loading or has failed.

- **10.14 — ADAPTER LANDED 2026-08-14; the export half stays open.** `in progress` `M`
  What landed: the owned native seam — `pick_open_file` / `pick_save_file` over Win32 Common
  Dialogs and AppKit, cancellation as an ordinary result, dialog authority kept in native code.
  **9.14**'s restore path uses it, which is what that item needed.
  **What did not:** the documents this item is actually about. `export_my_data`,
  `export_summary_report`, and `export_support_bundle` all still hardcode a folder under
  `data_dir` and return a printed path — no Save As, no Reveal or Copy path after success. The
  seam exists; the three exports have not been moved onto it.
  Opened 2026-08-05. Support, summary, and personal exports silently choose folders inside the
  app-data directory and then print a path. There is no file-dialog seam. That is tolerable for
  internal training artifacts, but poor desktop behavior for a document the user intends to
  keep, send, or restore on another machine; **9.14** will otherwise have no safe way to select
  an incoming snapshot either.

  Add owned native **Save As** dialogs for personal, summary, and support exports, plus native
  **Open** for the restore package in **9.14**. Cancellation is an ordinary result, not an error.
  Use platform overwrite confirmation, type filters and correct extension handling, then write
  to a sibling temporary file and publish atomically. After success offer Reveal and Copy path.
  The default private app-owned folder remains available for unattended/internal workflows.

  Keep dialog and filesystem authority in native code; do not expose an unrestricted path API
  to the webview, especially across **8.14**'s trust boundary. Adapter tests must cover cancel,
  overwrite refusal, Unicode and long paths, read-only destinations, extension normalization,
  and a window disappearing while the dialog is open; one Windows and macOS desktop smoke must
  exercise the real owner window. Coordinate long exports with **9.16/14.6**.

---

## Tier 11 — Test infrastructure

No open items. Completed work is in the [archive](roadmap_archive.md).

---

## Tier 12 — Documentation truth

**Every item here is a doc asserting something false.** This tier exists because that has now
happened often enough to be a category, not an accident. Two root causes recur: docs written
*before* the code (plans that were never reconciled), and docs written *about* code that
later moved.

> **Cleared 2026-07-23.** 12.1–12.5 are all done; only **12.6** remains, and it is a port
> gap the audit *found*, not a doc defect. Three things are worth carrying forward:
>
> 1. **The tier is now partly self-enforcing.** `scripts/check_doc_paths.py` runs in
>    `docs-smoke` and fails the build if any doc names a file that does not exist. That
>    closes the most common failure mode mechanically. It cannot check *claims* — only
>    paths — so the audit habit still matters.
> 2. **Doc audits find code bugs.** 12.2 turned up **8.7**, a silently dead `-UseVite`
>    flow, and 12.1 turned up **12.6**, an entire missing capability. A doc records what
>    the code was *supposed* to do; diffing that against what it does is cheap and finds
>    things tests do not.
> 3. **Every stale claim pointed the same way** — describing the system before a fix
>    landed, never after. Docs rot toward *pessimism* here, which is the dangerous
>    direction: it makes finished work look open and invites rebuilding it.

- **12.6 — Global label hotkeys were never built, and nothing recorded that.** `proposed` `M`
  Found 2026-07-23 while reconciling 12.1. The design called for global hotkeys that label
  the current window focused/distracted without leaving the app. **The frontend event and
  notification scaffolding now exists, but no native code registers an OS-global shortcut or
  emits the label-hotkey event.** It stayed invisible because
  `ARCHITECTURE.md`'s module map never listed the capability — the map only covered what
  someone intended to build, so a skipped module left no trace anywhere.

  Filed here because the doc audit is what surfaced it. It needs hand-written per-OS hotkey
  registration, which is presumably why it was skipped. ADR-0002's six-item blocker list does
  not include it, so this is post-v1 unless that accepted scope is explicitly revised.


---

## Tier 13 — Model lifecycle (breaking down 2.3)

**2.3 was one `L` item that hid at least seven.** The deployment identity, quality gate, and
rollback are complete as 13.1–13.4 in the [archive](roadmap_archive.md). The three unresolved product decisions
below still determine whether, where, and how the retraining loop should operate.

- **13.5 — Is there enough labelled data to train on at all?** `proposed` `S` `decision`
  Unexamined. Labels come from explicit user submissions plus auto-labels at session end.
  **7.5** unified explicit and shutdown stop, but **7.25** found the broader lifecycle still
  has holes: replacement can omit an auto-label and repeated Stop can add another. Before
  building the loop, first make label production idempotent, then measure: how many labels
  does a typical week produce, and what's the class balance? If the answer is "40 labels,
  90% PRODUCTIVE," personalization is premature and 2.3 should be rescoped to *collecting*
  data well rather than training on it.

- **13.6 — Define what happens when the model and the heuristic disagree.** `proposed` `S` `decision`
  5.1 established that the classifier blends model probabilities with rule/thrash/drift
  signals. Nobody has specified what *should* win, or how to tell when the model has drifted
  far enough from the heuristic to be distrusted.

  **ADR-0004 narrowed this, and it is now a smaller question than it was.** Model-vs-*policy*
  is settled: the scores are the model's opinion, `focus_state` is the verdict, and policy
  may only demote. What remains is model-vs-*heuristic* — two producers of the same opinion
  channel — which is a calibration question, not an authority one. `state_source` is the
  instrument for it: it records which rule bound each verdict, so "how often does policy
  overrule the model, and on what" is now a query rather than a study.

  Once behavior is settled, localize ownership too: `Classifier` reaches into the process-wide
  `OnnxModel::instance()`, while `AppState` separately loads it and reads its identity, and
  tests need cleanup guards for leaked singleton state. Put model lifecycle behind an owned
  classifier adapter without changing the chosen policy. Do not make that seam change first;
  it would disguise a behavior decision as architecture cleanup.

- **13.8 — PARTIAL 2026-08-10.** `in progress` `S/M` Startup no longer exits on model-recovery failure
  (`recover_model_deployment_for_startup`); health reports `degraded` with preserved paths and
  Diagnostics offers **Retry cleanup** and **Reveal preserved files**, which opens the private
  data directory and still reports its path when the OS refuses. Remaining from this item:
  the adversarial real-webview harness cases named in the original acceptance (coordinate
  with **10.1**).

  The original finding was:

- **13.8 (original finding) — Optional model recovery may degrade, never brick the core app.** `S/M`
  Opened 2026-08-05. Startup runs `recover_model_deployment()` before storage or the webview
  and exits the entire process on any exception. Recovery deliberately throws for a malformed
  marker or staging/cleanup debris it cannot remove — including a committed deployment whose
  valid live model is already in place. ONNX load and inference already fall back safely to
  the heuristic, so cleanup metadata is paradoxically more availability-critical than the
  optional model itself.

  If **13.7** removes consumer deployment, remove this startup path from normal builds and close
  the item that way. Otherwise preserve or quarantine questionable artifacts, start the core
  capture/history app on the heuristic, and expose a durable degraded-model health state with
  **Retry cleanup**, Reveal files, and a rollback action when one is provably safe. Never delete
  the only candidate/previous model merely to make startup green.

  Drive corrupt-marker, unremovable-staging, committed-but-locked-cleanup, invalid-model, and
  clean-retry cases through the actual startup orchestrator. Each must prove storage, session
  capture, Review, and heuristic predictions remain available; diagnostics must name the
  preserved paths without leaking their contents. This complements **13.4** rollback and
  **9.6** runtime failure UX; neither currently covers a pre-window optional-subsystem failure.

---

## Additions to existing tiers

- **9.10 — Retention deletes the data analytics depends on, and the user has no say.** `proposed` `S`
  `decision`
  The 90-day prune is hardcoded (`storage.hpp:kDefaultRetentionDays`) with no setting exposed. Two tensions
  nobody has resolved: a user who wants year-over-year trends silently can't have them, and
  a privacy-focused user who wants a 7-day window can't have that either. The value
  proposition ("see your focus patterns") and the privacy promise ("we don't keep it
  forever") point opposite directions, and the constant currently arbitrates. Make it a
  setting, and decide the default deliberately. Ties to **7.6** and **8.5**.

---

## Tier 14 — Architecture leverage (2026-08-01 and 2026-08-05 deep-module scans)

These are not “large file” complaints. Each item identifies a shallow seam where callers
must understand implementation details. Only work with a concrete acceptance boundary is
kept here; already-deep modules and completed performance work were rejected during the scan.

- **14.2 — Make one synchronous engine cycle the production test seam.** `proposed` `M`

  `engine_tick()` owns the real drain → idle/pomodoro → compute → persist → emit sequence,
  while tests and both benchmark targets include `tests/app_state_test_access.hpp` to reach
  three narrower private methods. Deleting that friend seam would force tests back to threads
  and sleeps; its forwarding interface is shallow because it exposes which internals to call.

  Extract a deterministic engine-cycle module whose `step` returns persistence jobs, emitted
  events, and updated live state. The production thread and tests must call the same step;
  persistence and UI dispatch remain adapters outside it. Delete the three private test
  methods and remove the benchmarks' dependency on the `tests/` include path. This is the
  concrete completion path for 7.14 and the remaining testability half of 11.4.

- **14.3 — Make the command registry the authoritative native contract.** `accepted` `M`

  Command names, argument defaults, validation, result casing, TypeScript DTOs, mappers,
  fixtures, and mocks are parallel hand-maintained descriptions across `commands.hpp`,
  `api.ts`, and `apiMappers.ts`. The contract test uses source-text matching for names, while
  selected command tests manually recreate handler lambdas. That catches some drift but still
  lets a real registered handler's payload shape diverge.

  Introduce a webview-free `CommandRegistry` that owns the real descriptors and handlers;
  make webview binding a thin adapter over it. Every registered handler must be invokable by
  name in native tests, and the frontend contract fixture must be generated from or validated
  against the same manifest. This complements 10.1's real-webview E2E; neither replaces the
  other.

  Progress (2026-09-16): `CommandRegistry` (`src/app/command_registry.hpp`) holds name,
  handler, and worker policy; `command_handlers.cpp` registers all 73 and `commands.hpp` is
  the adapter that binds them. The one platform reach in the handler table (the native
  overlay's dismiss) became an injected hook, so the table links headless. The contract test
  now compares the registry's names -- built against a real `AppState` -- to the fixture,
  replacing the regex over the source; `test_command_registry` invokes real handlers by
  name through the bridge's envelope, including the token check, and pins which commands
  carry a worker policy.

  Remaining: argument defaults and result casing are still described twice (handler and
  `apiMappers.ts`); a generated TypeScript manifest, or per-command result-shape assertions
  in the registry tests, would close that. The registry is also where 14.6's "slow" marking
  now lives, as the async policy.

  *Progress 2026-09-24:* registry tests now pin the actual Settings, recording, analytics,
  summary, and session-history response keys plus representative default arguments through
  real handlers. Expand this to the remaining mapped commands before closing the item.

- **14.4 — Move frontend invalidation into workflow modules.** `in progress` `M`

  `App.tsx` coordinates roughly a dozen feature states and passes 29 values into
  `useAppEffects`; deletion and session actions know which unrelated stores must refresh.
  Tests reproduce that knowledge with large command-switch mocks. Deep Now, Review, and
  Preferences workflow modules should own subscriptions, refresh consequences, and failure
  propagation, exposing smaller action/result interfaces to the surfaces.

  Preserve ADR-0003's Now/Review/Settings placement. Acceptance is workflow-level tests in
  which one public action proves every required invalidation without `App` manually calling
  each refresh function. Schedule this after release blockers; it is locality leverage, not a
  prerequisite to ship.

  **Performance acceptance added 2026-08-05.** The default surface is Now, but mount currently
  fetches health, latest prediction, rules, training status, insights, focus summary,
  analytics, and active session immediately. An active session polls context history even
  while Review is hidden. The new
  workflows must make hydration surface-aware: initial Now renders with zero Review/Settings
  data calls, first surface entry fetches each dataset once, re-entry uses cached data until a
  real invalidation, and no timeline query runs while Review is hidden. Deduplicate in-flight
  requests and prevent an older response from overwriting newer state. Prefer a native
  "context snapshot persisted" invalidation to refreshing history on ordinary prediction
  events. Pin command counts in workflow tests.

  *Correction 2026-08-19:* this paragraph also claimed `useAnalytics` performed a duplicate
  mount fetch. That hook was already dead when the claim was written — nothing imported it —
  and it has since been deleted, so the duplicate fetch it describes never ran. The rest of
  the acceptance stands; `useReviewWorkflow` fetches analytics once inside its batched
  `refreshReview`. Do not go looking for the duplicate.

  *Progress 2026-09-17:* Review hydration is gated on `active` (`surface === "review"`).
  Mounting Now no longer runs the Review batch; `frontend/tests/reviewWorkflow.test.tsx`
  pins zero review calls while inactive. Remaining performance work here is the broader
  workflow extraction and any Settings-surface gating still open above.

- **14.5 — Replace the fixed 10 Hz engine poll with deadline-aware, bounded work.** `in progress` `M`
  `performance`
  Opened 2026-08-05. The engine calls `engine_tick()` and sleeps 100 ms forever, including
  when there is no session and no event. Inside a cycle it drains until the capture queue is
  empty while holding the state lock; under sustained input, idle/Pomodoro work, persistence,
  emissions, stop, and session actions wait behind an unbounded drain.

  Build this with or after **14.2**'s deterministic cycle. Wake on capture arrival, stop/
  lifecycle requests, and the next idle/Pomodoro deadline. Give each cycle a fixed event or
  time budget, preserve event order across batches, and publish queue depth/high-water mark
  plus maximum drain time in diagnostics. A quiet minute should execute only deadline-required
  cycles rather than roughly 600; a continuous-producer test must prove stop and timer events
  cannot starve. Record same-host idle CPU/wakeups and event-to-prediction p95 before/after,
  with instrumentation overhead below 1%. ADR-0005 keeps sessions explicit, so also measure
  and eliminate unnecessary no-session classifier work without breaking **2.7**'s nudge path.

  *Progress 2026-09-17:* the per-tick drain is already bounded (`kEngineDrainBudget` /
  `kEngineDrainBudgetMs` in `state.hpp`, with backlog re-tick). Remaining work is
  deadline-aware wake (idle CPU when quiet) and diagnostics, not re-bounding the drain. The
  paragraph above that describes an unbounded `while (next_event())` is historical.

  *Progress 2026-09-21:* shutdown now joins the capture producer before allowing an empty queue
  to end the engine loop, so a final callback cannot land after the consumer exits. Healthy
  shutdown drains every time-limited slice; only consecutive failed ticks have a bounded
  termination path. Regression coverage fills the ring while forcing the 128-event checkpoint
  and publishes one final event during the stop barrier. Deadline-aware wake and diagnostics
  remain open.

- **14.6 — Move long-running commands behind owned, cancellable jobs.** `in progress` `L`
  Opened 2026-08-05. Webview bindings run on the UI thread. Training waits in `std::system()`
  for the Python process, and full training/personal exports execute directly inside bound
  handlers. A large export or real training run can freeze the native dispatch path while the
  frontend displays a progress state that cannot actually animate or cancel the work.

  Mark slow commands in **14.3**'s registry and return a job id within 50 ms. Progress,
  completion, structured failure, and cancellation arrive as events; only one training job
  runs at once, while independent exports have an explicit concurrency policy. A deliberately
  slow fake job must leave the UI heartbeat responsive. Cancellation and app shutdown must
  terminate/reap child processes, join workers, and guarantee no callback outlives `AppState`.
  Fast commands remain synchronous. Replace shell execution with an owned process API as part
  of this work, aligning with **4.2**, and do not promise cancellation until the child can
  actually be stopped.

  Progress (2026-09-16): the owned process API exists (`util/subprocess.hpp`: argv, not a
  shell; Job object / process group so a kill reaches the launcher's children; SIGTERM then
  SIGKILL). `train_from_export` runs through it on the command worker, gated so one training
  runs at a time and the privacy deletion refuses while one is reading the export directory,
  and it ends at its next poll once shutdown begins -- verified with a real Python child,
  including that `py -3`'s python.exe dies with the launcher. `pythonAvailable` no longer
  spawns on every status refresh.

  The card offers "Cancel training" while a run is in flight (`cancel_training`); the run
  answers through its own result, so the button cannot claim a stop the child has not made.
  Progress is the pipeline's own log: `train_from_export` re-reads `training.log`'s tail
  between waits and pushes `training-progress` events (via `AppState::emit_event`, the same
  hook and epoch check the engine uses) whenever it changes; the card shows elapsed time and
  the tail under the busy button, subscribed only for the length of the run.

  Decision (2026-09-16): completion stays on the command's own promise rather than moving
  to a job id plus a completion event. With cancel and progress in place, the job-id model's
  remaining benefit is surviving a webview reload mid-run, a development-only case, and it
  would replace a working, tested IPC contract. Revisit if exports grow the same need.

  Remaining: the registry marking from **14.3**, and the deliberately slow fake job proving
  the UI heartbeat stays responsive (a 10.1 concern, since it needs the real webview).

  *Correction 2026-09-17:* the opening paragraph's claim that training waits in
  `std::system()` on the UI bind path is historical. `train_from_export` and the exports
  already run on the command worker (`CommandRegistry` async policy); what remains is the
  heartbeat proof and any further job-id model revisit noted above.

  *Progress 2026-09-21:* model-file ownership now uses the training gate across promotion,
  reload, rollback, and deployment-cleanup retry. Conflicting synchronous commands return a
  clear busy error instead of touching a worker's transaction files; the command-registry
  regression holds the training job before asserting all three refusals. Cancellation and
  shutdown still release the same gate through the owned worker policy.

  *Progress 2026-09-21:* rollback now goes through the same staged, journaled pair promotion
  as a new model. Its regression covers an optional rollback metadata file and verifies the
  current model's metadata remains available as the next rollback target; a blocked metadata
  backup boundary now also proves both live pairs remain unchanged on failure.

  *Progress 2026-09-21:* classifier provenance now follows the producer of the persisted
  prediction: a failed loaded ONNX model reports the heuristic identity until a later ONNX
  inference succeeds. The ONNX regression checks backend, degraded status, and identity across
  both sides of that transition, and a fixture-backed AppState case verifies the successful
  identity reaches the stored prediction row.

- **14.7 — Move retention and space reclamation out of the launch critical path.** `in progress` `M`
  `performance`
  Opened 2026-08-05. `main.cpp` blocks on `Storage::open()` before the webview is constructed.
  That open synchronously migrates, prunes every retained runtime table, and runs a full
  blocking `VACUUM` after only 500 deleted rows. At roughly one prediction/feature row per
  second, an ordinary day's expiry clears that threshold; a mature database can periodically
  rewrite itself while a user double-clicks and sees no window.

  Measure first with month- and 90-day fixtures. Keep lock acquisition, schema validation, and
  required migrations on the correctness-critical startup path, but schedule ordinary pruning
  and page reclamation after first paint. Make timestamp predicates indexable after **7.16**,
  add the missing useful global timestamp access path for high-volume feature rows, delete in
  bounded chunks, and choose incremental/freelist-ratio or byte-based reclamation from measured
  file growth rather than row count alone. A session start must be able to yield/cancel
  maintenance before it harms capture persistence.

  Acceptance: no full `VACUUM` runs before the first window; the same-host mature-fixture p95
  for launch-to-visible improves by at least 80% over the captured baseline; expiry leaves no
  out-of-policy rows; engine write latency stays inside its existing benchmark bound; and a
  crash between chunks resumes safely without a second deletion interpretation. Publish last
  maintenance time/result and pending reclaim bytes in diagnostics. Reuse **14.5**'s deadline
  scheduler or **14.6**'s owned jobs rather than starting another unmanaged thread, and align
  the policy with user-configurable retention in **9.10**.

  *Progress 2026-09-17:* periodic retention no longer runs inside `engine_tick` under
  `storage_mutex_`. The tick only schedules work; `run_retention_maintenance` on
  `maintenance_thread_` deletes in bounded batches and yields, pauses while a session is
  active, and does not VACUUM. Startup `Storage::open` prune+VACUUM is still the launch
  blocker this item names. `idx_feature_snapshots_ts` exists (schema v8).

- **14.8 — Decompose `AppState` along its lock boundaries.** `in progress` `L`
  Opened 2026-09-16. `state.cpp` is ~2,400 lines and `AppState` has ~110 methods across
  seven concerns that share one class: the engine tick and event pipeline; sessions and
  their recaps; the Pomodoro state machine; alert routing, ids, and snooze/private-pause
  policy; settings, privacy exclusions, and app rules; reporting (analytics, summaries,
  history, exports); and model deployment/classifier lifecycle. The three ranked mutexes
  (`mutex_`, `activity_boundary_mutex_`, `storage_mutex_`) already say where the real
  boundaries are; the class just ignores them.

  **Do not "split the file".** Extract one concern at a time behind a type that owns its own
  state and is unit-testable without `AppState`, in this order, because each is the least
  entangled remaining piece: (1) Pomodoro -- `pomodoro_` plus the nine `*_pomodoro*`
  methods already form a machine whose only outward edges are `alert_route_unlocked` and
  persistence; (2) alert policy -- `issue_alert_id_unlocked`, `claim_alert_action`,
  `outstanding_alert_id`, snooze and private-pause lapses, reading settings through an
  interface rather than `settings_` directly; (3) reporting -- everything that takes only
  `storage_mutex_` and returns JSON (**14.1** closed without a read lane, so this is
  an extraction, not a second connection); (4) session lifecycle. The tick (`engine_tick`, `compute_event`,
  `persist`) stays last and becomes **14.2**'s production seam once the concerns it
  coordinates are types it can be handed.

  Acceptance per extraction: the moved methods are tested against the new type alone; the
  ranked-mutex order is unchanged (TSan and the `RankedMutex` self-check both stay green);
  `test_app_state` passes unmodified except for construction; no behaviour change is bundled
  in. Stop after any extraction whose diff exceeds ~600 lines and land it before the next.
  Prerequisite for none of the open items, so it yields to anything with a user-facing
  failure behind it.

  *Progress 2026-09-17 (concurrency audit follow-ups, before any extraction):*
  - Session start/stop bump `activity_epoch_` the way deletion already did, so queued
    prediction/snapback UI dispatches cannot paint after a generation change; the frontend
    also drops live cards on start/stop and ignores predictions for a different session id.
  - Writers open with `PRAGMA busy_timeout = kSqliteBusyTimeoutMs` so a brief external lock
    no longer fails `BEGIN IMMEDIATE` immediately and discards a drained persistence batch.
  - Snapbacks are not marked emitted until an emit hook exists (engine can start before the
    webview hook is installed).
  - `CaptureThread::record_failure` publishes the reason before `failed_`.
  - `recording_status` / `request_permissions` no longer hold `mutex_` across OS permission
    probes; `focus_summary_for_window` caps and reverses its prediction sample.
  Still under lock by design until extraction: settings fsync (`commit_settings_unlocked`)
  and ONNX reload (singleton shared with the tick).

- **14.12 — One Review load computes `prediction_stats` three times.** `proposed` `S` `performance`
  Opened 2026-09-22, by **14.11**'s measurement rather than by reading. `useReviewWorkflow.ts`
  fires five commands in parallel for one Review load, and three of them —
  `state.cpp:AppState::analytics`, `state.cpp:AppState::summary_report`, and
  `state.cpp:AppState::focus_summary_for_window` — independently run
  `storage.cpp:Storage::prediction_stats` over the **same cutoff**, each taking
  `storage_mutex_` to do it. At the measured 90-day cost of 5.2 s that is ~15.5 s of lock-held
  work for one number computed three times, inside a load whose other two commands add a
  further ~8 s.

  Two of those three call sites predate 2026-09-22; **7.33** added the third, deliberately and
  correctly — the implementation it replaced returned a *wrong* stretch on any window past
  about fourteen attended hours, and a right answer computed redundantly is the better defect.
  Recorded here rather than folded into 7.33 because the fix is not to undo it: the three
  commands want overlapping slices of one aggregate, so the answer is to compute it once per
  window and share it, which is a question about the command layer's shape and not about any
  one of them.

  *Reassessed 2026-09-22, after **14.13**.* The figures above were taken when every window read
  the whole table. On the presets the product actually offers, three calls now cost **165 ms**
  (`today`) and **1.3 s** (`7d`), not 15.5 s. That is no longer an obvious build — a cache
  carries invalidation, and the three commands derive their cutoffs milliseconds apart, so a
  memo keyed on the exact cutoff would never hit and one keyed on `(window, since)` would hand
  two of the three an answer computed for a slightly different instant. **Left `proposed` on
  purpose.** **14.1** closed 2026-09-22 with writer priority rather than a read lane, so a
  persist no longer waits out a whole load. What redundancy still costs is the Review
  page's own load time, not the engine's.

  *Measured inside a load 2026-09-22* (**14.1**'s per-command breakdown, corrected fixture,
  7-day preset). The three calls are **~1.3 s of a 2.55 s Review load** — the largest single
  share of it. Still `proposed`: the cache objection above is unchanged, and since 14.1's
  gate it is a question of how fast Review opens, not of how long a persist waits.

---

## Decided not to build (2026-09-22)

Recorded so the next time one of these is proposed the answer is a paragraph read, not a
debate. Each was checked against the code before being rejected, and "a comparable product
has it" was not treated as evidence. Deferred is different from rejected: a day-timeline lane
on Review is deferred, not rejected — it waits on **7.33** so it is drawn over one correct
stretch computation, and on FWD-02 so Review is reshaped once.

- **Blocking apps or sites.** Snapback reflects; it does not block. The principle is now
  stated in [`ARCHITECTURE.md`](ARCHITECTURE.md); a Block rule forces the verdict
  ([ADR-0004](adr/0004-verdict-and-opinion.md)) and never touches the window. A proposal to
  add blocking is a proposal for a different product and needs its own ADR.
- **Full-text search (FTS5) over context history.** FTS5 is not compiled in —
  `CMakeLists.txt` builds the amalgamation with no `SQLITE_ENABLE_FTS5` — and titles live only
  in `context_snapshots`, written on foreground change plus a 30-second checkpoint, so the
  table is thousands of rows a week, not millions. If search is ever wanted it is a `LIKE`;
  the "return to what you were doing" promise is already the snapback restore target.
- **A localhost HTTP API or MCP server.** A loopback listener is still a listener; **8.10**
  and [ADR-0009](adr/0009-local-first-threat-model.md) promise a release build that opens no
  sockets. The honest alternative is the documented schema plus the existing exports
  (**9.14**, **9.16**). Reversing this is a new ADR with a threat-model delta, not a feature.
- **Compacting consecutive identical predictions into runs.** The premise was per-event
  persistence; the cadence is at most one row per second while attended (**14.11**).
  `feature_snapshots` is written 1:1 with `predictions` and is the training export, and
  `focus_momentum` feeds the model from stored scores
  ([ADR-0004](adr/0004-verdict-and-opinion.md)), so runs would discard exactly what the model
  consumes.
- **Auto-update, or evaluating a packager for it, ahead of an ADR.** An update check is a
  network call. Until an ADR amends the network-silent rule the evaluation has nothing to be
  measured against; revisit when FWD-06 is scheduled.
- **Reading browser URLs through UI Automation before 8.11 exists.** URLs are more sensitive
  than titles, the read is per-browser-version brittle, and the domain field's value is
  asserted, not shown. Not before scoped capture rules can redact what it would capture.
- **A fourth, always-on-top "Now" surface.** [ADR-0003](adr/0003-three-surface-dashboard.md)
  fixes three surfaces; the overlay already owns the glanceable moment the product needs.
  There is no evidence users keep the full window open to watch the hero.
- **Generating the IPC contract from the fixture.** `tests/test_ipc_contract.cpp` already
  checks registry equals fixture and frontend invokes are a subset; **14.3** covers the
  remaining shape drift. A generator adds a build step to save two type declarations per
  command.
- **Performance ceilings asserted in CI.** Hosted runners are too noisy and the harness
  measures in-memory SQLite. Publish numbers (**14.11**); do not gate on them.
- **Auto-proposed or auto-detected sessions.** Decided in
  [ADR-0005](adr/0005-a-session-is-declared-and-attended.md); **2.7**'s untracked-work nudge
  asks and never starts a session. Reopen only with new evidence, and as a decision.
- **Issue and pull-request templates.** Sole-author repository; CI already enforces the one
  contribution rule that matters. `SECURITY.md` was the part worth doing (**9.18**).

---

## Recurring health checks

Checks to run on a cadence, not one-off tasks. Several are automatable; where so, that's
itself a backlog item below.

### Before every release

- [ ] Open a **pre-existing** `focoflow.db` written by an earlier install and run a
      full session end-to-end. The 7.11 fixture corpus now makes this directly runnable.
- [ ] Kill the process uncleanly mid-session, restart, confirm WAL recovery and that the
      orphaned `ACTIVE` session resumes (`state.cpp:AppState` claims to handle this — verify it).
- [ ] Run a session on each OS long enough to exceed the ring buffer under load, and confirm
      `capture_events_dropped` reflects reality; 7.4/7.17 expose the signal, this validates
      it under a real desktop workload.
- [ ] Confirm every `invoke(...)` string in `frontend/src/api.ts` resolves in
      `commands.hpp`. `test_ipc_contract` covers the C++ side; confirm it covers the TS side
      too. This contract is easy to drift and a mismatch fails silently at runtime; 14.3 is
      the structural fix.
- [ ] Confirm the release tag equals CMake's version, names a commit reachable from protected
      `master`, and carries the full CI result required by 9.11.
- [ ] Extract every release artifact and verify the project license, dependency notices,
      frontend bundle, executable signature where required, and launchable binary are inside.
- [x] Feed a window title containing invalid UTF-8, U+2028, quotes, and backslashes through
      the full pipeline. Covers 8.1 and 8.2 in one test. **Automated 2026-08-22** as
      `tests/test_app_state.cpp:a hostile window title crosses the whole pipeline without dropping the tick`,
      so it no longer needs running by hand. It found a live defect on its first run: the
      emit dumps used nlohmann's strict handler and threw `type_error.316` on the invalid
      bytes, costing every event of that tick. `dump_json` (`src/types.hpp:dump_json`) now
      replaces malformed bytes with U+FFFD on every path carrying OS-derived strings.

### Monthly, or when a subsystem is touched

- [ ] **Ghost-item sweep.** For each item claiming something is missing, grep first —
      including non-`.cpp` extensions. For each Done-archive item, confirm the code has a
      **caller**. This has found real ghosts twice (0.3, 2.4); assume it will again.
- [ ] **Dead-code sweep.** Every `.hpp` in `src/` should have a caller outside its own test.
      `confidence.hpp` was the known offender and is now deleted (5.3, ADR-0004); check for
      siblings.
- [ ] **Unit sanity sweep.** Grep thresholds and confirm each matches its producer's scale.
      5.3 shipped `[0,100]` logic against a `[0,1]` producer and the tests passed because they
      fed values the system never emits.
- [ ] **Default-build coverage.** Confirm what sits behind `SNAPBACK_ONNX` /
      `SNAPBACK_BUILD_APP` and is therefore unexercised by the default build. The 5.2 fix
      lives inside an `#if` only one CI job compiles — a standing risk, not a one-time note.
- [ ] **Stack-size sweep.** Grep for large by-value members (6.1). Anything over ~64 KB per
      object is a Windows landmine.
- [ ] **Fresh-clone sweep.** Run the doc-path guard and the frontend build from a *clean*
      clone, not the working tree. **2026-07-24:** `scripts/check_doc_paths.py` was green
      locally and red in CI on its first run, because this working tree still has
      `frontend/dist` from earlier builds while a fresh checkout does not. A guard that only
      passes on a developer's machine guards nothing. `git clone . /tmp/x && cd /tmp/x` is the
      whole check.
- [ ] Re-run the feature-parity golden test. Any `features.cpp` change without a matching
      golden update is a CI failure waiting to happen (5.6).

### Candidates for new CI jobs

- [ ] **Schema-drift job:** diff the current `CREATE TABLE` statements against a checked-in
      snapshot and fail on divergence. Guards the 7.3 compatibility promise directly.
- [ ] **Scale job:** seed a month of synthetic usage; assert analytics/summary return correct
      counts inside a time budget. Would have caught 7.1; guards 7.12.
- [ ] **Health-truthfulness job:** force each failure mode (dead hook, over-broad exclusion,
      persistence failure, no session) and assert `HealthStatus` reports something other than
      healthy. Capture/prediction fields are unblocked by 7.4 and 7.10; persistence waits on
      9.6. The point is that health fields must never be literals again.
- [x] **Stack-size assertion:** `static_assert(sizeof(AppState) < N)`. One line, permanently
      prevents 6.1's class of regression. **Done 2026-08-22** in `tests/test_app_state.cpp`,
      beside the equivalent guard for `CaptureThread`; N is 16 KB against a current 3,392
      bytes, and the assertion was verified to fire before being set to that bound.
- [x] **Dead-header job:** automate the dead-code sweep above. It's the check that would have
      caught 2.4 for free. **Done 2026-08-22** as `scripts/check_dead_headers.py`, run in the
      CI guard job. A header's own `.cpp` and its tests do not count as callers, so a
      `.hpp`/`.cpp` pair nothing else uses fails it, not just a header-only file.

---

## Completed work

Everything finished lives in [`roadmap_archive.md`](roadmap_archive.md), with its original
wording, ID, and date. Search that file for the ID. The index below exists so this file can
still answer "is 9.15 done?" without opening it.

| Item | Item | Item |
| --- | --- | --- |
| 0.1 | 5.9 | 9.3 (2026-08-04) |
| 0.2 | 6.1 (2026-07-22) | 9.7 (2026-07-26) |
| 0.3 (2026-07-25) | 6.2 (2026-08-27) | 9.8 (2026-07-26) |
| 0.4 | 6.4 (2026-07-22) | 9.9 (2026-07-26) |
| 0.8 | 6.6 (2026-08-06) | 9.11 (2026-08-04) |
| 1.1 | 7.1 (2026-07-22) | 9.12 (2026-08-27) |
| 1.2 (2026-08-03) | 7.3 (2026-07-29) | 9.13 (2026-08-27) |
| 1.3 | 7.4 (2026-07-22) | 9.14 (2026-08-11) |
| 1.4 | 7.5 (2026-07-22) | 9.15 (2026-08-27) |
| 1.5 | 7.6 (2026-07-30) | 9.16 (2026-08-06) |
| 1.6 | 7.7 (2026-08-03) | 9.18 (2026-09-22) |
| 2.1 | 7.9 (2026-07-22) | 10.2 (2026-07-25) |
| 2.2 | 7.10 (2026-07-22) | 10.4 (2026-07-26) |
| 2.4 | 7.11 (2026-07-31) | 10.5 (2026-07-26) |
| 2.5 | 7.12 (2026-08-06) | 10.7 (2026-07-26) |
| 2.6 | 7.13 (2026-07-22) | 10.8 (2026-08-06) |
| 2.7 (2026-08-10) | 7.14 (2026-07-31) | 10.9 (2026-08-11) |
| 2.8 (2026-08-14) | 7.15 (2026-07-31) | 10.12 (2026-08-11) |
| 2.10 (2026-08-10) | 7.16 (2026-08-24) | 10.13 (2026-08-06) |
| 2.11 (2026-08-10) | 7.17 (2026-07-26) | 11.1 (2026-07-31) |
| 2.12 (2026-08-11) | 7.18 (2026-08-03) | 11.2 (2026-07-31) |
| 2.13 (2026-08-10) | 7.19 (2026-08-04) | 11.3 (2026-07-31) |
| 2.14 (2026-08-10) | 7.20 (2026-08-04) | 11.4 (2026-07-31) |
| 2.15 (2026-08-06) | 7.21 (2026-08-07) | 11.5 (2026-07-31) |
| 2.16 (2026-08-27) | 7.22 (2026-08-05) | 11.6 (2026-07-31) |
| 2.19 (2026-08-12) | 7.23 (2026-08-06) | 11.7 (2026-08-04) |
| 3.0 (2026-07-30) | 7.24 (2026-08-05) | 11.8 (2026-08-04) |
| 3.1 (2026-07-28) | 7.25 (2026-08-06) | 11.9 (2026-08-08) |
| 3.6 (2026-08-27) | 7.26 (2026-08-06) | 11.10 (2026-08-08) |
| 4.1 | 8.1 (2026-07-22) | 11.11 (2026-08-07) |
| 4.6 | 8.2 (2026-07-26) | 11.12 (2026-08-27) |
| 4.7 | 8.3 (2026-07-22) | 12.1 (2026-07-23) |
| 4.8 | 8.4 (2026-07-22) | 12.2 (2026-07-23) |
| 4.9 | 8.5 (2026-08-27) | 12.3 (2026-07-23) |
| 4.10 | 8.6 (2026-07-31) | 12.4 (2026-07-23) |
| 4.13 (2026-08-08) | 8.7 (2026-07-26) | 12.5 (2026-07-23) |
| 5.1 | 8.8 (2026-08-04) | 12.7 (2026-08-08) |
| 5.2 | 8.9 (2026-08-04) | 12.8 (2026-09-22) |
| 5.3 (2026-08-03) | 8.10 (2026-08-05) | 13.1 |
| 5.4 (2026-08-03) | 8.12 (2026-08-06) | 13.2 |
| 5.5 (2026-08-24) | 8.13 (2026-08-07) | 13.3 |
| 5.7 | 9.1 (2026-07-25) | 13.4 |
| 5.8 | 9.2 (2026-07-22) | 13.7 (2026-08-07) |
|  |  | 14.1 (2026-09-22, writer priority) |
|  |  | 14.11 (2026-09-22) |
|  |  | 14.13 (2026-09-22) |
|  |  | 14.14 (2026-09-22, not built) |
