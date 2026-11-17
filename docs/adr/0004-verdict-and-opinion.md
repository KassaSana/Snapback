# ADR-0004: The state is the policy verdict, the scores are the model's opinion

- **Status:** Accepted
- **Date:** 2026-08-03

## Question

A prediction has continuous scores and a discrete `focus_state`, and the guardrails can make
them contradict each other. Who owns `focus_state`, what can policy overrule, and what does the
user see?

## Context

The classifier produces two kinds of output that were never named as different things:

- **Scores:** `focus_score`, `distraction_risk`, `thrash_score`, `drift_score` and
  `goal_alignment`, computed from the class probabilities and behaviour
  (`classifier.cpp:scores_from_probas`).
- **State:** `focus_state`, the argmax class, which `apply_focus_guardrails` then overrides.
  High risk (above `risk_threshold(mode)`), thrash above 0.75 or a Block rule forces
  `DISTRACTED`. High drift rewrote anything except `DEEP_FOCUS` to `PSEUDO_PRODUCTIVE`.

That caused several problems:

- A Block-rule row could read `DISTRACTED` with `focus_score = 95`, and the UI coloured the
  state word by risk, so "Distracted" showed up painted calm.
- The drift rule could **upgrade** a model-`DISTRACTED` moment to `PSEUDO_PRODUCTIVE`, so a
  guardrail made a distracted user look better. Property tests found it.
- The thrash-spike count required both `distraction_risk >= 0.7` and a `DISTRACTED` state, so
  Recovery mode (threshold 0.85) never counted spikes in the 0.70–0.85 band.
- A `confidence.hpp` gating layer was dead code, written on a `[0,100]` scale while the
  classifier produces `[0,1]`.

The forces pull against each other. The contradiction looks like a bug to a user. But
`focus_score` feeds back into the features as `focus_momentum`, so clamping it would leak
policy into the model's own inputs. And once the guardrail overwrites the state, nothing
records what the model thought.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. Clamp the scores when policy overrides the state | No visible contradiction | Corrupts `focus_momentum`, and loses the model-vs-policy disagreement that retraining needs |
| B. Two named channels; policy can only demote; record which rule decided | The contradiction becomes explainable; scores stay pure | One new column; the UI has to label both channels |
| C. Hide the score wherever the state is shown | No contradiction, no schema change | Hides real information instead of explaining it |

## Decision

**Two channels.** The scores are the model's opinion, and policy never edits them. The
`focus_state` is the policy verdict, the one value the app acts on: snapbacks, distracted
counts, streaks and the Now headline.

**Policy only demotes.** A guardrail can move the state toward distraction, never away from it.
The drift rule only fires on `PRODUCTIVE`; `DEEP_FOCUS` stays exempt; `DISTRACTED` is never
softened.

**Verdicts record their source.** `predictions.state_source` (migration 3, nullable) is
`model`, `risk`, `thrash`, `block` or `drift`. NULL means the row predates the column; nothing
backfills it.

**The UI labels the channels.** The headline word and its colour come from the verdict. Score
and risk are shown as supporting evidence. When policy overrode the model, the reason is shown
first ("a blocked app is open").

**Thrash spikes are opinion only:** `distraction_risk >= 0.7`, regardless of state.

**The focus mode is the sensitivity setting.** Deep, Normal and Recovery map to
`risk_threshold` 0.55, 0.70 and 0.85. There's no separate slider in v1. If finer control is
ever needed, it should be a per-mode threshold override, never a transform on the scores.

`confidence.hpp` is deleted. Existing rows are left as written and age out with retention.

## Why

The contradiction was never the bug; the silence about it was. "The model thought you were
focused; policy says that app is blocked" is useful precisely because both halves survive.
Clamping is the obvious fix and the trap: it feeds policy back into `focus_momentum` and erases
the disagreement signal.

Demote-only is what makes "guardrail" an accurate name. Before this change the property
"guardrails never raise the state" failed on 246 of 188,502 generated cases. It's now asserted
in `test_classifier_properties.cpp`.

## Consequences

- `apply_focus_guardrails` records `state_source`, and the schema version is 3.
- Spike counts rise slightly for Recovery-mode sessions, which shifts their auto-labels.
- Rules this out: policy that rescales or clamps scores, and a separate sensitivity slider in
  v1.

## Revisit if

A trained model's calibrated output is trustworthy enough to be the verdict itself; then
guardrails shrink to user rules. Or users still read "Distracted · focus 95" as a bug even with
the labels; then option C deserves another look.
