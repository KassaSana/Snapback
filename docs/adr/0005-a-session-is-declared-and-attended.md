# ADR-0005 — A session is declared by the user and attended by the user

- **Status:** Accepted
- **Date:** 2026-08-05
- **Roadmap item:** 2.7, 7.23, 2.8 (Decision session C)
- **Decided by:** Kassa

## Question

When is a session real? Three open items were three symptoms of one unanswered question:
nothing is recorded unless the user presses Start (2.7), nothing ever stops a session so a
forgotten one reports the night as focus time (7.23), and the one moment the app understands
the user's context perfectly it declines to act on it (2.8).

## Context

The session is the unit everything hangs off. `AppState` throws `no active session` and
**writes nothing at all** without one — no predictions, no features, no context snapshots.
Capture still runs; its events are discarded. So the product's entire value is gated on a
manual action taken at the moment a user is least likely to be thinking about tooling.

At the other end, `duration_secs` is computed as
`julianday(COALESCE(ended_at, CURRENT_TIMESTAMP)) - julianday(started_at)` — wall clock, no
idle subtracted — and `grep` for `auto_stop`, `pause_session`, or `resume_session` returns
nothing. A session left open overnight reports fourteen hours, eight of them sleep, and that
number headlines the Review surface and will become training context for 2.3.

**The decisive fact is that the fix was designed and never wired.** Two comments in
`src/engine/idle_detector.hpp` state it as contract:

> `// Default AFK threshold: 5 minutes of no input pauses the session.`

> `// What the last call did to the state. Callers act on the edges (pause/resume),`
> `// not the level, so we report the transition rather than making them diff state().`

`IdleTransition` reports `WentIdle`/`WokeUp` **edges** specifically so a caller can pause and
resume. The only caller (`AppState::compute_event`) emits a UI event and does nothing else.
The mechanism exists and the intent is written down; only the action is missing.

## Decision

**A session is real when the user declared it *and* was present for it.** Declaration stays
manual. Presence is measured rather than assumed.

1. **Sessions are never auto-started.** 2.7 is answered with a *nudge* — "you have been
   active N minutes with no session" — latched so it fires once per stretch.
2. **Idle pauses the session and activity resumes it**, implementing the contract quoted
   above.
3. **Active time is stored as spans**, in a new `session_spans` table. Duration is `SUM` over
   spans, not a running counter.
4. **`duration_secs` keeps its meaning** (elapsed). Active duration is reported alongside it
   as the headline number.
5. **Auto-stop is optional**, not urgent.
6. **2.8 ships independently**, after 4.11.

## Why

**Auto-starting sessions would poison the thing it was meant to serve.** `goal_alignment` is
one of the 31 features and 2.5 scores against it; a session with no declared goal produces
alignment numbers that mean nothing, and those rows feed 2.3's corpus. Auto-start also changes
the consent story — recording window titles because someone was typing is materially different
from recording because they asked, which cuts against 1.6 and the onboarding promise. The
nudge fixes the actual complaint (forgetting) and changes neither.

**Pausing dissolves 7.23 rather than trading it off.** That item framed a choice between
auto-stopping (needs a threshold nobody has chosen, and splits one work block into two) and
redefining `duration_secs` to active time (silently restates every historical row). Pausing
makes both unnecessary: time spent away is never counted in the first place, so there is
nothing to subtract and nothing to restate. Once a paused session accrues no time, forgetting
to press Stop costs correctness nothing, which is what demotes auto-stop from urgent to
tidy-up.

**Spans rather than an accumulated column**, because a running `active_secs` is mutable
in-memory state that a crash loses — the same shape as the bug 7.20 just fixed. Spans are
durable, make active duration a query rather than a counter, and give later analytics
("when do you actually work?") real data instead of a scalar.

**Pre-fix sessions stay wall-clock.** Backfilling active time for sessions whose idle history
was never recorded would be inventing numbers, which is worse than an honest discontinuity.

**Pausing is the safe direction on privacy**: it records strictly less. Predictions are
already suppressed while idle (7.10's `idle` suppression reason), so this aligns what is
stored with behaviour that already exists.

## Options rejected

**Always record; sessions become labels over a continuous stream.** Arguably the better
long-term model — the snapback feature genuinely does not need a goal to function, so gating
it on a goal-tagged session under-delivers the namesake feature. Rejected **for v1 only**: it
invalidates the corpus shape, needs migrations, changes the consent story, and delays the one
thing actually blocking release (3.3). Revisit after v1 if real use shows people forget
constantly.

**Auto-start with an inferred goal** from the context tracker. Most useful, most presumptuous,
and it guesses at precisely the thing the user is supposed to declare.

**Redefine `duration_secs` in place.** Every stored session silently changes meaning, and no
reader could tell which definition a given historical number was written under.

## Consequences

- A schema migration adds `session_spans`. **7.22's pre-migration backup should land first**,
  so the first migration written after this decision is also the first one with a safety net.
- Reported duration becomes discontinuous at the release that ships this. The changelog must
  say so plainly.
- The 5-minute idle threshold is inherited, not chosen. It is likely too aggressive for
  reading a long document without input, so it becomes a setting, and whether the capture
  layer counts scroll and mouse-move as activity needs checking before it is trusted.
- Sessions gain a third observable state (running / paused / stopped). Every surface that
  renders "active session" has to say which.
