# TODO — feature tracking

Lightweight running log of feature work. One line per item. Newest on top.
Full backlog lives in [ROADMAP.md](ROADMAP.md); this file tracks what's *in flight*
and what just landed, so the next session can pick up without re-deriving state.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done

## In progress

- [x] **1.5 Idle/AFK detection** — core + wiring + `idle` event + AFK prediction freeze. Done.

## Done

- [x] **1.4a** Notification payload builders (`app/notification.hpp`) — distraction + hyperfocus.
- [x] **2.2b** Expose `AppState::focus_summary()` over recent predictions.
- [x] **2.2a** Focus summary aggregation (`engine/focus_summary.hpp`) — avg/peak/streak math.
- [x] **4.6** Dependabot config (`.github/dependabot.yml`) — github-actions + frontend npm.
- [x] **4.1a** Leveled logger core (`util/logger.hpp`) — levels, filtering, injectable clock.
- [x] **2.4a** Confidence calibration helpers (`engine/confidence.hpp`) — gate low-conf nags.
- [x] **2.6a** Pomodoro / focus-timer core state machine (`engine/pomodoro.hpp`) + doctest.
- [x] **1.5c** Freeze prediction generation while idle (no AFK pollution of feature windows).
- [x] **1.5b** Wire `IdleDetector` into the engine tick; `is_idle()` + `idle` frontend event.
- [x] **1.5a** `IdleDetector` core state machine (`engine/idle_detector.hpp`) + doctest.
- [x] **0.2** Storage retention prune on open (predictions + context_snapshots > 90d).

## Next up (from ROADMAP suggested sequence)

- [x] **1.3** Windows start-on-login / autostart — Run-key backend, IPC, settings toggle,
  and tests.
- [x] **1.6** Privacy controls — private mode, app exclusions, local-only statement, and tests.
- [x] **2.1** Analytics / trends — hourly aggregates, top apps, streaks, and frontend views.
- [x] **2.2** Daily / weekly summary — report windows, JSON export, and frontend controls.
- [x] **2.5** Goal categories — persisted editable keywords wired into goal alignment.
- [x] **4.10** Diagnostics — health state, recent logger tail, IPC, panel, and tests.
- [x] **1.4b** Win32 toast delivery consuming `NotificationPayload` via the existing tray icon.
- [x] **4.1b** Logger file sink + rotation (`RotatingFileStream`, bounded backups).
- [x] **2.6b** Wire Pomodoro timer into AppState + IPC commands + transition events.
- [ ] **2.4b** Gate the distraction nag through `should_nag`.
