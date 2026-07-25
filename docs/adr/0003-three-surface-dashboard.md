# ADR-0003 — Split the dashboard into Now, Review, and Settings

- **Status:** Accepted
- **Date:** 2026-07-25
- **Roadmap item:** 10.2
- **Decided by:** Kassa

## Question

The dashboard renders sixteen cards in one flat scrolling list. What is the information
architecture — how should the UI be organized so that adding the next feature does not make
it worse?

## Context

`frontend/src/App.tsx` composes every card as a sibling: `LiveStatusCards`,
`SessionControlCard`, `PomodoroCard`, `TrainingDeployCard`, `InsightsCard`, `AnalyticsCard`,
`SummaryCard`, `GoalCategoriesCard`, `DiagnosticsCard`, `FocusSummaryCard`, `ActivityCards`,
`SessionReviewCards`, `RulesCard`, `SettingsCard`, `PrivacyCard`, `PermissionsCard`. There is
no navigation, so everything competes for attention permanently and the only hierarchy is
scroll position.

Forces that make this a real choice:

- **The app has two distinct modes that are currently fused.** *Working* (a session is
  running; the user glances at it) and *reflecting* (looking at yesterday, the week, what
  broke focus). A running session does not need a weekly chart on screen; a weekly review
  does not need a Start button.
- **The main view leads with engineering telemetry.** `Thrash: 6% · Drift: 0% · Goal fit:
  50%`, classifier backend, and model identity are all above the fold. Those exist to debug
  the pipeline, not to help someone focus. Verified against a live run 2026-07-25.
- **The score's meaning is undecided.** `focus_score` is displayed as `71.2` — one decimal of
  precision for a number whose scale is still an open question (Roadmap 5.3, 5.4, 1.2, 7.7;
  `src/engine/confidence.hpp` is dead code whose `[0,100]` threshold cannot fire against a
  `[0,1]` producer). Any layout that makes the number the hero will need rebuilding once that
  decision lands.
- **Feature count is still growing.** Tier 9 alone adds empty states and single-instance
  handling; Tier 13 adds model lifecycle UI. A flat list degrades monotonically.
- **There is exactly one user today.** So "what would a stranger expect" is a weaker
  constraint than "what does Kassa actually do all day."

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Three surfaces — Now / Review / Settings (chosen) | Each screen has one job; new features land in an obvious place | Most work; needs nav, routing, and per-surface empty states |
| B. One page, fixed hierarchy | Cheapest; large hero plus collapsed sections | Degrades again with the next five features — the same failure, deferred |
| C. Two surfaces — Focus (now + review) / Settings | Fewer clicks; today and the week visible while working | The working screen still carries charts, which is the specific problem |

## Decision

**Three surfaces, with navigation in the header.**

| Surface | Contains |
|---------|----------|
| **Now** (default) | `SessionControlCard`, the live state, `PomodoroCard`. The goal, a state word, elapsed time, and the primary action |
| **Review** | `InsightsCard`, `AnalyticsCard`, `SummaryCard`, `FocusSummaryCard`, `SessionReviewCards`, `ActivityCards` (Recent Predictions, Context Timeline) |
| **Settings** | `RulesCard`, `SettingsCard`, `PrivacyCard`, `PermissionsCard`, `GoalCategoriesCard`, `TrainingDeployCard`, `DiagnosticsCard` |

`PermissionWizard`, `AppHeader`, and `ActionErrorBanner` stay outside the surfaces — they are
app-level chrome, not content.

**The Now surface leads with the focus *state*, not the score.** `PRODUCTIVE` / `DRIFTING` is
the hero; `focus 71 · risk 21%` is secondary and rendered with no decimal places. Thrash,
drift, goal fit, classifier backend, and model identity move to Diagnostics under Settings.

**Visual direction: keep the palette, fix the layout.** The cream background, serif display
headings, rounded cards, and coral primary accent stay. What changes is hierarchy, a
consistent spacing scale, fewer and larger numbers, and jargon removal.

## Why

Option A wins because the two modes are genuinely different jobs, and every cheaper option
leaves them fused. B was tempting — it is a fraction of the work — but it fixes the *symptom*
(everything shouts) while preserving the *cause* (no place for anything to go), so the next
five features re-create the problem. C keeps charts on the working screen, which is the exact
complaint.

Leading with the state rather than the score is the load-bearing detail. It makes this
redesign **independent of Decision session A**: whatever `focus_score` turns out to mean, the
Now surface still reads correctly, because a state label survives a rescaling that a
hero number would not. That is deliberate sequencing, not indecision — it lets the UI work
proceed without pre-empting a decision that must not be pre-empted.

Keeping the palette is a judgment that the current look is distinctive and the problem is
structural. Restyling at the same time would confound "is this better organized?" with "do I
like the new colors?", and only one of those questions is answerable by a test.

## Consequences

- `App.tsx` stops being a flat card list and becomes a shell plus surface routing. No card
  component needs rewriting to move — this is composition, not a rewrite.
- Each surface needs an empty state (Roadmap 9.7), which is now a per-surface question rather
  than one page-wide one.
- Diagnostics stops being a peer of the live view. `DiagnosticsCard` keeps its content but
  loses its prominence.
- Accessibility (Roadmap 10.3) gets a natural home: nav needs roles, focus management, and
  keyboard support, assessed once for the shell instead of per card.
- Does **not** unblock or pre-empt 5.3 / 5.4 / 1.2 / 7.7. The score keeps its current
  rendering, only demoted.
- The frontend is a plain React surface with no native implementation dependency, so there is no reference
  implementation to check against — this ADR is the specification.

## Revisit if

A second user appears whose workflow differs (the "one user" assumption is doing real work
here), or the Now surface accumulates more than about five elements — which would mean the
split was drawn in the wrong place, not that splitting was wrong.
