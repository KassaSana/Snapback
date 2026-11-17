# ADR-0003: Split the dashboard into Now, Review and Settings

- **Status:** Accepted
- **Date:** 2026-07-25

## Question

The dashboard was sixteen cards in one scrolling list. How should it be organised so the next
feature doesn't make it worse?

## Context

- **Two different modes were fused.** Working (a session is running and you glance at it) and
  reflecting (looking back at the day or week). A running session doesn't need a weekly chart;
  a review doesn't need a Start button.
- **The main view led with debugging telemetry:** thrash, drift, goal fit, classifier backend
  and model id, all above the fold.
- **The score's meaning was still undecided**, and it was shown as `71.2`. A layout built
  around that number would need redoing once the meaning was settled.
- Features were still being added, and a flat list only gets worse.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. Three surfaces: Now, Review, Settings | Each screen has one job; new features have an obvious home | The most work: navigation, routing, per-surface empty states |
| B. One page with a fixed hierarchy | Cheapest | Degrades again with the next few features |
| C. Two surfaces: Focus (now + review) and Settings | Fewer clicks | Charts stay on the working screen, which is the problem |

## Decision

Three surfaces, with navigation in the header.

| Surface | Contains |
|---------|----------|
| **Now** (default) | The session controls, the live focus state, the Pomodoro timer |
| **Review** | Summary, session explorer, trends, top apps, activity timeline |
| **Settings** | Rules, preferences, privacy, permissions, goal categories, diagnostics, developer tools |

The permission wizard, header and error banner sit outside the surfaces.

The Now surface leads with the focus **state** (Productive, Drift, …), not the score. The score
and risk are secondary and shown without decimals. Debug telemetry moves to Diagnostics.

The visual direction stays: cream background, serif headings, rounded cards, coral accent.
What changes is hierarchy, spacing and wording.

## Why

The two modes really are different jobs, and every cheaper option leaves them fused. Option B
fixes the symptom (everything competes) but not the cause (nothing has a place).

Leading with the state also means the redesign doesn't depend on the scoring decision: a state
label still reads correctly if the score is rescaled.

Keeping the palette separates two questions: "is it better organised?" and "do I like the new
colours?". Only the first one is being answered here.

## Consequences

- `App.tsx` becomes a shell plus surface routing. Cards move; they aren't rewritten.
- Each surface needs its own empty state.
- Navigation needs proper roles, focus handling and keyboard support.

## Revisit if

A second user with a different workflow shows up, or Now grows past about five elements.
That would mean the split is in the wrong place, not that splitting was wrong.
