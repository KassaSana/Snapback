# ADR-0004 — The state is the policy verdict, the scores are the model's opinion

- **Status:** Accepted
- **Date:** 2026-08-03
- **Roadmap item:** 7.7, 7.18, 5.3, 5.4, 1.2 (Decision session A)
- **Decided by:** Kassa

## Question

A stored prediction carries continuous scores and a discrete `focus_state`, and the
guardrails can make them contradict each other. Who owns `focus_state`, what is policy
allowed to overrule, and what does the user actually see?

## Context

The classifier produces two kinds of output that were never named as different things:

- **Scores** — `focus_score`, `distraction_risk`, `thrash_score`, `drift_score`,
  `goal_alignment`. Continuous, computed from the class probabilities and behavioral
  signals (`classifier.cpp`, `scores_from_probas`).
- **State** — `focus_state`, the argmax class, which `apply_focus_guardrails` then
  overwrites: risk over `risk_threshold(mode)`, thrash over 0.75, or a Block rule forces
  `DISTRACTED`; high drift rewrote anything but `DEEP_FOCUS` to `PSEUDO_PRODUCTIVE`.

Five open items were all frictions at this one seam:

- **7.7** — a Block-rule row could read `focus_state = 'DISTRACTED', focus_score = 95`,
  and every surface mixed the two channels without comment. The Now hero even colored the
  state word by `riskLevel(distraction_risk)`, so a blocked-app verdict rendered
  "Distracted" painted calm.
- **7.18** — the drift branch excluded only `DEEP_FOCUS`, so it *upgraded* a
  model-DISTRACTED row (risk under the mode threshold, drift ≥ 0.55) to
  `PSEUDO_PRODUCTIVE` — a guardrail making a distracted user look better. Found by 11.2's
  property tests; pinned by a characterization test until this decision.
- **5.4** — `thrash_spikes` counted `distraction_risk >= 0.7 AND focus_state =
  'DISTRACTED'`: an absolute opinion bar AND'd with the mode-dependent verdict, so
  Recovery rows in the 0.70–0.85 band were never counted — defeating the stated reason
  the 0.7 is absolute (mode-independent session metrics feeding auto-labels).
- **5.3** — `confidence.hpp` was a stillborn policy layer: dead code, `[0,100]` units
  against a `[0,1]` producer, and no risk-driven nag anywhere for `should_nag` to gate.
  The debounce it promised already exists in `ContextTracker` (30s of continuous off-task
  before a snapback fires).
- **1.2** — "distraction sensitivity" was unanswerable while nobody could say which
  numbers are policy and which are structure. 7.15 sorted the constants by role; the
  policy group plus `risk_threshold(mode)` is the whole answer space.

Forces pulling in opposite directions: the contradiction is user-visible and looks like a
bug; but `focus_score` feeds back into the feature vector as `focus_momentum`
(`state.cpp`), so "fixing" the contradiction by clamping the score would leak policy into
the model's own inputs. And the model's argmax is not stored — once the guardrail
overwrites `focus_state`, nothing can reconstruct what the model believed.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Clamp the scores when policy overrides the state | No visible contradiction | Poisons the `focus_momentum` feedback loop; destroys the model-vs-policy disagreement signal 2.3 needs; splits `avg_focus_score`'s meaning across a migration boundary |
| B. Two named channels; policy demote-only; provenance column (chosen) | Contradiction becomes a sentence; momentum stays model-pure; the UI can explain a Block-rule verdict | One new nullable column; the channels must be labeled wherever they meet |
| C. Drop the score from every state-bearing surface | No contradiction, no schema change | Throws away the evidence line ADR-0003 built; hides real information to avoid explaining it |

## Decision

**Two channels.** The scores are the **model's opinion** and policy never edits them. The
`focus_state` is the **policy verdict** — the single value the app acts on (the snapback
tracker, distracted counts, streaks, the hero word).

**Policy is demote-only.** A guardrail may move the state toward distraction, never away
from it. Concretely, the drift branch fires only on `PRODUCTIVE` (7.18 fixed); the
`DEEP_FOCUS` exemption stays; `DISTRACTED` is never softened.

**Verdicts carry provenance.** `predictions.state_source` (migration 3, nullable TEXT)
records which rule decided the state: `model`, `risk`, `thrash`, `block`, or `drift`.
NULL means "written before provenance existed" — unknown, not `model`; nothing backfills
it, because nothing can.

**The UI labels the channels.** The hero's word and color both come from the verdict
(`verdictLevel`); the score and risk stay on the evidence line as the opinion; a policy
override is stated as the leading reason ("a blocked app is open") and suppresses the
contradictory calm-behavior phrases.

**`thrash_spikes` is pure opinion:** `distraction_risk >= 0.7`, no state conjunct. The
absolute bar keeps meaning "strong distraction moment" in every mode. The Review tile is
labeled "Distraction spikes"; the wire name stays.

**Sensitivity is the mode.** Deep / Normal / Recovery *are* the user-facing sensitivity
control (`risk_threshold` = 0.55 / 0.70 / 0.85). v1 ships no additional tunable (1.2
closed). If evidence ever demands finer control, it is a per-mode threshold override in
`AppSettings` — an offset to policy, never a transform on the opinion.

**`confidence.hpp` is deleted** (5.3), and the Done archive's 2.4 "confidence gating"
claim is retracted.

**Existing rows are left as written.** No rewriting migration exists that would not
fabricate data; this ADR dates the semantics change, and the 90-day retention window ages
the mixed-era rows out.

## Why

B wins because the contradiction was never the defect — the *silence* about it was. A row
saying "the model thought you were deep in; policy says the app is blocked" is useful
exactly because both halves survive. A (clamping) reads as the obvious fix and is the
trap: `update_focus_score` feeds the score back into `focus_momentum`, so clamped scores
would drag the model's own features toward whatever policy said, a feedback loop nobody
designed — and it would erase the disagreement signal that tells 2.3 where the heuristic
and the user's rules part ways. C avoids the loop but answers a communication problem by
hiding information.

Demote-only is what makes "guardrail" a true name: rule 1 was already demote-only in
effect, and the drift branch's `!= "DEEP_FOCUS"` was an oversight, not a considered
asymmetry — the author was thinking about exemptions and missed `DISTRACTED`. The
property "guardrails never raise the state" failed on 246 of 188,502 assertions before
this change; it is now asserted.

For A to have been right, `focus_score` would have to stop feeding `focus_momentum` and
the model's raw argmax would have to be stored separately — at which point clamping would
be relabeling, not destroying. Neither is true today.

## Consequences

- `apply_focus_guardrails` branches record `state_source`; the drift branch fires only on
  `PRODUCTIVE`. Weakly-distracted high-drift moments are now off-task for the snapback
  tracker — the guardrail doing its job.
- Migration 3 adds `predictions.state_source`; `kSchemaVersion` is 3.
- The characterization test in `test_classifier_properties.cpp` became the demote-only
  property. The recap fixtures pin the new spike predicate, including a Recovery-band row
  the old predicate missed.
- `thrash_spikes` counts rise slightly for Recovery-mode sessions, so auto-labels via
  `infer_session_label` shift toward Distracted/Pseudo for those sessions — a correction
  in the training corpus, not drift.
- Old rows may contain drift-softened DISTRACTED states; they are dated by this ADR and
  age out with retention. `state_source` NULL marks them.
- Forecloses: policy rules that rescale or clamp scores; a user-facing sensitivity slider
  in v1.
- Unblocks: the last decision on the ADR-0002 v1 blocker list. 2.3's label corpus keeps
  the model-vs-policy disagreement signal.

## Revisit if

2.3 replaces the heuristic with a trained model whose calibrated probabilities make the
opinion trustworthy enough to *be* the verdict — then the guardrail layer shrinks to user
rules only and this split should be re-argued. Or a real user reads "Distracted · focus
95" with the labels in place and still files it as a bug: that would mean labeling lost
to intuition, and C (dropping the score from state surfaces) deserves a second look.
