# ADR-0005: A session is declared by the user and attended by the user

- **Status:** Accepted
- **Date:** 2026-08-05

## Question

When is a session real? Three problems traced back to this:

- Nothing is recorded unless the user presses Start.
- Nothing ever stops a session, so a forgotten one reports the night as focus time.
- The app knows exactly what the user was doing, but never offers to resume it.

## Context

Everything hangs off the session. Without one, `AppState` writes nothing: no predictions, no
features, no context snapshots. Capture still runs, but its events are dropped.

At the other end, `duration_secs` was plain wall-clock time with no idle subtracted, and nothing
paused or stopped a session. One left open overnight reported fourteen hours, eight of them
asleep, and that number was the Review headline.

The fix had been designed but never wired up. `src/engine/idle_detector.hpp` reports
`WentIdle`/`WokeUp` transitions so a caller can pause and resume on them, and its comments say
five minutes of no input pauses the session. The only caller emitted a UI event and did nothing
else.

## Decision

**A session is real when the user started it and was present for it.** Starting stays manual;
presence is measured.

1. **Sessions are never auto-started.** Instead, a nudge ("you've been active for N minutes
   with no session") fires once per stretch of activity.
2. **Going idle pauses the session, and activity resumes it.**
3. **Attended time is stored as spans** in a `session_spans` table, and the duration is the sum
   of the spans.
4. **`duration_secs` keeps meaning elapsed time.** Attended time is shown alongside it as the
   headline.
5. **Auto-stop is optional.** Once paused time doesn't count, forgetting to press Stop no longer
   makes the numbers wrong.

## Why

**Auto-start would undermine what it's meant to help.** Goal alignment is one of the model's
features; a session with no declared goal produces alignment numbers that mean nothing, and
those rows end up in the training data. It also changes consent: recording window titles
because someone happened to be typing is different from recording because they asked. The nudge
fixes the real complaint (forgetting to start) without either problem.

**Pausing removes the duration problem instead of trading it off.** The alternatives were
auto-stopping (needs a threshold, and splits one work block into two) or redefining
`duration_secs` (silently changes every historical row). If time away is never counted, there's
nothing to subtract and nothing to restate.

**Spans, not a running counter**, because an in-memory counter is lost in a crash. Spans are
durable, make attended time a query, and give later analytics real data.

**Older sessions stay wall-clock.** Backfilling attended time without the idle history would be
inventing numbers.

**Pausing is the safe direction for privacy**: it records strictly less.

## Rejected

- **Always record, with sessions as labels over a continuous stream.** Arguably the better
  long-term model: the snapback overlay doesn't strictly need a goal. Rejected for v1 because it
  changes the data shape, the schema and the consent story. Worth revisiting if real use shows
  people constantly forget to start.
- **Auto-start with an inferred goal.** It guesses the one thing the user is supposed to say.
- **Redefine `duration_secs` in place.** Every stored session would silently change meaning.

## Consequences

- A migration adds `session_spans`, with a backup taken before migrating.
- Reported durations change at the release that ships this; the changelog says so.
- The idle threshold becomes a setting (five minutes by default), since reading a long document
  without touching the keyboard shouldn't count as away.
- Sessions have three states: running, paused and stopped. Every surface that shows a session
  says which.
