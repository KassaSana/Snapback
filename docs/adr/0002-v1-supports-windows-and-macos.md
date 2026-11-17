# ADR-0002: v1 supports Windows and macOS

- **Status:** Accepted
- **Date:** 2026-07-24 (overlay sub-decision 2026-07-25)

## Question

Which platforms does v1 support, and what has to be true before it ships?

## Context

When this was decided:

- **Windows** had the most CI coverage (capture, overlay, tray, start-on-login) but hadn't been
  used day to day.
- **macOS** was the author's own machine but the least verified. The `CGEventTap` capture
  hadn't run on real hardware, tray and overlay were stubs, and there was no packaging.
- **Linux** had working evdev capture but no users asking for the desktop app, and the desktop
  build had already cost time (X11 macro clashes, now contained in
  `src/app/webview_compat.hpp`).
- Two non-platform questions also blocked a credible v1: what the focus scores mean, and
  whether existing databases would survive schema changes.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. Windows only | Ships soonest | The author couldn't use it every day |
| B. Windows and macOS | The author uses it daily; both platforms have real capture | Adds macOS tray, overlay, packaging and notarization |
| C. All three | No caveats | Doubles the tray/overlay/packaging work for users who don't exist yet |

## Decision

**v1 supports Windows and macOS.** Linux keeps headless capture and CI coverage but isn't a
supported desktop target.

Release blockers:

1. Verify macOS capture on real hardware with Accessibility granted.
2. macOS tray and a native `NSPanel` overlay.
3. macOS `.app`/DMG packaging and notarization.
4. A CI job that launches the macOS app, not just builds it.
5. Decide what the scores mean ([ADR-0004](0004-verdict-and-opinion.md)).
6. Schema migrations, so existing `focoflow.db` files keep working.

After v1: macOS notifications (need a bundle id from packaging), the Linux tray and overlay,
and on-device retraining.

### Overlay or notification on macOS?

A notification looked cheaper, on the assumption that notifications already worked. They
didn't: on macOS the tray stub returned `false` without calling the OS, and
`UNUserNotificationCenter` refuses to post without a bundle id, which only comes with
packaging and an Apple Developer account. A native `NSPanel` overlay has no external
dependency, works the same as on Windows, and is the app's namesake feature. So macOS v1 ships
the overlay, and notifications follow once the app is bundled. The notification text builders
in `src/app/notification.hpp` are already cross-platform.

## Why

Dogfooding. A focus tracker its author can't use every day won't get the feedback that makes
it good. Windows stays because the work there is already done and verified.

## Consequences

- An item is v1 work only if it's on the list above.
- The tray and overlay stubs are now Linux-only; they guard on `!_WIN32 && !__APPLE__`.
- Linux desktop bugs aren't release blockers, but the Linux build stays in CI because it's
  cheap and has caught real bugs.

## Status

Items 1, 2, 4, 5 and 6 are done. Packaging and notarization remain.

## Revisit if

The author's main machine changes, or real users show up on a platform this deferred.
