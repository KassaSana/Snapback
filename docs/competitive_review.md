# Competitive review 2026-09-22 — what the neighbours do that Snapback does not

A point-in-time record, like [`audit-2026-08-19.md`](audit-2026-08-19.md). It compares
Snapback with the products and repositories nearest to it, on two axes: the engineering shape
(native local-first capture → SQLite → webview UI) and the product surface (focus drift,
sessions, review). Each finding names the roadmap item it feeds or the decision it raises;
open work lives in [`ROADMAP.md`](ROADMAP.md), not here.

The frame everything is measured against is Snapback's own set of decisions, not a generic
time tracker: sessions are **declared and attended**
([ADR-0005](adr/0005-a-session-is-declared-and-attended.md)), the state is a **policy
verdict** and the scores are the **model's opinion**
([ADR-0004](adr/0004-verdict-and-opinion.md)), the dashboard has **three surfaces**
([ADR-0003](adr/0003-three-surface-dashboard.md)), release builds are **network-silent and
local-only** ([ADR-0009](adr/0009-local-first-threat-model.md), Roadmap 8.10), and v1 is
**Windows and macOS** ([ADR-0002](adr/0002-v1-supports-windows-and-macos.md)). A comparator
feature that contradicts one of those is a *decision* to record, not a gap to close.

## Comparators

| Product / repository | What it is | Where it is closest to Snapback |
| --- | --- | --- |
| **ActivityWatch** (open source; Python watchers, Python or Rust server) | Automatic local tracker. Small *watchers* post events to a local server; events live in per-watcher *buckets*; consecutive identical events are merged by a *heartbeat* rule; a local REST + query API feeds a timeline/category web UI. | Engineering: local-only, data ownership, extensible capture. |
| **screenpipe** (source-available; Rust core, Tauri desktop, YC S26) | 24/7 local screen and audio memory. Event-driven capture (app switch, click, typing pause) rather than fixed-rate; pairs each capture with the OS accessibility tree and falls back to OCR; SQLite with FTS5; localhost REST and an MCP server; markdown-defined "pipes" as scheduled agents. Publishes CPU and disk budgets up front. | Engineering: event-driven native capture, local SQLite, native shell around a web UI. |
| **Rize** (closed; macOS and Windows) | AI focus coach rather than timesheet. Focus-quality score from many attributes, auto-detected focus sessions, break and overwork nudges, distraction blocking, a day timeline, daily/weekly reports, focus music. | Product: the same "quality of focus, not hours" framing. |
| **Timing** (closed; macOS) | Document-level tracking (file, URL, mail subject), a rules engine, AI summaries, meeting detection, menu-bar-first UI with the full window as the exception. | Product: title/document parsing, rules, glanceable chrome. |
| **ManicTime** (closed; Windows, local database) | Parallel day timelines — applications, documents, tags — with drag-to-select ranges and hover detail; idle detection; explicit time-zone policy. | Product: a Windows-native local timeline. |
| **Dayflow** (open source; macOS) | Screenshot every ten seconds, local or self-hosted LLM, output is a narrative daily timeline ("researching on YouTube") instead of app-usage charts. | Product: narrative over charts. |
| **Cold Turkey / Freedom / Opal** | Blockers. | Product: what Snapback deliberately is not. |

## Part 1 — Product and UI

Ordered by value to the "return to the work you were doing" promise times how much of the
work already exists in the tree.

### CR-01 — Every comparator leads Review with a day timeline; Snapback has cards and a list

Rize, Timing, ManicTime, ActivityWatch, screenpipe, and Dayflow all open their review view on
a horizontal strip of the day. Snapback's Review surface is a set of cards plus a per-session
*Context Timeline* rendered as a list (`frontend/src/ActivityCards.tsx`).

The data for a strip is already persisted and queryable: verdict per prediction, attended
spans, snapback episodes (`storage.hpp:list_snapback_episodes`), and Pomodoro phases. A strip
coloured by verdict with episode markers, and attended spans as a second lane in ManicTime's
parallel-timeline style, is composition over existing reads, not new capture.

**Feeds:** Phase 2's weekly digest (FWD-02) — the timeline is the digest's picture. Sequence it
with AUD-20's Review reshaping so the surface is split once. **Roadmap:** 10.15.

### CR-02 — The narrative recap is the headline feature, and it does not need a model

Dayflow, Timing, and Rize sell a written "what you did" paragraph. Snapback is network-silent
and has no language model, but the sentence those products sell is a template over queries
that already exist: longest focused run (`storage.hpp:longest_focus_secs`), most expensive
distraction (episodes grouped by app and summed), attended versus declared time, and the
number of recoveries. The roadmap already notes that episodes are "surfaced almost nowhere".

**Feeds:** FWD-02 as written. The finding is about weight, not scope: comparators confirm the
recap is what Review is *for*, not one card among nine.

### CR-03 — Search over context history is on-message and cheap

screenpipe's whole loop is "what was I doing at three on Tuesday". Snapback stores window
titles and parsed context per prediction and ships the SQLite amalgamation, which includes
FTS5. A "find the thing I was working on" box on Review is the most literal expression of
the product's promise. The privacy posture is unchanged — same file, same threat model as
ADR-0009 — and a new command walks the four-file IPC contract listed in
[`AGENTS.md`](../AGENTS.md). **Roadmap:** 10.16.

### CR-04 — Rize and Timing are glanceable; Snapback's only Now view is the full window

Both live in the menu bar with a small popover and treat the full window as the exception.
Snapback has a tray and the snapback overlay, but a running session is watched from the full
webview. ADR-0003's own description of Now — the goal, a state word, elapsed time, the primary
action — fits a small always-on-top pill. Overlay geometry code already exists for both
desktop platforms. **Roadmap:** 10.17.

### CR-05 — Rules keyed on title and URL, not only the app

Timing's rules engine and ActivityWatch's categories match on document and URL. Snapback's
rules and exclusions are app-only, which the roadmap already records as a privacy gap: one
banking tab hides the whole browser (8.11). 8.11's design — ordered rules scoped to an app
plus a bounded title matcher — is the right seam; this review adds that the same rule table
should drive goal fit and `frontend/src/WorkAppTeachCard.tsx`, not only redaction, and that
the domain field it waits on (7.27) has a capture-side answer in CR-09. **Feeds:** 8.11.

### CR-06 — Decisions the comparators raise (do not code these)

- **Auto-detected sessions versus declared sessions.** Rize finds focus sessions by watching;
  ADR-0005 says a session is declared. The `untracked_work` alert already detects the gap.
  The open question is narrower than "switch models": may the app *propose* a session
  retroactively from an untracked stretch, and if so does accepting one create a span with
  attendance credit? **Roadmap:** 7.30 (`decision`).
- **Blocking.** Cold Turkey, Freedom, Opal, and Rize block. Snapback's Block rule forces a
  verdict; it does not block. Staying a mirror rather than a wall is defensible — a blocker
  competes on enforcement, Snapback competes on recovery — but it should be written down so
  the next "add blocking" thought is a doc read. **Roadmap:** 7.31 (`decision`).
- **A local API or MCP server.** ActivityWatch and screenpipe expose localhost APIs and
  screenpipe ships an MCP server. Snapback's 8.10 promise is *network-silent*; a listening
  socket, even loopback-only, is a new ADR. The honest cheap alternative is a documented
  schema plus the existing exports (9.14, 9.16). **Roadmap:** 7.32 (`decision`).

## Part 2 — Engineering

### CR-07 — Generate the IPC contract instead of hand-syncing four files

[`AGENTS.md`](../AGENTS.md) lists four places a native command must be added —
`src/app/command_handlers.cpp`, `fixtures/ipc_commands.json` and its count in
`tests/test_ipc_contract.cpp`, `frontend/src/api.ts`, `frontend/demo/backend.ts` — and relies
on the contract tests to catch the one that was forgotten. Tauri-shaped repositories
(screenpipe among them) generate the TypeScript client from the command definitions. Promote
the fixture to the source of truth, with argument and result shapes, and generate the
TypeScript command types and a C++ name table from it; the count assertion becomes "the
generated files are fresh". This removes, for commands, the "shipped one half" failure the
roadmap's reconciliation notes describe. **Roadmap:** 14.9.

### CR-08 — Consecutive identical predictions could be stored as runs

ActivityWatch's heartbeat merges consecutive events with identical data inside a `pulsetime`
window; screenpipe captures only on change. Snapback persists a prediction per input event
(`storage.hpp:insert_prediction`; rows "arrive on input rather than on a clock"), so a fast
typist writes rows proportional to typing cadence, and the roadmap already worries about the
weekly `sample_count` and about Review reads contending with the persist phase on the shared
storage mutex ([`ARCHITECTURE.md`](ARCHITECTURE.md)). Measure rows per hour first with the
benchmark harness; if row volume dominates, keep per-event rows only for training export and
add a run table (verdict, start, end, count) that Review reads. ADR-0004's `focus_momentum`
feedback loop constrains what may be dropped. **Roadmap:** 14.10 (measure), then a decision.

### CR-09 — Windows URL and document context via UI Automation, not a browser extension

The roadmap defers browser extensions (FWD-10). screenpipe and Timing get URL and document
context from the accessibility tree instead; on Windows the address bar of Chromium-based
browsers and Firefox is readable through `IUIAutomation` without an extension. macOS already
enriches browser tabs through Accessibility. This is the capture-side half of 7.27's domain
field and what 8.11 waits on. **Roadmap:** 2.20.

### CR-10 — Evaluate Velopack before hand-writing the update check

FWD-06 plans a native-side update fetch. Velopack packages a compiler's output into an
installer, delta updates, and a self-updating portable build for Windows, macOS, and Linux,
with C++ bindings. It would absorb parts of 0.4b and 3.3 as well. The constraint is 8.10:
the check must be opt-in and its network behaviour written into the ADR that FWD-06 needs
anyway. **Roadmap:** 4.14.

### CR-11 — Publish resource budgets and assert them

screenpipe states "5–10% CPU, N GB per month" on its front page. Snapback has a benchmark
harness and a scheduled benchmark job but no user-facing number for idle CPU or database
growth per hour of work. Add both to [`benchmarking.md`](benchmarking.md) and assert a
ceiling in the benchmark workflow. This is also the measurement CR-08 needs. **Roadmap:** 4.15.

### CR-12 — Public-repository hygiene

ActivityWatch and screenpipe carry issue and pull-request templates and a security policy.
Snapback has neither an issue template nor a SECURITY.md. Small, and it matters the day the
release is public — Phase 1's "before strangers install" framing. **Roadmap:** 9.17.

### What was checked and found already covered

- **Real-UI end-to-end tests.** 10.1 has landed an in-page acceptance script on all three
  OSes and CDP-driven clicks in the Windows smoke. The comparator-standard layer exists; what
  remains is WebKitGTK and, optionally, screenshot diffs of the demo page.
- **Import path, crash capture, retention setting, global hotkeys.** 9.14 (done), 4.3, 9.10,
  and 12.6 already track them.
- **Calendar integration, gamification, browser extension, cloud sync.** Explicitly out of
  the six-month sequence (FWD-10); nothing here reopens them.

## Sources

- ActivityWatch — <https://activitywatch.net/>, <https://github.com/ActivityWatch/activitywatch>,
  buckets, events, and heartbeats: <https://docs.activitywatch.net/en/latest/buckets-and-events.html>,
  <https://activitywatch.net/blog/activitywatch-vs-rescuetime/>
- screenpipe — <https://github.com/screenpipe/screenpipe>
- Rize — <https://rize.io/features/productivity>, <https://rize.io/blog/rescuetime-alternatives>
- Timing — <https://timingapp.com/blog/rescuetime-alternatives/>
- ManicTime — <https://www.manictime.com/features/automatic-time-tracking>
- Dayflow — <https://www.dayflow.so/compare/rescuetime-alternative/>
- Velopack — <https://github.com/velopack/velopack>, <https://velopack.io/>
