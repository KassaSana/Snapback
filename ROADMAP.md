# ROADMAP.md — six months, sequenced

Derived from [BACKLOG.md](BACKLOG.md) (2026-08-19 audit + direction tickets). This is the
strategic sequence; the operational item tracker remains
[docs/ROADMAP.md](docs/ROADMAP.md), and items below cite its numbers where they overlap
rather than restating them.

**The sequencing rule:** each phase exists to make the next one cheaper. Concretely:
trust-in-the-numbers fixes come before any feature that displays numbers; feature-semantics
fixes come before any training-data collection; a shipped release comes before anything whose
value depends on users existing.

**The one-sentence plan:** fix the four things that are actually broken, ship the release the
repo is already dressed for, make the weekly data worth looking at, and only then spend on the
personalization loop — with platform breadth explicitly deferred.

---

## What is already built and waiting (use it, don't rebuild it)

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
  scripts, a changelog discipline, a release workflow gated on CI-green (Tier 9 in
  docs/ROADMAP.md is mostly checked off).
- **`SNAPBACK_FRONTEND_URL` dev seam, benchmark harness, feature-parity fixtures** — the
  infrastructure FWD-09 and FWD-07 need is half-present.

---

## Phase 0 — Weeks 1–3: make the existing product true

Nothing ships and nothing new gets built until the features the app already claims actually
work. All four fixes are small; their absence undermines every later phase.

| Item | Backlog | Why now |
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

## Phase 1 — Weeks 3–6: ship v1 and the channel to v1.1

| Item | Backlog | Notes |
| --- | --- | --- |
| Cut Windows v0.3.0 | FWD-01 | Tier 9 checklist in docs/ROADMAP.md; decide signing (0.4b) as ship-unsigned-with-caveat vs. wait — recommend ship with caveat, sign in v0.4 |
| Auto-update check | FWD-06 (S-M) | Native-side fetch; depends on AUD-03 (the "download" link is an external link) |
| Notification budget & quiet hours | FWD-05 (S-M) | Retention insurance *before* strangers install; pure settings-surface work |
| `winget` manifest | FWD-06 rider (S) | Only once signing lands; otherwise defer to v0.4 |

**Why before the data work:** every Phase 2+ decision improves with even ten real users'
feedback, and the release machinery is the closest-to-done big item in the repo.

## Phase 2 — Weeks 6–12: fix the feature semantics, then make the data worth opening

Ordering inside this phase is load-bearing: **the feature-vector fixes must land before the
digest advertises the numbers and before any training corpus is collected**, because they
change the input distribution (train/serve skew otherwise). This is one coordinated
feature-contract change, not three drive-by fixes:

1. Synthesize idle events / reset the break clock (AUD-02, M) — revives `idle_time_30s`,
   `idle_event_count_5min`, `longest_active_stretch_5min`, `minutes_since_last_break`, and
   re-arms the hyperfocus nudge.
2. Stop counting the empty app name in `unique_apps_5min` (AUD-12, XS) — batched into the same
   contract bump and golden-fixture regeneration.
3. Resolve the `is_pseudo_productive` question with the trainer (AUD-11, XS investigate) —
   the contract bump is the moment to drop or document it.
4. Windows context-gate improvement (AUD-09, M) — foreground-event-driven refresh; improves
   the very features (thrash, keystroke rate) the digest will highlight.

Then, on top of honest features:

| Item | Backlog | Notes |
| --- | --- | --- |
| Weekly focus story / digest | FWD-02 (M) | Composition of existing queries + episodes; needs AUD-08's cap honesty for any "all time" claim |
| "All time" cap honesty | AUD-08 (S) | Do together with the digest — same surfaces |
| Snapback recovery upgrade | FWD-04 (M) | Builds on AUD-01; episode-outcome logging here is deliberately *before* Phase 3, because it produces the labels Phase 3 trains on |

**Conflict noted:** docs/ROADMAP.md Tier 2 contains several ML-depth items that assume the
current 31-feature contract. Any of those started before this phase's contract bump would be
built on features that are about to change meaning — sequence them after.

## Phase 3 — Weeks 12–20: the personalization loop

| Item | Backlog | Notes |
| --- | --- | --- |
| Offline model evaluation harness | FWD-07 (M) | **First.** No model swap without a replay-based "not worse on your own history" check |
| On-device retrain path | FWD-03 (L) | **Flagged as a partial rewrite**: option (a) re-implements the trainer contract in C++ and revises ADR-0006. Timebox a spike (1 week) before committing; option (c) — loop stays dev-only for v1.x — is an acceptable outcome of the spike |
| Model observability polish | FWD-07 rider | `state_source` and `explainPrediction` already exist; wire eval results into the TrainingDeploy card |

**Dependency chain that justifies the ordering:** Phase 2's feature fixes → fresh corpus with
correct semantics → FWD-07's harness to judge candidates → FWD-03's trainer has something safe
to do. Starting FWD-03 first (it's the most exciting item) would train on features the fixed
engine never reproduces — the skew would be introduced by roadmap ordering alone.

**Supersession noted:** if FWD-03's spike lands on option (a) (native mini-trainer), the
existing Python-repo plumbing (`set_training_repo_path`, `train_from_export`'s repo checks)
becomes legacy for end users — keep it for developers, but don't invest further in its UX
(several docs/ROADMAP.md Tier 13 items become dev-only concerns at that point).

## Phase 4 — Weeks 20–26: quality, breadth *decision*, and paydown

| Item | Backlog | Notes |
| --- | --- | --- |
| Frontend surface split | AUD-20 (M-L) | Do it *after* the digest reshaped Review — splitting before would split twice |
| File splits, const-correctness, test-runner glob | AUD-21, AUD-15, AUD-22 (each S) | Opportunistic paydown; none blocks anything, which is why they're last |
| Clock seam completion | AUD-14 (S-M) | Do the AppState half; Storage half only if a Phase 2/3 bug demanded it |
| Linux cheap fixes | AUD-10 parts (2)+(3) (S) | Click mapping + wall clock: two small diffs, big honesty gain for Linux users |
| **Platform breadth decision** | — | One platform gets real investment next half: macOS polish (notifications 3.3, signing) *or* Linux capture rework (AUD-10 part 1 + Roadmap 3.2). Not both. Decide from v0.3 install telemetry-by-anecdote — whichever platform users actually asked about |
| macOS audit pass | AUD-23.4 | The `.mm` files have not had this audit's scrutiny; do it on a Mac before investing in macOS breadth |

Deliberately **not** scheduled in these six months (from FWD-10): calendar integration,
gamification, browser extension, cloud sync, Linux UI parity. Each is either
differentiator-diluting or sequenced behind evidence of demand.

---

## Standing risks this sequence carries

- **Phase 2's contract bump is the riskiest single change** — it touches golden fixtures, the
  deployed-model assumption, and product behavior (hyperfocus re-arming) at once. Mitigation:
  it's also the phase with the eval harness *next*, so schedule slip there should push Phase 3
  wholesale rather than letting FWD-03 start early.
- **FWD-03 is the only L-sized item and the only flagged rewrite.** The spike-then-decide gate
  is the plan's main scope valve: if the spike fails, Phases 3–4 compress and the freed time
  goes to the Phase 4 platform decision.
- **The existing docs/ROADMAP.md remains authoritative for item-level detail.** This file
  sequences; it doesn't duplicate. When the two disagree on ordering, this file's phase gates
  (feature semantics before corpus, eval before deploy, release before demand-driven breadth)
  are the tiebreaker.
