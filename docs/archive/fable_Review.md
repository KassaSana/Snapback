# Snapback review — independent findings and verdicts on the prior competitive review

> Historical source review. Findings and proposals below describe the inspected
> checkout, not the current implementation or an accepted work queue. Use the
> [live roadmap](../ROADMAP.md) for remaining work and accepted priorities.
> Recheck source evidence before acting; this archival move does not reproduce findings.
> The draft supplies no authoritative review date or commit; neither is inferred here.

Date: 2026-09-22
Scope: the working tree at HEAD. docs/competitive_review.md (historical/proposed path, not a current repository reference) and the roadmap items it
spawned (10.15, 10.16, 10.17, 14.9, 14.10, 2.20, 4.14, 4.15, 9.17, 7.30–7.32, the FWD-02 recap
changes, the new Phase 2 row) were treated as unverified proposals, not as evidence that
anything "already exists". Every CR-XX finding was treated as a hypothesis and checked
against code.

Nothing in the repository was modified for this review other than adding this file.

---

## Contents

1. [How the review was done](#1-how-the-review-was-done)
2. [Independent findings](#2-independent-findings)
   - 2.1 Engineering
   - 2.2 Privacy
   - 2.3 State that exists in more than one place
   - 2.4 Testing
   - 2.5 Product and UI
   - 2.6 Simplify or remove
3. [Verdicts on CR-01 to CR-12](#3-verdicts-on-cr-01-to-cr-12)
4. [Prediction persistence cadence, resolved](#4-prediction-persistence-cadence-resolved)
5. [What to measure before building](#5-what-to-measure-before-building)
6. [Comparable products and repositories](#6-comparable-products-and-repositories)
7. [Things Snapback should not build](#7-things-snapback-should-not-build)
8. [Candidate improvements](#8-candidate-improvements)

---

## 1. How the review was done

Read in full or in the relevant regions: `docs/ARCHITECTURE.md`, `docs/adr/0005-*.md`,
`docs/adr/0009-*.md`, `src/main.cpp`, `src/app/state.hpp`, `src/app/state.cpp`,
`src/app/settings.hpp`, `src/app/command_handlers.cpp`, `src/app/webview_origin.cpp`,
`src/storage/storage.hpp`, `src/storage/storage.cpp`, `src/capture/capture_thread.cpp`,
`src/capture/ring_buffer.hpp`, `src/capture/input_hook_windows.cpp`,
`src/capture/active_window.cpp`, `src/snapback/tracker.hpp`, `src/snapback/tracker.cpp`,
`src/engine/focus_summary.hpp`, `CMakeLists.txt`, `benchmarks/bench_snapback.cpp`,
`.github/workflows/benchmarks.yml`, `frontend/src/api.ts`, `frontend/src/useLiveData.ts`,
`frontend/src/useAppEffects.ts`, `frontend/src/sessionCockpit.ts`, and the roadmap items
named below. Claims about "nothing emits X" are grep results over `src/`.

Where the previous review or the roadmap made a factual claim about the code (persistence
cadence, FTS5 availability, where titles are stored), the claim was checked against the code
and the result is stated, whichever way it went.

---

## 2. Independent findings

### 2.1 Engineering

#### F-1. Persistence failures drop data silently and never reach the UI

`AppState::engine_tick` (`src/app/state.cpp`) runs the persistence phase — the
`Storage::Transaction` around `AppState::persist` — inside a try/catch. On an exception it
logs and moves on. The `PersistJob` that was about to be written (prediction row, feature
snapshot row, context snapshot, snapback episode) is discarded. There is:

- no retry of the job,
- no backoff, so a persistent fault (disk full, database locked by a second instance,
  read-only volume) is re-attempted on every subsequent tick that produces a job,
- no durable state in `HealthStatus`, so `health` polling reports nothing,
- no emitted event.

The frontend, however, already has the receiving end:

```ts
// frontend/src/api.ts, lines 910–917
onPersistenceFailed: (handler: (payload: PersistenceFailurePayload) => void) =>
  listen<Record<string, unknown>>("persistence-failed", (event) => { ... }),
```

A grep of `src/` for `persistence-failed` finds only the payload struct in
`src/types.hpp` (`PersistenceFailurePayload`, line ~715). No native code emits it.

Roadmap 9.6 records this gap accurately ("exceptions escaping the engine persistence phase
are logged, but no durable failure state reaches HealthStatus and no retry policy stops the
engine from repeating the same failed write"). The 2026-09-20 attendance-recovery work fixed
it for `session_spans` transitions only: those are kept pending until their transaction
commits. Predictions, feature snapshots, context snapshots, and episodes are still lost on
the tick that fails.

Why this matters more than anything else in this document: the failure mode is a dashboard
that simply stops updating, which the roadmap itself describes as "indistinguishable from
'you're doing great'". A product whose promise is honest feedback cannot have its most likely
fault look like success.

**Status: real, open, acknowledged.**

#### F-2. Four frontend event listeners have no native emitter

In `frontend/src/api.ts`:

| Event name            | Line | Emitted anywhere in `src/`? |
|-----------------------|------|-----------------------------|
| `capture-failed`      | 894  | No                          |
| `overlay-failed`      | 903  | No                          |
| `persistence-failed`  | 911  | No                          |
| `label-hotkey`        | 982  | No (roadmap 12.6 knows)     |

Capture failure *does* reach the UI, but through `health` polling: `AppState::health`
sets `h.status = "capture_failed"` and `h.capture_failed` (`state.cpp` ~998–1010), and
`frontend/src/useHealth.ts` consumes it. The event listener is dead code beside a working
polling path. Overlay failure and persistence failure have no path at all.

`fixtures/ipc_commands.json` and `tests/test_ipc_contract.cpp` cover *commands*. Events
are not in the contract, so drift between "what the frontend listens for" and "what the
native side emits" is invisible to every test. That is how four listeners accumulated.

**Status: real.** Wire them or delete them. Keeping dead scaffolding is worse than either.

#### F-3. One application-level mutex serialises UI reads against engine writes

`storage_mutex_` is taken by:

- the engine thread's persistence phase in `engine_tick`,
- every Review/analytics read on the UI (webview binding) thread:
  `AppState::analytics` (`state.cpp` 1677–1699), `AppState::focus_summary_for_window`
  (1156–1166), `AppState::session_history_for_window` (1168–1173),
  `AppState::session_history` (1175–1180),
- `AppState::delete_all_activity_data` (1182–), which also takes `mutex_` and
  `activity_boundary_mutex_`.

SQLite in WAL mode (which `Storage::open` configures) already provides one writer plus
any number of readers with snapshot isolation. A single connection guarded by one mutex
forfeits that and makes every Review read a potential stall for capture persistence, and
vice versa. `kSqliteBusyTimeoutMs = 500` (storage.hpp (historical line 30)) bounds the SQLite-level wait but
not the mutex wait.

The mitigations already landed are the right ones *for this design*: aggregation was pushed
into SQL (`Storage::prediction_stats`, `Storage::hourly_focus_buckets`, `context_app_counts`,
`recent_session_summaries` replacing a 1+5N pattern), and the one C++-side materialisation
is capped. The design itself is the ceiling. Whether the ceiling matters is unmeasured (see §5).

**Status: real as a design limit; impact unmeasured.**

#### F-4. `kFocusSummaryMaxSamples` truncation produces a wrong answer, not a slow one

```cpp
// src/app/state.cpp, 1156–1166
FocusSummary AppState::focus_summary_for_window(const std::string& window,
                                                const std::optional<std::string>& since) {
    const auto cutoff = review_window_cutoff(window, since, cutoff_unix_ms);
    std::lock_guard lock(storage_mutex_);
    // Newest-first from SQL, then reverse: summarize_predictions measures streak gaps
    // between chronological neighbours. Cap the materialisation so a 90-day window cannot
    // hold storage_mutex_ across an unbounded scan on the UI command path.
    auto rows = storage_.predictions_since(cutoff, kFocusSummaryMaxSamples);
    std::reverse(rows.begin(), rows.end());
    return summarize_predictions(rows);
}
```

`kFocusSummaryMaxSamples = 50'000` (state.hpp (historical line 75)). At the real persistence cadence
(§4: at most one prediction row per second, only while attended and receiving input),
50,000 rows is roughly 14 hours of continuous input. For a 7-day or 90-day Review window
the query returns the newest 50,000, so the oldest rows in the window are silently dropped
and the "longest focused run" is computed over roughly the last 14 attended hours rather
than the window the user asked for. Nothing in the result indicates truncation.

Meanwhile `Storage::prediction_stats` (`storage.cpp`) computes `longest_focus_secs` in SQL
with window functions over the full range, and `src/engine/focus_summary.hpp` computes the
same concept in C++ with different boundary rules (session boundaries, gap thresholds).
Two implementations of the headline number, one of which is wrong for long windows, and
which one the UI shows depends on which command it called.

**Status: real; correctness bug.** Pick the SQL implementation, delete the C++ one, remove
the cap (it is no longer needed once nothing is materialised).

#### F-5. Startup does migration, prune, and `VACUUM` synchronously on the main thread

`Storage::open` (`src/storage/storage.cpp`) runs schema migration, retention prune, and —
when `kVacuumMinDeletedRows = 500` (storage.hpp (historical line 24)) is exceeded — `VACUUM`, before
`AppState` construction returns and before the webview is created. A `VACUUM` of a
several-hundred-MB database is seconds of a blank window. A maintenance thread already
exists and is the natural owner.

Roadmap 14.7 covers this. **Status: real, known.**

#### F-6. The classifier runs when there is no session

`AppState::compute_event` (`state.cpp` 2474–2541) ingests every event, and for every
un-throttled event runs `features_.extract`, `classifier_.predict`, and updates
`latest_prediction_`, regardless of `have_session`. Only persistence is gated:

```cpp
// state.cpp 2536–2539
if (have_session) {
    job.prediction = std::move(record);
    job.features = features;
}
```

Combined with the fixed 10 Hz tick (`kEngineTickIntervalMs = 100`, state.hpp (historical line 65)) that
runs whether or not anything is queued, the process does real work while nothing is being
recorded. The per-tick drain is bounded (`kEngineDrainBudget`, `kEngineDrainBudgetMs`), so
this is idle CPU and wakeups, not latency. Roadmap 14.5 asks for deadline-aware wake and
measurement; neither has happened.

**Status: real; magnitude unmeasured.**

#### F-7. Lifecycle and shutdown are sound

Checked and found no issue:

- `src/main.cpp` declares `webview::webview w` before `EngineLifetime` before `AppState`,
  so destructors run webview → engine stop → state, which is the order a controlled
  shutdown needs.
- `EngineLifetime::~EngineLifetime` calls `AppState::stop_engine`, which joins the engine
  and maintenance threads.
- The Windows hook thread exits on `WM_QUIT` (`input_hook_windows.cpp`).
- As of 2026-09-21 the capture producer is joined before an empty queue is allowed to end
  the engine loop, so a final callback cannot land after the consumer has exited.
- The ring buffer (`ring_buffer.hpp`) is a correct SPSC design: power-of-two capacity,
  `alignas(64)` head/tail, drops on full with a counter rather than blocking the hook.

#### F-8. The benchmark measures the wrong thing

`benchmarks/bench_snapback.cpp` uses an in-memory SQLite database, a fixed timestamp for
every row, and inserts a prediction on every other event. It therefore cannot measure:

- rows per hour under real throttling (it bypasses the 1 s throttle),
- on-disk write cost under WAL + `synchronous=NORMAL`,
- read/write contention on `storage_mutex_`,
- index behaviour on timestamps that actually increase.

Any scaling decision made from `benchmarks.yml` output is made from data that does not
describe the running product. **Status: real.**

### 2.2 Privacy

#### F-9. Window titles are stored verbatim with app-only exclusion

`context_snapshots.window_title` is written as captured (`storage.cpp` 584–594).
ADR-0009 names window titles as the most sensitive field Snapback holds. Exclusion
(`excluded_apps`, `AppState::is_private_event_unlocked`) matches on app name by substring;
there is no title-level rule. A user who excludes nothing but has a browser tab titled with
a medical portal or a bank name has that string on disk, unencrypted (which ADR-0009
accepts for v1), for `kDefaultRetentionDays = 90`.

**Status: real gap; already roadmap 8.11.**

#### F-10. "Delete all activity" leaves user-typed goal text in `localStorage`

`AppState::delete_all_activity_data` (`state.cpp` 1182–) does the right thing on the native
side: source first, then every replica, attempted regardless of prior failures, plus
`capture_.discard_pending_events()` so in-flight titles are not filed after the wipe. The
frontend's recent-goal presets in `frontend/src/sessionCockpit.ts` are stored in
`localStorage`, described there as "local convenience, not user data", and are not
cleared. Goal strings are typed by the user and often name the work ("apply to X",
"finish Y for Z"). That classification is worth revisiting.

**Status: real, small.**

#### F-11. Threat-model document names the wrong file

ADR-0009's body refers to `snapback.db`; `storage.hpp` opens `focoflow.db`. Minor, but it
is the document a user would read to learn what is on their disk. **Status: real, trivial.**

### 2.3 State that exists in more than one place

| State                          | Places                                                                                   | Consequence |
|--------------------------------|------------------------------------------------------------------------------------------|-------------|
| Focus mode                     | `AppState::focus_mode_`, `settings_`, `sessions` row                                     | Three writers; reconciliation on session start/stop |
| Pomodoro runtime state         | `AppSettings::pomodoro_state` → `settings.json`                                          | Every phase tick is a settings-file write; config and a running timer share one file and one lock path |
| Longest-focus streak           | SQL in `Storage::prediction_stats`; C++ in `focus_summary.hpp` via `summarize_predictions` | Different semantics; one truncated (F-4) |
| Latest prediction              | `latest_prediction_` hydrated from `storage_.latest_prediction()` at construction (state.cpp (historical line 161)) | Stale hero on relaunch; frontend filters by `sessionId !== activeSessionId` in `useLiveData.handlePrediction`, so mostly mitigated |
| Frontend preferences           | `localStorage` (presets, onboarding, appearance, review range) vs `settings.json` vs DB   | Three lifetimes; two covered by delete-all |

### 2.4 Testing

- `tests/test_ipc_contract.cpp` asserts a hardcoded command count. Set-equality between the
  fixture's names and the `CommandRegistry`'s registered names would catch the same drift
  without the "bump the number" ritual, and would be a strictly stronger check.
- Events are not part of any contract or parity test (see F-2).
- No test exercises a realistically sized on-disk database. The persistence-failure tests
  that landed on 2026-09-20 (injected begin/write/commit failures, real `BEGIN` contention)
  are good and should be the model.

### 2.5 Product and UI

- The failure UX (roadmap 9.6) is the product finding: every backend fault presents as a
  dashboard that stops updating. Until F-1 lands, no Review feature is trustworthy in the
  face of a full disk.
- Snapback episodes are, per the roadmap's own note, surfaced almost nowhere in the UI
  despite `Storage::list_snapback_episodes` existing. This review did not trace every
  surface; the claim is taken from the roadmap and is plausible.
- The three-surface model (ADR-0003) plus overlay plus tray is a reasonable surface count.
  The product's exposure to complexity is in settings breadth (Pomodoro, alert routes, quiet
  hours, goal categories, app rules, excluded apps, autostart, appearance), not in surfaces.

### 2.6 Simplify or remove

1. The four dead event listeners (F-2).
2. One of the two longest-focus implementations (F-4).
3. `docs/ROADMAP.md` at roughly 5,000 lines is simultaneously the plan, the changelog, and
   the postmortem archive. That is why commit 907517d could land eleven speculative items
   and have them read like decided plans: there is no structural difference between an
   open proposal and an accepted item in that file. Move the done archive and postmortems
   out; keep the roadmap to open items with a status field.
4. The benchmark as a decision input (F-8): either make it measure on-disk behaviour with
   real cadence, or stop citing it.

---

## 3. Verdicts on CR-01 to CR-12

Legend — *Keep*: do as proposed. *Modify*: the problem is worth solving but the proposal is
wrong or over-scoped. *Defer*: not wrong, not now, needs evidence. *Remove*: premise is
false or the proposal contradicts a decision already made.

### CR-01 — Day-timeline lane on Review (roadmap 10.15)

**Verdict: Defer. Problem: hypothetical.**

The data is present: per-second verdicts in `predictions`, attended spans in
`session_spans`, episodes via `Storage::list_snapback_episodes`, Pomodoro phases. The only
argument offered is that every comparator has one. No user evidence is cited that Review's
current cards-plus-context-list fails anyone. Building it now means a new aggregate query
running on the UI thread under `storage_mutex_` (F-3) before the contention question is
answered, and before the headline number it would sit beside is correct (F-4).

### CR-02 — Deterministic narrative recap as Review headline (FWD-02)

**Verdict: Modify. Problem: real, small.**

The inputs exist (`prediction_stats`, `list_snapback_episodes`, `context_app_counts`) and a
template needs no model. But "longest focused run" — the example's first clause — is
precisely the number computed two ways with one wrong for long windows (F-4). Fix that first.
Then a one-line recap is `S`. Do not make it "the headline" until the numbers it quotes are
the same numbers the cards show.

### CR-03 — Search over context history using FTS5 (roadmap 10.16)

**Verdict: Remove as written. Problem: hypothetical; premise false on two counts.**

1. *"FTS5 is already in the SQLite amalgamation."* The FTS5 source is present in the
   amalgamation but compiles only with `-DSQLITE_ENABLE_FTS5`. `CMakeLists.txt` 62–73 builds
   `sqlite3` from either third_party/sqlite/sqlite3.c (historical/proposed path, not a current repository reference) or the pinned 3.45.3 amalgamation
   with no `target_compile_definitions`. `CREATE VIRTUAL TABLE ... USING fts5` would fail
   at runtime today.
2. *"Titles and parsed context are stored per prediction."* The `predictions` table
   (`storage.cpp` 534–546) has no title column. Titles live in `context_snapshots`, written
   on foreground change plus a 30 s checkpoint (`ContextTracker::snapshot_interval_secs_ =
   30.0`, tracker.hpp (historical line 82)). That is thousands of rows per week, not millions.

Given (2), a `LIKE` over `context_snapshots.window_title` needs no index and no migration
and would be fast at any plausible size. Given the product, the "return to the work you were
doing" promise is already served by the snapback restore target
(`AppState::restore_snapback_target`), which is a one-click return to the specific window
the user left, not a search box. If search is ever wanted: `LIKE`, `S`. FTS5: never.

### CR-04 — Compact always-on-top Now mode (roadmap 10.17)

**Verdict: Defer. Problem: hypothetical.**

ADR-0003 fixes three surfaces. A fourth requires per-platform window-size and always-on-top
handling; `window_lifecycle_macos.mm`, `tray_macos.mm`, and the overlay each already own a
slice of that, so it would be a fourth copy. The glanceable moment the product actually
needs — "you drifted, here is the way back" — is the overlay, which exists. Revisit with
evidence that users keep the full window open to watch the hero.

### CR-05 — Title and URL pattern rules (roadmap 8.11)

**Verdict: Modify — split in two. (a) real; (b) hypothetical.**

(a) *Title-pattern redaction before storage.* Closes F-9. Apply at `save_context_snapshot`
time (and to the in-memory `latest` context the UI reads), never after. `M`. Keep.

(b) *The same rules table drives goal fit.* This conflates two pipeline stages: redaction is
a pre-storage filter on captured strings; goal fit is a classifier input. A rule that hides a
title from disk and a rule that says "this title is on-task" have different lifetimes,
different failure modes, and different tests. Also, there is no URL on Windows to match
(CR-09), so "URL rules" are title rules in practice. Defer (b) until there is a case that
`app_rules` plus `goal_categories` cannot express.

### CR-06 — Record three decisions (roadmap 7.30–7.32)

**Verdict: Modify.**

- (a) *Auto-proposed vs declared sessions.* **Already solved.** ADR-0005 rejects auto-start
  and explains why; roadmap 2.7's untracked-work nudge (`onUntrackedWork`, "it asks; it
  never starts a session on their behalf") is the accepted compromise. Do not reopen without
  new evidence.
- (b) *Blocking vs reflective feedback.* **Real but tiny.** The code embodies "mirror, not
  wall" everywhere — nothing blocks, nothing prevents — but no document says it in one
  sentence. One paragraph in `ARCHITECTURE.md`'s principles. `S`.
- (c) *Local interface / MCP server vs network-silent rule 8.10.* **Already solved.** A local
  listener is exactly the attack surface ADR-0009 and rule 8.10 exclude. The decision exists;
  a new ADR would restate it.

### CR-07 — Generate the IPC contract from `fixtures/ipc_commands.json` (roadmap 14.9)

**Verdict: Modify — no generator. Problem: real, small, already mitigated.**

Four files must move together per `AGENTS.md`; `test_ipc_contract.cpp` and the demo-parity
tests catch the one forgotten. A generator adds a build step and a codegen dependency to save
editing two TypeScript declarations per new command, and `frontend/demo/backend.ts` still
needs a hand-written body per command regardless. The actual irritant is the hardcoded count
assertion. Replace it with fixture-vs-registry set equality, and add events to the fixture
while there (F-2). `S`.

### CR-08 — Prediction-run compaction, heartbeat pattern (roadmap 14.10)

**Verdict: Remove the compaction; keep the measurement. Premise false.**

Predictions are persisted at most once per second, not per input event (§4). Two further
reasons compaction is the wrong shape here:

- `feature_snapshots` is written 1:1 with `predictions` (`AppState::persist`, `state.cpp`
  2552–2554) and is the training export. Merging consecutive identical verdicts discards the
  feature vectors those verdicts came from.
- `focus_momentum` feeds the model from stored scores (ADR-0004). Runs have no score series.

ActivityWatch's heartbeat merging works because its rows are interchangeable duration
records. Snapback's rows are samples. Measure rows/hour and DB bytes/day as a diagnostic
(§5); do not gate a decision on it.

### CR-09 — Windows URLs via UI Automation (roadmap 2.20)

**Verdict: Defer. Problem: real gap; value unproven.**

The gap is real: macOS enriches browser *tab titles* via `osascript`
(`active_window.cpp` 96–134, Safari and Chromium), not URLs; Windows has neither. Reading the
address bar through `IUIAutomation` is per-browser and per-version brittle, adds a UIA
round-trip on every foreground change, and — the decisive point — URLs are more sensitive
than titles, so this must not land before CR-05(a). The domain field's product value is
asserted, not demonstrated.

### CR-10 — Evaluate Velopack (roadmap 4.14)

**Verdict: Defer. Problem: hypothetical.**

No update work is scheduled. An update check is a network call; under rule 8.10 that is a
threat-model change requiring its own ADR before any tool is evaluated. The Windows installer
is IExpress (`package_windows.ps1`) and macOS/Linux have none, so packaging is the prior
question. Evaluate when FWD-06 is actually scheduled, not before.

### CR-11 — Publish resource budgets and assert a ceiling in `benchmarks.yml` (roadmap 4.15)

**Verdict: Modify. Problem: real — no numbers exist.**

Publish budgets (idle CPU, DB bytes/hour, event-to-prediction p95) and measure them on a
real host with a real database. Do **not** assert ceilings in CI: GitHub-hosted runners have
high variance, and the current bench uses in-memory SQLite (F-8), so it cannot measure DB
growth at all. A flaky performance gate gets disabled within a month and then lies by
omission.

### CR-12 — Issue and PR templates, `SECURITY.md` (roadmap 9.17)

**Verdict: Modify. Problem: partly real.**

`SECURITY.md` is cheap, fits ADR-0009, and tells a finder where to report. Do it. PR
templates in a sole-author repository with a CI attribution check are ceremony. An issue
template is optional. `S`.

---

## 4. Prediction persistence cadence, resolved

The previous review said in one place that predictions persist on a 100 ms tick and in
CR-08 that they persist per input event. Neither is right.

- The 100 ms figure is `kEngineTickIntervalMs` — the cadence at which `engine_tick` drains
  the ring buffer. It is not a persistence cadence.
- A prediction is *computed* per un-throttled event, but the throttle is one second:

```cpp
// src/app/state.cpp, 2479–2485
if (last_prediction_secs_ >= 0.0 && now - last_prediction_secs_ < 1.0) {
    // Throttled: no new prediction this event. Persist the context snapshot and/or the
    // episode if either was produced; otherwise there's nothing to write.
    if (job.context_snapshot || job.snapback_episode) return job;
    return std::nullopt;
}
last_prediction_secs_ = now;
```

- It is persisted only when a session is active (`have_session`, 2536), not while idle
  (2465), and not for private events. A second with no input produces no row at all.
- Each persisted prediction also writes one `feature_snapshots` row (31 REAL columns).
- Context snapshots are independent: one per foreground change plus a 30 s checkpoint.

### Growth

| Quantity | Ceiling (continuous input) | Realistic |
|---|---|---|
| `predictions` rows / attended hour | 3,600 | well below; only seconds with input count |
| `feature_snapshots` rows / attended hour | 3,600 | same |
| Bytes / attended hour (rows + `idx_predictions_ts` + `idx_predictions_session_ts`) | ~1.5 MB | a few hundred KB |
| Per 6-hour day | ~10 MB | 2–4 MB |
| At 90-day retention | ~1 GB | tens to low hundreds of MB |

`kDefaultRetentionDays = 90` and the prune path bound the tail. This is not a storage
problem and does not justify compaction.

### Review query cost

- `Storage::prediction_stats` and `Storage::hourly_focus_buckets` are SQL aggregates over an
  `idx_predictions_ts` range scan: O(rows in window). A 7-day window is at most ~150k rows,
  tens of milliseconds on disk.
- `Storage::context_app_counts`, `recent_session_summaries`, `productive_session_streak`
  are bounded by explicit limits.
- The one read that scales badly is `AppState::focus_summary_for_window`, which
  materialises up to 50,000 `PredictionRecord`s in C++ on the UI thread under
  `storage_mutex_` and, per F-4, returns a truncated answer beyond ~14 attended hours. That
  is a correctness fix, not a scaling one, and it removes the only unbounded-ish read.

---

## 5. What to measure before building

Each item names the decision it informs.

1. **Prediction rows per attended hour and DB bytes per day**, from a real user database:
   `SELECT strftime('%Y-%m-%d %H', timestamp), count(*) FROM predictions GROUP BY 1`, plus
   file size over a week. Informs: whether §4's realistic column is right; retires CR-08.
2. **Wall time of `prediction_stats`, `hourly_focus_buckets`, `context_app_counts`,
   `focus_summary_for_window`** against a synthetic 90-day on-disk WAL database generated at
   the real cadence — not the in-memory bench. Informs: whether any Review read needs work.
3. **`storage_mutex_` hold time on UI commands and engine persist-phase wait time**, p50 and
   p95, plus how often SQLite's 500 ms busy timeout fires. Informs: whether a second
   read-only connection (improvement 3) is worth doing or is premature.
4. **Idle CPU and wakeups/sec with no session**, and **event-to-prediction p95** under
   sustained typing. Informs: roadmap 14.5's deadline-aware wake; whether no-session
   classification (F-6) costs anything a user would notice.
5. **Capture ring high-water mark and `captureEventsDropped`** over a full working day.
   These are already in `health`; nobody has published a value. Informs: whether the ring
   size and drain budget are right.
6. **Startup time from process start to first frame** with a database at 90-day realistic
   size, with and without a pending `VACUUM`. Informs: priority of F-5.

---

## 6. Comparable products and repositories

Named only where they offer a concrete lesson.

- **ActivityWatch (heartbeat compaction).** The lesson is negative: its merging works
  because its rows are interchangeable duration records with no per-row payload. Snapback's
  rows are training samples with a 31-dimensional feature vector each. Do not import the
  pattern.
- **SQLite's own WAL documentation.** Not a comparator, but the one external reference that
  changes a decision: in WAL mode, readers on a separate connection never block the writer
  and see a consistent snapshot. This directly addresses F-3 without any product change.
- **Rize, Timing, ManicTime, Dayflow, screenpipe.** None offers a lesson stronger than the
  evidence in this repository. Their features were the source of CR-01, CR-03, CR-04, CR-09;
  the verdicts above stand on Snapback's own code and decisions, not on whether those
  products have the feature.

No additional comparator is named to make the list look complete.

---

## 7. Things Snapback should not build

- FTS5 search over context history (CR-03).
- A local HTTP API or MCP server (CR-06c; rule 8.10, ADR-0009).
- Prediction-run compaction (CR-08).
- Auto-update or Velopack integration before an ADR changes the network-silent stance (CR-10).
- UI Automation URL scraping before title redaction exists (CR-09 before CR-05a).
- A fourth, always-on-top Now surface (CR-04).
- IPC code generation (CR-07).
- Performance ceilings asserted on shared CI runners (CR-11).
- Auto-detected or auto-proposed sessions (CR-06a; already decided in ADR-0005).
- PR templates for a sole-author repository (CR-12).
- A day timeline before the headline number beside it is correct (CR-01 before F-4).

---

## 8. Candidate improvements

Ordered by the ratio of user-visible harm avoided to scope. Scope letters follow the
roadmap's convention (`S` hours-to-a-day, `M` days, `L` a week or more).

### 1. Persistence failure → health state, event, backoff — `M`

- **Problem.** Failed writes vanish; the UI shows a frozen dashboard; the engine hot-retries
  the same failing write every tick.
- **Evidence.** `engine_tick` catch path; frontend/src/api.ts (historical line 910) listener with no emitter;
  roadmap 9.6.
- **Scope.** Add a degraded persistence state to `HealthStatus` (`types.hpp`), emit
  `persistence-failed` with `reason`/`message`, add exponential backoff with a cap so a full
  disk is retried every N seconds rather than every 100 ms, surface it in `useHealth` /
  `PermissionsCard` beside the existing capture-failed treatment. Tests: disk-full and
  locked-DB using the injected-failure harness that landed 2026-09-20.

### 2. One longest-focus implementation, in SQL; delete the C++ copy; remove the 50k cap — `S`–`M`

- **Problem.** Wrong answer for windows longer than ~14 attended hours; two divergent
  implementations of the headline number.
- **Evidence.** `state.cpp` 1156–1166; `Storage::prediction_stats`; `engine/focus_summary.hpp`.
- **Scope.** Make `focus_summary_for_window` call the SQL aggregate (extend
  `prediction_stats` if it lacks a field the summary needs), delete `summarize_predictions`
  and its tests or reduce them to a fixture that pins the SQL semantics, remove
  `kFocusSummaryMaxSamples`.

### 3. Read-only second SQLite connection for UI reads — `M`, after measurement

- **Problem.** One app-level mutex serialises readers against the writer that WAL would let
  run concurrently.
- **Evidence.** `analytics`, `session_history*`, `focus_summary_for_window` all take
  `storage_mutex_`; `Storage::open` configures WAL.
- **Scope.** A second `sqlite3*` opened read-only on the same file, used by UI-thread reads;
  `storage_mutex_` stays for writes; `activity_epoch_` and `activity_boundary_mutex_` keep
  the deletion fence. Do §5 item 3 first: if p95 mutex wait is negligible, skip this entirely.

### 4. Delete or wire dead event listeners; add events to the IPC fixture; set-equality instead of a count — `S`

- **Problem.** Four listeners with no emitter; events outside every contract test; a
  hardcoded command count.
- **Evidence.** `api.ts` 893–917, 982; `test_ipc_contract.cpp`.
- **Scope.** Remove `onCaptureFailed` / `onOverlayFailed` / `onLabelHotkey` or emit them
  natively (the first has a working polling path; deletion is cleaner). Add an `events` array
  to `fixtures/ipc_commands.json`. Replace the count assertion with fixture ⊆ registry and
  registry ⊆ fixture.

### 5. Title-pattern redaction before `save_context_snapshot` — `M`

- **Problem.** Verbatim titles on disk for 90 days; app-only exclusion.
- **Evidence.** `storage.cpp` 584–594; ADR-0009; roadmap 8.11.
- **Scope.** A `title_rules` table (pattern, action, created_at); apply in `compute_event`
  before the job is built so neither the DB nor the in-memory `latest` context sees the raw
  string; settings UI beside excluded apps; tests that a redacted title never reaches
  `context_snapshots` or a `SnapbackPayload`. This is CR-05(a) alone.

### 6. Include recent-goal presets in "delete all activity" — `S`

- **Problem.** User-typed goal text survives a full erase.
- **Evidence.** `sessionCockpit.ts`; `delete_all_activity_data`.
- **Scope.** Clear the preset key on the frontend when the delete command resolves; note it in
  the deletion result's `deleted` list.

### 7. Move prune and `VACUUM` off the main thread at startup — `S`–`M`

- **Problem.** Blank window for seconds on a large database.
- **Evidence.** `Storage::open`; roadmap 14.7.
- **Scope.** Migration stays synchronous (the app cannot run on a wrong schema); prune and
  `VACUUM` move to the maintenance thread's first run, under `storage_mutex_`.

### 8. Pomodoro runtime state out of `settings.json` — `S`–`M`

- **Problem.** Config and a ticking timer share one file and one write path.
- **Evidence.** `AppSettings::pomodoro_state` in `src/app/settings.hpp`.
- **Scope.** A small `runtime_state.json` or a `kv` table in the DB for phase, phase start,
  and cycle count; settings writes stop happening on phase change.

### 9. Publish real numbers: rows/hour, DB bytes/day, persist wait p95, idle CPU — `S`

- **Problem.** Every scaling discussion in the roadmap and in the previous review is
  unmeasured.
- **Evidence.** F-8; CR-08 and CR-11 both open with "measure first" and nothing has.
- **Scope.** Extend the `health` / diagnostics command with the counters from §5; a table in
  `docs/testing_strategy.md` or a new docs/budgets.md (historical/proposed path, not a current repository reference) with measured values and the host they
  came from. This replaces CR-08 and CR-11 as build items.

### 10. Split the roadmap — `S`, touches the doc guards

- **Problem.** ~5,000 lines where speculative proposals and decided plans are
  indistinguishable; commit 907517d demonstrated the failure mode.
- **Evidence.** `docs/ROADMAP.md`; items 10.15–10.17, 14.9, 14.10, 2.20, 4.14, 4.15, 9.17,
  7.30–7.32 landed as siblings of accepted work.
- **Scope.** `docs/ROADMAP.md` keeps open items with an explicit status (`proposed`,
  `accepted`, `in progress`); docs/roadmap/done.md (historical/proposed path, not a current repository reference) and docs/roadmap/postmortems.md (historical/proposed path, not a current repository reference) take
  the rest; `scripts/check_doc_paths.py` and `check_doc_symbols.py` run on all three.

---

## Appendix — facts checked against the previous review

| Claim in previous review / roadmap item | Checked against | Result |
|---|---|---|
| Predictions persist on a 100 ms tick | `state.cpp` 2479–2485, state.hpp (historical line 65) | False; 100 ms is the drain tick |
| Predictions persist per input event (CR-08, 14.10) | same | False; ≤ 1/s, attended, non-idle, non-private |
| FTS5 is already in the SQLite amalgamation (CR-03, 10.16) | `CMakeLists.txt` 62–73 | Source present, not compiled; no `SQLITE_ENABLE_FTS5` |
| Titles stored per prediction (10.16) | `storage.cpp` 534–546, 584–594 | False; titles are in `context_snapshots` only |
| macOS already enriches tabs through Accessibility (2.20) | `active_window.cpp` 96–134 | Partly; tab *titles* via `osascript`, no URL |
| Frontend already has a `persistence-failed` event shape (9.6) | api.ts (historical line 910) | True; and nothing emits it |
| Playwright / in-page acceptance already landed (10.1) | not re-verified | Taken as stated by the roadmap |
| Episodes surfaced almost nowhere | not fully traced | Plausible; taken from the roadmap |
