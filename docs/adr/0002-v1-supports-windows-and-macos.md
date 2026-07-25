# ADR-0002 — v1 supports Windows and macOS

- **Status:** Proposed
- **Date:** 2026-07-24
- **Roadmap item:** 9.1
- **Decided by:** Kassa

## Question

What has to be true before this is version 1, and which platforms does v1 claim to
support? Without an answer, all ~80 open roadmap items look equally required and nothing
can be deferred on principle.

## Context

The port itself is finished and the pipeline runs end to end, so what remains is not
"make it work" but "decide what shipping means." The forces that make this a real choice:

- **Windows is the most verified platform and the least dogfooded.** Capture, overlay,
  and tray are compiled and CI-smoke-tested (`src/app/tray_windows.cpp`,
  `src/snapback/overlay_windows.cpp`), and start-on-login works via the HKCU Run key
  (`src/app/autostart.cpp`). None of it has ever been used interactively for a day.
- **macOS is the author's own machine and the least verified platform.** Per
  [CLAUDE.md](../../CLAUDE.md), development happens on Darwin. The `CGEventTap` in
  `src/capture/input_hook_macos.mm` was fixed on 2026-07-20 and has never run on real
  hardware with Accessibility permission granted (Roadmap 0.3). Tray and overlay are
  deliberate no-ops (`src/app/tray_stub.cpp`, `src/snapback/overlay_stub.cpp`), and there
  is no packaging or notarization path.
- **The desktop app already builds on macOS in CI.** The `Desktop app build /
  macos-latest` job is green. What is missing is a *launch* smoke check, not a build
  check — worth stating because a previous audit nearly rebuilt work that existed.
- **Linux desktop support has cost and no user.** Its first real CI run surfaced X11 macro
  pollution (fixed via `src/app/webview_compat.hpp`). Capture via evdev is real, but
  nobody is asking for the desktop half.
- **Not all v1 risk is platform-shaped.** Two open questions bind regardless of OS: what
  the scores mean (Roadmap 5.3, 5.4, 1.2, 7.7 — `src/engine/confidence.hpp` is currently
  dead code whose `[0,100]` threshold cannot fire against a `[0,1]` classifier output),
  and whether `focoflow.db` stays compatible with databases created by earlier releases when there are
  no migrations at all (Roadmap 7.3).

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Windows-only v1 | Ships soonest; the platform with the most CI coverage | The author cannot run it. A focus tracker that its own author never lives with will not get the feedback that makes it good |
| B. Windows + macOS (chosen) | Author can dogfood daily; covers the two platforms with real capture backends | Adds tray, overlay, packaging, notarization, and one live-hardware verification to the blocker list |
| C. All three desktop OSes | No platform caveats anywhere | Linux desktop doubles the overlay/tray/packaging surface for a user who does not exist yet |

## Decision

**v1 supports Windows and macOS.** Linux keeps its headless capture and CI coverage but is
not a supported desktop target in v1.

**Release blockers**

1. **0.3** — verify `CGEventTap` on real Mac hardware with Accessibility granted. First,
   because everything else on macOS is decoration if capture does not record.
2. **3.1** — macOS tray.
3. **3.3** — macOS `.app`/DMG packaging **and notarization**. Longest lead time (Apple
   Developer account, certificates); start early even though it lands late.
4. **macOS launch smoke in CI** — extend beyond the existing build-only job.
5. **Decision session A** (5.3, 5.4, 1.2, 7.7) — what the scores mean. A polished UI over
   an unspecified score is shippable and indefensible.
6. **7.3** — schema migrations, or an explicit retraction of the `focoflow.db`
   compatibility promise.

**Fast-follow (not blocking)**

- macOS autostart via launchd.
- Linux desktop overlay/tray (3.2).
- 2.3 / Tier 13 remainder — on-device retraining.

**Open sub-decision:** does macOS v1 need a *native overlay*, or is a notification enough?
Toasts already work. The overlay is the largest single item in the blocker list, so this
question is worth settling before 3.1 starts. Recorded here rather than assumed.

## Why

Option B wins on **dogfooding**, not on user demand. The author's machine is macOS, so a
Windows-only v1 is a product its own author cannot live with — and this project's stated
goal is understanding and defending the thing, which requires using it. Windows stays in
scope because it is already the most verified surface and dropping it would waste the
overlay, tray, and autostart work that is done.

Option A would flip back into contention if the author's primary machine changed to
Windows, or if real users appeared on Windows first and macOS work started delaying them.
Option C would flip if a Linux user materialized — capture already works there, so the
increment is the desktop shell, not the engine.

Blockers 5 and 6 are on the list deliberately even though they are not platform work. The
platform framing hides them, and both are cases where the repo currently promises
something it does not deliver.

## Consequences

- The roadmap gains a real filter: an item is v1 work only if it appears above, and
  everything else is explicitly post-v1. This is what makes ~80 items tractable.
- `src/snapback/overlay_stub.cpp` and `src/app/tray_stub.cpp` stop being acceptable on
  macOS and stay acceptable on Linux.
- Linux desktop link coverage in CI stays — it is cheap and it caught a real bug — but a
  Linux desktop *bug* is no longer a release blocker.
- Unblocks prioritization of Tiers 3, 7, and 9. Does not unblock any coding on Decision
  session A, which still needs its own ADR.

## Revisit if

The author's primary development machine changes, or the first real users land on a
platform this ADR deferred. A Linux request alone is not enough — capture works there
already, so re-scope only if someone wants the desktop shell.
