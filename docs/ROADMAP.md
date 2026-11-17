# Roadmap

Roughly in priority order.

## Shipping

- **Code-sign the Windows build.** The installer and ZIP work, but SmartScreen warns until
  they're signed. Needs a certificate.
- **macOS packaging.** `.app` bundle, notarization and a DMG. This is the main thing between
  the current state and a proper v1 on both platforms. It also unblocks native notifications
  on macOS, which need a bundle id.
- **Upgrade and uninstall.** Walk an install-over-install upgrade end to end, and make
  uninstall respect the data-purge setting.
- **Linux.** Tray and overlay are stubbed; capture already works. Packaging (AppImage or
  Flatpak) after that.

## Product

- **Session explorer.** Finish editing past sessions and the per-session timeline.
- **Editable labels.** One authoritative, editable history of the corrections a user makes,
  so retraining and Review agree on what a moment was.
- **Scoped rules.** Let an app rule apply to one goal category rather than globally, and add
  capture rules that keep sensitive window titles out of storage entirely.
- **Lock, sleep and wake** as real lifecycle events instead of looking like long idle gaps.
- **Global hotkeys** for labelling the current moment.
- **Review windows.** Decide between rolling and calendar ranges, and show when a range is
  incomplete.

## Engineering

- Split `AppState` along its lock boundaries.
- Make engine wakeups deadline-driven, and move retention cleanup off the startup path.
- Fuzz the untrusted inputs: data import, IPC arguments, model files.
- C++ coverage reporting, and finish the clang-tidy rollout.
- Broader real-webview UI tests and an accessibility pass.
- Optional encryption at rest.

## Open questions

- Is there enough labelled data to justify training a model over the heuristic? And when the
  two disagree, which one wins?
- The window-title parser guesses filenames, and sometimes guesses wrong. Keep it, narrow
  it, or drop it?

## Not planned

- **Blocking apps or sites.** Snapback shows you where your attention went; it doesn't
  enforce anything.
- **A local HTTP API or MCP server.** The app opens no sockets, and that's a deliberate
  privacy guarantee ([ADR-0009](adr/0009-local-first-threat-model.md)). Use the exports.
- **Auto-detected sessions.** A session is something you start
  ([ADR-0005](adr/0005-a-session-is-declared-and-attended.md)).
- **Auto-update.** It would need a network call; not without revisiting the threat model.
