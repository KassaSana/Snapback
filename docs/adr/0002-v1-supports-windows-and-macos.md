# ADR-0002 — v1 supports Windows and macOS

- **Status:** Accepted
- **Date:** 2026-07-24 (sub-decision resolved and accepted 2026-07-25)
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
- **macOS is the author's own machine and the least verified platform.** Development
  happens on Darwin. The `CGEventTap` in
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
2. **3.1** — macOS tray **and native `NSPanel` overlay** (per the resolved sub-decision).
3. **3.3** — macOS `.app`/DMG packaging **and notarization**. Longest lead time (Apple
   Developer account, certificates); start early even though it lands late.
4. **macOS launch smoke in CI** — extend beyond the existing build-only job.
5. **Decision session A** (5.3, 5.4, 1.2, 7.7) — what the scores mean. A polished UI over
   an unspecified score is shippable and indefensible.
6. **7.3** — schema migrations, or an explicit retraction of the `focoflow.db`
   compatibility promise.

**Fast-follow (not blocking)**

- macOS autostart via launchd.
- macOS toast delivery via `UNUserNotificationCenter`, once 3.3 provides a bundle ID.
- Linux desktop overlay/tray (3.2).
- 2.3 / Tier 13 remainder — on-device retraining.

**Open sub-decision:** does macOS v1 need a *native overlay*, or is a notification enough?
The overlay is the largest single item in the blocker list, so this question is worth
settling before 3.1 starts. Recorded here rather than assumed.

> **Correction, 2026-07-25 — this sub-decision was stated on a false premise.** The
> sentence "Toasts already work" was written into a macOS-scoped paragraph and is **not
> true on macOS.** All three notification call sites (`src/main.cpp:main`)
> go through `Tray::instance().show_notification()`, and on macOS that resolves to
> `NoopTray::show_notification()`, which **returns `false` without ever calling the OS**
> (`src/app/tray_stub.cpp`). Toasts work on Windows only. What exists cross-platform is the
> *payload builders* in `src/app/notification.hpp` — the copy, not the delivery.
>
> Two consequences for the choice:
>
> 1. **Neither option is free.** "Notification is enough" was the cheap branch because it
>    was believed to be already built; it is not. Both branches require new native macOS
>    code, so the question is how much, not whether.
> 2. **The cheap branch has a dependency the expensive one does not.**
>    `UNUserNotificationCenter` requires a valid bundle identifier and refuses to post from
>    an unbundled binary — so notification delivery on macOS is **gated on 3.3** (`.app`
>    bundle, and in practice the Apple Developer account behind it), the longest-lead-time
>    blocker on this list. A native `NSPanel` overlay has no such gate and can be built and
>    run today.
>
> Also worth weighing: macOS is not silent today. `AppState` emits the `snapback` event and
> the React UI renders it with a working Dismiss (`src/snapback/overlay_stub.cpp` explains
> why that path must stay reachable). But that only reaches a user who is *looking at
> Snapback* — and the entire premise of a snapback is reaching someone who is looking at
> something else. So macOS v1 does need some out-of-app delivery; this sub-decision picks
> which.

**Sub-decision resolved 2026-07-25: macOS v1 ships a native `NSPanel` overlay.** 3.1 is
therefore tray *and* overlay, mirroring `src/snapback/overlay_windows.cpp`.

The correction above is what settled it. Before it, this looked like "expensive parity vs.
cheap toast" and cheap was winning. Once toasts turned out to be unbuilt on macOS *and*
gated on a bundle identifier, the comparison inverted: the notification branch is smaller
code that **cannot ship until 3.3 and an Apple Developer account land**, while the overlay
branch is larger code with **no external dependency at all** — buildable and runnable on the
author's own machine today. Choosing the overlay takes v1's critical path off Apple's
paperwork, which is the single least controllable item on the blocker list.

Secondary reasons, in order of weight: Windows and macOS then behave identically, so there
is one mental model of what a snapback *is* rather than two; the overlay is the product's
namesake feature and demoting it to a toast on the author's daily-driver platform is exactly
the kind of quiet scope loss this ADR exists to prevent; and `overlay_stub.cpp`'s note about
`ContextTracker::Recovering` having exactly one exit means a real overlay restores the
dismiss path natively instead of depending on the web UI being visible.

Toast delivery on macOS becomes **fast-follow**, not dropped — once 3.3 bundles the app, the
payload builders in `src/app/notification.hpp` are already written and cross-platform, so
delivery is the only missing piece.

**Accepting this closes 9.1.** Every blocker below is now a scoped piece of work rather than
an open question.

> **Progress note, updated 2026-07-29.** Four of the six blockers are now cleared: **0.3**
> (live-Mac capture, 2026-07-25), **3.1** (macOS tray + native `NSPanel` overlay,
> 2026-07-28), the **macOS launch smoke** (`macos-gui-smoke` in `ci.yml`, 2026-07-28), and
> **7.3** (schema versioning with an ordered migration list and a downgrade guard,
> 2026-07-29). **Neither remaining blocker is implementation work** — one is an Apple
> Developer account, the other is Decision session A. The Context section above is left
> as written — it describes the state at the time of the decision, and its claim that "tray
> and overlay are deliberate no-ops" on macOS is no longer true of the code. The live
> blocker table is in [ROADMAP.md](../ROADMAP.md), which is the source of truth; it also
> records that 3.1 and the smoke were verified by hand on the author's Mac but have not yet
> run on a CI runner. **The status of this ADR is unchanged — `Accepted`, not superseded.**

> **Progress correction, 2026-08-01.** The final sentence above is now historical: PR #40's
> hosted `macos-gui-smoke` passed on GitHub's macOS runner with the other 14 CI jobs. Four of
> six blockers remain cleared; the two open blockers are still 3.3 packaging/notarization and
> Decision session A. The live table remains in [ROADMAP.md](../ROADMAP.md).

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
  macOS and stay acceptable on Linux. *(Carried out 2026-07-28: both now guard on
  `!_WIN32 && !__APPLE__`, so Linux is their only remaining consumer.)*
- Linux desktop link coverage in CI stays — it is cheap and it caught a real bug — but a
  Linux desktop *bug* is no longer a release blocker.
- Unblocks prioritization of Tiers 3, 7, and 9. Does not unblock any coding on Decision
  session A, which still needs its own ADR.

## Revisit if

The author's primary development machine changes, or the first real users land on a
platform this ADR deferred. A Linux request alone is not enough — capture works there
already, so re-scope only if someone wants the desktop shell.
