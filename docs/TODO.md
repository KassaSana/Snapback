# TODO — feature tracking

Lightweight running log of feature work. One line per item. Newest on top.
Full backlog lives in [ROADMAP.md](ROADMAP.md); this file tracks what's *in flight*
and what just landed, so the next session can pick up without re-deriving state.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done

## In progress

- [~] **1.5 Idle/AFK detection** — pure `IdleDetector` state machine landed + tested.
      Next: wire it into the engine loop so idle pauses capture + the active session.

## Done

- [x] **1.5a** `IdleDetector` core state machine (`engine/idle_detector.hpp`) + doctest.
- [x] **0.2** Storage retention prune on open (predictions + context_snapshots > 90d).

## Next up (from ROADMAP suggested sequence)

- [ ] **1.5b** Wire `IdleDetector` into the engine tick: pause on idle, resume on activity.
- [ ] **0.1** Feature-parity fixture harness + dual-language CI.
- [ ] **4.6** Dependabot config.
- [ ] **4.7** Security-audit CI job.
