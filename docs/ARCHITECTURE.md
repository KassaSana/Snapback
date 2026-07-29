# Snapback architecture

This document describes the current native application.

## Runtime shape

```
capture/ ──▶ ring buffer ──▶ engine/ ──▶ storage/
   │                         │             │
   └── permissions           └── snapback/ └── app/
                                                │
                                      window.__snapback ↔ React
```

`main.cpp` owns startup and shutdown. It resolves the data directory, opens
SQLite, creates `AppState`, starts capture and the engine tick, then creates the
webview when the desktop target is enabled.

## Modules

| Module | Responsibility |
| --- | --- |
| `src/types.*` | Shared records, enums, and JSON wire mappings |
| `src/capture/` | Platform input hooks, active-window lookup, permissions, and SPSC buffering |
| `src/engine/` | Feature extraction, heuristic/ONNX classification, focus modes, and summaries |
| `src/storage/` | SQLite schema, versioned migrations, transactions, sessions, predictions, exports, and retention |
| `src/snapback/` | Context tracker, title parsing, and platform overlay |
| `src/app/` | App state, command registration, settings, tray, notifications, and frontend assets |
| `frontend/src/` | React views, API mappers, and the project-owned native bridge |

## Threading

The OS hook invokes a lightweight callback that writes to the bounded ring. The
engine thread is the only consumer and owns feature extraction/classification.
App state protects storage and mutable configuration with mutexes. Native events
are copied and dispatched to the UI thread before JavaScript is evaluated.

The UI thread and the engine thread take the *same* storage mutex, so a slow read
answering the UI blocks the tick's persist phase — and a bounded ring turns a long
enough block into dropped events. That is why the history and analytics read paths
aggregate in SQL rather than looping over sessions (Roadmap 7.12): the cost of a
query here is paid in capture fidelity, not just latency.

## Schema versioning

`focoflow.db` carries its schema version in `PRAGMA user_version`, and `Storage::migrate()`
applies an ordered, append-only migration list inside one transaction. Two rules make it
work and neither can be enforced by the compiler, so they are stated on `kSchemaVersion`:
**every migration must be idempotent**, because version 0 means both "new file" and
"install from before versioning existed" and cannot be told apart; and **a released
migration is never edited**, only appended to.

A database stamped *newer* than the running build is refused rather than opened, so a
downgrade cannot write rows a later build considers malformed.

## IPC

`src/app/commands.hpp` is the single command registry. `webview.bind()` exposes
each command as a browser function. `src/app/ipc_shim.hpp` injects
`window.__snapback` before page scripts run:

- `invoke(command, args)` forwards to the matching native binding.
- `listen(event, handler)` registers a frontend callback.
- `emit(event, payload)` delivers a native event to registered callbacks.

Command failures cross the synchronous binding boundary as a JSON error
envelope; the bridge turns that envelope into a rejected promise.

## Data and model contracts

Frontend DTOs use camelCase. Internal records use snake_case. The feature vector
has 31 named values and a fixed training-column order. The scenarios in
`fixtures/feature_parity/scenarios.json` exercise representative behavior, and
`fixtures/feature_parity/golden.json` records exact expected vectors.

## Platform boundaries

Platform-specific code is isolated behind small interfaces:

- Windows low-level hooks, active-window lookup, overlay, tray, and autostart.
- macOS `CGEventTap`, accessibility permissions, active-window lookup including browser
  tab titles, an `NSStatusItem` tray (`src/app/tray_macos.mm`), and a native `NSPanel`
  overlay (`src/snapback/overlay_macos.mm`) matching the Windows card's geometry and
  dismiss behavior. `mac_ui.mm` holds the AppKit shims `main.cpp` needs so that translation
  unit stays plain C++. Native **notifications** remain absent —
  `Tray::show_notification()` still returns `false` without calling the OS, because
  `UNUserNotificationCenter` needs a bundle identifier that arrives with packaging
  (Roadmap 3.3).
- Linux input capture and desktop stubs where native UI support is pending.

The remaining stubs live in `src/snapback/overlay_stub.cpp` and `src/app/tray_stub.cpp`.
They now cover Linux only — both guard on `!_WIN32 && !__APPLE__` — and exist so the
desktop app links there at all. Read their header comments before replacing them: they
record which behavior is load-bearing and which is merely absent. The load-bearing one is
dismissal, because `ContextTracker`'s `Recovering` state has exactly one exit
(`dismiss_recovery`); on Linux the web UI's Dismiss button is still the only thing that
reaches it.

The headless core remains buildable without the webview or ONNX runtime.
