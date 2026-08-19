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
| `src/app/` | App state, command registration, settings, tray, notifications, start-on-login, data export, and frontend assets |
| `src/util/` | Header-only leaf utilities with no project dependencies: the leveled rotating logger and monotonic-clock helpers |
| `frontend/src/` | React views, API mappers, and the project-owned native bridge |

`src/main.cpp` is deliberately absent from the table: it is not a module but the single
translation unit that wires them together, and it is the only place that knows about all of
them at once.

## Threading

The OS hook invokes a lightweight callback that writes to the bounded ring. The
engine thread is the only consumer and owns feature extraction/classification.
App state protects storage and mutable configuration with mutexes. Native events
are copied and dispatched to the UI thread before JavaScript is evaluated.

The engine computes mutable live state under its state mutex, then publishes one immutable
snapshot. Hot UI reads (`health`, current session/prediction/snapback, idle and classifier
status) consume that snapshot instead of joining the compute critical section. Publication
is dirty-driven, so an empty 100 ms tick performs no allocation. Storage-backed reads keep
the separate storage seam below.

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

**There are two JSON boundaries, and they do not share a convention.** Knowing which one you
are looking at is the difference between a correct mapper and a silently empty field.

| Boundary | Casing | Where | Why |
| --- | --- | --- | --- |
| IPC — everything the dashboard calls | **camelCase** keys, **snake_case** command *names* | `src/app/commands.hpp` ↔ `frontend/src/apiMappers.ts` | The keys are consumed by TypeScript, so they follow TypeScript's convention; the command names are native identifiers and follow C++'s. `get_session_recap({ sessionId })` is both conventions in one call, on purpose |
| Training / fixture data — `CaptureEvent` | **snake_case** keys | `src/types.cpp`, the training export, `fixtures/` | These rows are consumed by the Python-side training tooling and pinned by fixtures, not by the dashboard. Renaming them to camelCase would invalidate exported corpora for no reader's benefit |

`CaptureEvent` is the only type on the second boundary. If you are adding a field to anything
the UI reads, camelCase is the answer.

### The five things called a "summary"

Five distinct types overlap in name and partly in content. They are not interchangeable, and
picking the wrong one is the most common way to fetch the right numbers for the wrong window.

| Type | Command | Scope | Answers |
| --- | --- | --- | --- |
| `SessionRecap` | `get_session_recap` | **One session** | How did *that* session go — duration, attended `active_secs`, avg focus, distraction spikes, deep-focus % |
| `SessionSummary` | `get_session_history` | **One session, plus its record** | `SessionRecord` + `SessionRecap` — the shape one row of the history list needs |
| `FocusSummary` | `get_focus_summary` | **A batch of predictions** | Pure aggregation over prediction rows: avg, peak, distracted fraction, longest unbroken focused stretch. No storage, no clock — `src/engine/focus_summary.hpp` is unit-testable on a vector |
| `AnalyticsSummary` | `get_analytics` | **A time window, by shape** | The chart data — hourly buckets and top apps, plus a session streak |
| `SummaryReport` | `get_summary_report` | **A time window, by total** | The Review headline — session counts, focus seconds, distracted fraction, attended vs planned |

The last two both take a window and both start with `prediction_stats`; they differ in what
they return, not in what they cover. `SessionRecap` and `SessionSummary` are per-session and
never take a window at all.

**One duplication to know about.** `Storage::recap` computes a single session's aggregate, and
`Storage::recent_session_summaries` computes the same thing for many sessions in one query,
with the aggregate expressions copied verbatim between them. They are held in agreement by a
field-by-field parity test rather than by sharing code — so a change to one is a change to
both, and the parity test is what tells you if you forgot. Roadmap 14.3 is where that seam
gets a single owner.

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

**Start-on-login and revealing the data folder are split differently**, and deliberately so.
`src/app/autostart.cpp` chooses a backend per OS, but the backends themselves —
`autostart_launchd.cpp` (macOS plist) and `autostart_systemd.cpp` (Linux user unit) — compile
on **every** platform and take their target directory as an argument. So does
`src/app/data_export.cpp`. The rule these follow: *the platform decides where, portable code
decides what*. It buys two things a `#if` around the whole module does not — the plist and unit
text are covered by all three CI jobs, and the tests can drive a temp directory instead of the
developer's real login items (Roadmap 11.7). `src/app/reveal_path.cpp` follows the same shape
with one exception: its macOS backend needs AppKit, so it lives in `reveal_path_macos.mm` and
is the only per-OS file linked into `snapback_app` rather than the app target.

The remaining stubs live in `src/snapback/overlay_stub.cpp` and `src/app/tray_stub.cpp`.
They now cover Linux only — both guard on `!_WIN32 && !__APPLE__` — and exist so the
desktop app links there at all. Read their header comments before replacing them: they
record which behavior is load-bearing and which is merely absent. The load-bearing one is
dismissal, because `ContextTracker`'s `Recovering` state has exactly one exit
(`dismiss_recovery`); on Linux the web UI's Dismiss button is still the only thing that
reaches it.

The headless core remains buildable without the webview or ONNX runtime.
