# Decision Session A — working notes (2026-08-03)

Scratch file for settling 7.7, 7.18, 5.3, 5.4, 1.2. Every claim below was read from the
code this session; file:line references are to the current tree.

## The seam, as built

**Producer** — `Classifier::predict` (both backends converge on the same two steps):

1. *Model opinion* — `scores_from_probas` (`classifier.cpp:74`): argmax of four class
   probabilities → `focus_state`; probability-weighted average over
   `kFocusLevels = {25, 50, 75, 100}` → `focus_score`; `distraction_risk = p[DISTRACTED] +
   thrash * 0.15` (`shape::kThrashRiskBump` — the one place a context signal edits a
   *score*; noted in `classifier_tuning.hpp:168`).
2. *Policy* — `apply_focus_guardrails` (`classifier.cpp:213`): mutates **only**
   `focus_state`.
   - Rule 1: `risk >= risk_threshold(mode)` (0.55/0.70/0.85, `types.hpp:85`) OR
     `thrash >= 0.75` OR `personal_block` → `DISTRACTED`.
   - Rule 2: else `drift >= 0.55 && state != "DEEP_FOCUS"` → `PSEUDO_PRODUCTIVE`.
     This is 7.18: it *upgrades* a model-DISTRACTED row (score 25 → 50 reading).

**Stored row** (`insert_prediction`, `storage.cpp:1050`): focus_score, distraction_risk,
focus_state, thrash_score, drift_score, goal_alignment, timestamp, model_id. **The model's
own argmax is not stored** — once policy overwrites `focus_state`, the opinion is
unrecoverable from the row. No migration can retro-compute it.

## Consumers, by channel

| Consumer | score channel | state channel | notes |
|---|---|---|---|
| `features_.update_focus_score` (`state.cpp:1018`) | focus_score/100 → `focus_momentum` | — | model opinion feeds back into the *feature vector*. Clamping score to match policy would leak policy into the model's own inputs |
| `ContextTracker` via `set_prediction_feedback` (`state.cpp:1019`) | — | `snapback_on_task` (`app_context.cpp:155`): `DISTRACTED` → off-task; PSEUDO/PRODUCTIVE/DEEP → on-task | the **live snapback nudge obeys the policy verdict**. 7.18's upgrade makes weakly-distracted+high-drift rows count as on-task |
| `recap()` (`storage.cpp:964`) | avg_focus_score, avg_distraction_risk | deep_focus_pct | thrash_spikes = `risk >= 0.7 AND state = 'DISTRACTED'` — **hybrid** of the two channels (`storage.cpp:1019`, comment defends the absolute 0.7) |
| `recent_session_summaries` (`storage.cpp:819`) | same | same | SQL copied verbatim from recap(); parity-tested field by field |
| `infer_session_label` (`storage.cpp:1029`) | avg_distraction_risk | deep_focus_pct | + thrash_spikes ≥3 → Distracted, ≥1 → Pseudo. **Auto-labels = 2.3's training corpus** |
| `analytics()` (`state.cpp:578`) | avg_focus_score; streak = recap.avg_focus_score ≥ 70 | hourly distracted rate | mixes channels across tiles |
| `summary_report()` (`state.cpp:635`) | avg_focus_score | distracted_fraction, longest_focus_streak | mixes channels in one struct |
| `summarize_predictions` (`focus_summary.hpp:29`) | avg/peak | distracted_fraction, streak | same mix |
| Now surface (`FocusStateHero.tsx`) | secondary line `focus N` | hero word | **hero color = `riskLevel(risk)` (fixed 0.4/0.7, `utils.ts:45`) while hero word = state** — a block-rule row at risk 0.3 renders "Distracted" with the low-risk color |
| `explainPrediction` (`utils.ts:104`) | thrash/drift/goal reasons | caveat keyed on state | **cannot explain a block-rule demotion** — personal_block isn't in the record, so the UI can say "Distracted … because no app switching · settled in one window" |
| `VerdictFeedback` | — | predictedState = policy state | user confirmations label the *verdict*, not the opinion |
| Review tiles (`SessionReviewCards.tsx:64-78`, `SummaryCard`, `FocusSummaryCard`, `AnalyticsCard`) | Avg focus | Deep work %, Distracted %, streak | "Thrash spikes" tile shows the hybrid count |
| `export_my_data` (`data_export.cpp:106`) | avg focus score | deep focus pct | user-facing archive |

## Fixture / test surface (what actually moves per option)

- `fixtures/feature_parity/golden.json` pins the **feature vector only** — guardrail
  changes do not touch it (confirmed by reading `run_feature_golden_file`,
  `test_feature_parity.cpp:102`).
- `fixtures/feature_parity/classifier_scenarios.json` — 3 scenarios asserting
  PRODUCTIVE / DISTRACTED(thrash) / DISTRACTED(block). **None exercises the drift branch;
  none would fail under a demote-only rule 2.** A new drift scenario would be an addition,
  not a move.
- `test_classifier_properties.cpp:276` — the characterization test: asserts the upgrade
  happens (drift 0.60, probas {0.30,0.25,0.25,0.20} → PSEUDO) and that DEEP survives.
  Designed to fail when 7.18 is settled; flipping it is Phase 2's job.
- `test_classifier_properties.cpp:244` — "guardrails force DISTRACTED" property is
  rule-1-only; unaffected by a rule-2 change.
- `test_storage.cpp:768` recap case seeds (90,0.10),(70,0.20),(30,0.80) → spikes == 1;
  seeder at `:50` sets state explicitly, `thrash_score = risk >= 0.7 ? 1 : 0`. Predicate
  changes to thrash_spikes need this case re-derived by hand; the batched-parity and
  large-fixture cases compare against `recap()` itself so they follow automatically.
- `test_confidence.cpp` — 4 cases, dies with confidence.hpp if 5.3 = delete.

## 5.3 facts

`confidence.hpp` callers: **only** `test_confidence.cpp` (grepped whole repo). Units
documented `[0,100]`, classifier emits `[0,1]`, so `should_nag(0.9)` is false — the roadmap
claim verified. There is **no risk-driven nag anywhere for it to gate**: the snapback
overlay fires from `ContextTracker`'s state machine (off-task ≥ `min_distraction_secs_ =
30.0`, `tracker.hpp:83`), which already provides the debounce confidence gating was for.
ADR-0002:36 and ADR-0003:34 both call it dead; Done-archive 2.4 is flagged disputed.

## Migration mechanics (7.3 gives us)

Ordered append-only `kMigrations` (`storage.cpp:595`), replay-from-0, idempotent, one
transaction, downgrade guard. `user_version` currently 2. Retention prunes runtime data at
**90 days** (`kDefaultRetentionDays`, `storage.hpp:22`) on open — mixed-semantics rows age
out on their own. `model_id` is diagnostic display + provenance only; nothing compares it
exactly (grepped; frontend only defaults/displays it).

Key asymmetry: **no option can rewrite history honestly** — the model argmax isn't stored,
so old rows cannot be re-derived. The only honest treatments of existing data are
(a) leave it and date the semantics change in the ADR, (b) delete predictions (user data —
their call, and 7.6's delete-all already exists), or (c) add a nullable column going
forward. There is no computable UPDATE.

## Shape of the settlement (as argued in the memo)

Two named channels with an authority rule:

- **Opinion** (never edited by policy): focus_score, distraction_risk, thrash, drift,
  goal_alignment. Keeps the momentum feedback model-pure.
- **Verdict** (`focus_state`): what the app *acts on* — tracker, distracted counts,
  streaks, hero word. Policy is **demote-only**: rule 1 already is (forcing DISTRACTED on
  an argmax-DISTRACTED row is identity); rule 2 becomes `state == "PRODUCTIVE"` →
  PSEUDO (one-token change from `!= "DEEP_FOCUS"`), preserving the deliberate DEEP
  exemption and ending the 7.18 upgrade.
- 7.7 resolved by labeling, not clamping. Hero color should follow the verdict, not
  riskLevel.
- 5.4: thrash_spikes becomes pure-opinion `risk >= 0.7` (absolute intensity bar the
  roadmap already defends; drops the state conjunct that smuggles mode-dependence in).
- 1.2: FocusMode *is* the sensitivity control for v1; no new tunable.
- 5.3: delete.

Open questions for Kassa are in the memo (end of Phase 1 message), not duplicated here.

## Phase 2 — implemented 2026-08-03 (all six answers: as recommended)

- [x] `apply_focus_guardrails` — demote-only; drift branch fires only on `PRODUCTIVE`; each
      branch records `state_source`
- [x] characterization test → the demote-only property (rank-ordered, 2000×3 draws) plus a
      `state_source` attribution case
- [x] fourth scenario `title_churn_pseudo_productive` in `classifier_scenarios.json`;
      `drift_score_min` support added to the runner
- [x] `thrash_spikes` = `distraction_risk >= 0.7` at both SQL sites; recap test extended with
      a Recovery-band row (risk 0.75, state PRODUCTIVE) that the old predicate missed
- [x] migration 3 `predictions.state_source` (nullable TEXT), `kSchemaVersion` 3; insert +
      all three read paths carry it; legacy-row test asserts NULL, not "model"
- [x] `confidence.hpp` + `test_confidence.cpp` deleted (CMake globs `tests/*.cpp`, so no
      build-file edit was needed — verified)
- [x] UI: `verdictLevel()` colours the hero from the verdict; `explainPrediction` leads with
      the policy rule and suppresses contradictory calm phrases; tile → "Distraction spikes"
- [x] ADR-0004 + index row + roadmap rewrites for 7.7 / 7.18 / 5.3 / 5.4 / 1.2, blocker table,
      2.4 retraction, 11.2 payoff note, 13.6 narrowed
- [x] existing data: leave-and-date (no rewriting migration exists that would not fabricate)

### Verification status

**Frontend.** `typecheck` clean; `test:unit` all 9 scripts pass; `focusStateHero` 13/13.
Component-suite totals are **44 failed / 43 passed** with these changes and **44 failed /
42 passed** on a stashed clean tree — the same 44 pre-existing failures
(`window.localStorage` undefined under Node 26 here), one net new passing test. Measured
both ways, not assumed.

**C++ — built and run 2026-08-04.** `313/314 ctest cases pass.` The machine had no
toolchain (no cl/g++/cmake, and Docker cannot start because WSL is not installed), so a
**portable** CMake + GCC 14.2 (WinLibs UCRT) was unpacked into the session scratchpad —
nothing installed system-wide, nothing to uninstall. Build was clean: 34 test TUs plus the
core library, **zero errors and zero warnings**, with `--timeout 120 --no-tests=error`
matching CI's invocation.

Two incidental confirmations from the run. The case count is **314 = the roadmap's 318
minus the four deleted `confidence` cases**, so the deletion propagated through CMake's
`tests/*.cpp` glob with no build-file edit. And `test_confidence.cpp` is absent from the
compile list.

**The one failure is pre-existing and unrelated:** *rollback_model swaps the deployed model
and quality metadata* throws `filesystem error: cannot copy file: File exists`. It was
reproduced at the pre-decision commit `697e77b` in a separate worktree, where it fails
identically — so it is not a regression from this work. Mechanism: `swap_file`
(`src/app/training_deploy.cpp:463`) passes `copy_options::overwrite_existing` on every copy,
which libstdc++ on MinGW does not honour on Windows. CI never sees it because its Windows
job uses MSVC and its Linux jobs use a different libstdc++ path. **Not fixed here** — out of
scope for this decision, and not a defect on any supported platform. Worth knowing only if
MinGW is ever added to CI.

The new fixture's hand arithmetic held up against the real run: drift ≈ 0.77 against the
0.55 bar, risk ≈ 0.15. Note `context_switches_30s` counts only `WindowFocusChange`, so that
scenario reaches PSEUDO_PRODUCTIVE via the *model's* argmax rather than the demote branch;
the demote path is covered by the unit test in `test_classifier_properties.cpp`.
