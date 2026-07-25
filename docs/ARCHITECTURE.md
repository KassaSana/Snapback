# Snapback architecture

This document describes the current native application. Historical migration
notes live in [PORT_HISTORY.md](PORT_HISTORY.md).

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
| `src/storage/` | SQLite schema, transactions, sessions, predictions, exports, and retention |
| `src/snapback/` | Context tracker, title parsing, and platform overlay |
| `src/app/` | App state, command registration, settings, tray, notifications, and frontend assets |
| `frontend/src/` | React views, API mappers, and the project-owned native bridge |

## Threading

The OS hook invokes a lightweight callback that writes to the bounded ring. The
engine thread is the only consumer and owns feature extraction/classification.
App state protects storage and mutable configuration with mutexes. Native events
are copied and dispatched to the UI thread before JavaScript is evaluated.

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
- macOS event tap, accessibility permissions, and active-window lookup.
- Linux input capture and desktop stubs where native UI support is pending.

The headless core remains buildable without the webview or ONNX runtime.
