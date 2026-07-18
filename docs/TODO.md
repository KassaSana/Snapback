# TODO — feature tracking

Lightweight running log of feature work. One line per item. Newest on top.
Full backlog lives in [ROADMAP.md](ROADMAP.md); this file tracks what's *in flight*
and what just landed, so the next session can pick up without re-deriving state.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done

## In progress

- [x] **1.5 Idle/AFK detection** — core + wiring + `idle` event + AFK prediction freeze. Done.

## Done

- [x] **1.5c** Freeze prediction generation while idle (no AFK pollution of feature windows).
- [x] **1.5b** Wire `IdleDetector` into the engine tick; `is_idle()` + `idle` frontend event.
- [x] **1.5a** `IdleDetector` core state machine (`engine/idle_detector.hpp`) + doctest.
- [x] **0.2** Storage retention prune on open (predictions + context_snapshots > 90d).

## Next up (from ROADMAP suggested sequence)

- [ ] **2.6** Pomodoro / focus-timer core state machine.
- [ ] **2.4** Confidence calibration (suppress low-confidence nags).
- [ ] **4.1** Structured leveled logger.
- [ ] **4.6** Dependabot config.
