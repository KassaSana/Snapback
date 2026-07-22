# ROADMAP.md — the single source of truth for what to build next

**This file is the live backlog.** Every open task lives here — feature work, wiring gaps,
CI breakage, security hardening, chores. If it's not in this file, it's not planned; if it's
done, it moves to the [Done archive](#done-archive) at the bottom. `CLAUDE.md`'s status
table is a summary that points here — when they disagree, this file wins.

**There is no second backlog.** `docs/TODO.md` used to carry open items and drifted out of
sync — it tracked `2.4b` as a task while this file correctly tracked the same work as the
decision in 5.3. It was **deleted** on 2026-07-20; its history is in git and its `[x]`
entries duplicated the [Done archive](#done-archive) below. Don't reopen a parallel list.

**Last synced against the code: 2026-07-20.** Three passes landed that day: five product
features (privacy, analytics, summary reports, goal categories, diagnostics); Tier 0's four
wiring gaps; an engine/storage audit that opened **Tier 5**; and a staff review of CI,
security, and the app/storage/capture paths that opened **Tiers 6, 7, and 8**. 152 C++ tests
and 38 frontend tests green *on macOS and Linux* — **Windows CI is currently red, see 6.1.**

**A note on trusting this file.** Past audits found items here that were simply wrong: 0.3
described work that was already written (and broken), 2.4 sits in the Done archive on the
strength of code that never runs (see 5.3), and the "Rust ref" path pointed at a directory
that doesn't exist on this machine. **When an item claims something is missing, check
whether it's actually missing before rebuilding it. When an item claims something is done,
check that the code has a caller.**

The Rust→C++ port itself is done; the phase-by-phase playbook is archived in
[PORT_HISTORY.md](PORT_HISTORY.md).

## How to read an item

- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **`decision`** — do **not** write code for this until the question is answered. Roughly a
  third of the open backlog is decisions mistaken for bugs; that mistake has been made
  repeatedly here and has twice produced a "fix" that had to be reverted.
- **Rust ref:** the file to consult when one exists — the original stays the behavioral
  source of truth. **It lives at `../FocoFlow-1/src-tauri/src/`**, not `../Snapback/`
  (same GitHub repo, different local directory name). CI pulls the same tree as
  `KassaSana/Snapback` ref `main-fresh`.
- **C++/Rust delta:** called out when an item re-touches something Tauri/Rust gave us for
  free, since naming that gap is the whole point of this project.

Work each item on the standard loop: code → test → senior-to-junior explanation → commit
(terse one-liner, Kassa's identity, zero AI attribution). Claude commits; only Kassa pushes.

---

## Start here — the current sequence

Ordered by dependency, not severity. This replaces every previous "suggested sequence."

| # | Item | Why now |
|---|------|---------|
| 1 | **6.1** Windows stack overflow | Nothing else is verifiable while CI is red, and it's the smallest fix on the list |
| 2 | **6.4** `actions/checkout` bump | A deadline, not a preference — Node 20 removal breaks *every* job |
| 3 | **6.2 / 6.3** red-master rule + guard decoupling | Cheap, and the reason they matter is fresh right now |
| 4 | **9.1** define what v1 means | **Scopes everything below it.** Without it, all 80 open items look equally required |
| 5 | **12.3** create `docs/adr/` | **Blocks the decision sessions** — eleven items produce decisions with nowhere to land |
| 6 | **8.1** engine-thread exception boundary | A crash in normal use, not just under attack |
| 7 | **7.4 + 7.10** capture + prediction health | These are the instruments 0.3 needs to mean anything |
| 8 | **0.3** live-Mac verification | Now actually measurable |
| 9 | **Decision session A**: 5.3, 5.4, 1.2, 7.7 | One question, four items unblocked — highest leverage on the list |
| 10 | **Decision session B**: 4.11 (incl. the no-separator case) | The diverge-from-Rust call |
| 11 | **7.16** timestamp representation, then 7.1, 7.2 | **7.1 is the highest user-visible impact in this file** |
| 12 | **7.3 + 7.11** migrations + DB fixtures | Unblocks the schema-drift CI job and 9.4's upgrade path |
| 13 | **8.3 + 8.4** CSP + frontend-URL gate | Small, pure defense in depth |
| 14 | **8.5** threat model | Gates whether 4.5's encryption is a requirement; shapes 7.6 and 9.5 |
| 15 | **9.2, 9.7, 9.8** version, empty states, single-instance | Small, and each is visible to the first stranger who runs this |
| 16 | **7.5, 7.6, 7.8, 7.9** | Independent, pick up any time |
| 17 | **10.1** E2E harness | The IPC seam is the one place nothing tests; grows more valuable as surfaces multiply |
| 18 | **7.12 + 7.13** perf | After 4.4 benchmarking, so the fix is measured not guessed |
| 19 | **2.3** model retraining | The biggest product win left; unblocked since 5.1 |

Everything else is opportunistic. **Tier 9 is what turns this from a correct program into a
shippable product** — if the goal is "someone else uses this," 9.1 should arguably be #1.

---

## Tier 6 — CI is red (blocking)

Opened by the 2026-07-20 staff review against run `29728565319`.

- **6.1 — Windows CTest dies with a stack overflow.** `S` ⛔ **blocks everything**

  > **Fixed in code 2026-07-21, awaiting Windows CI confirmation.** `RingBuffer` now owns
  > its storage on the heap (`unique_ptr<T[]>`), fixing the test, `AppState`, and every
  > future caller at once. Reproduced first on macOS with `ulimit -s 1024` (SIGSEGV, same as
  > Windows CI), then verified: 161 test cases / 672 assertions green under the same 1 MB
  > stack. A `static_assert(sizeof(CaptureThread) < 4096)` in `test_capture_thread.cpp` now
  > fails the *compile* if the array ever moves back inline — verified by reintroducing the
  > bug and watching it fire. **Do not move this to Done until Windows CI is green** —
  > consequence 1 below still stands: 138 skipped cases may reveal the next failure.

  Two jobs fail — **C++ headless tests / windows-latest** and **ONNX backend / windows** —
  and they are the *same* failure, because both run the same CTest binary. macOS and Ubuntu
  pass.

  ```
  tests/test_capture_thread.cpp(71): CaptureThread drains hook events in FIFO order
  FATAL ERROR: test case CRASHED: SIGSEGV - Stack overflow
  [doctest] test cases: 25 | 24 passed | 1 failed | 138 skipped
  ```

  **Root cause is arithmetic, not a race.** The test declares `CaptureThread capture;` as a
  local (`test_capture_thread.cpp:72`). `CaptureThread` holds its ring buffer **by value**
  (`capture_thread.hpp:41`), and `RingBuffer` holds `std::array<T, Capacity> slots_{}` by
  value. `CaptureEvent` (`types.hpp:124`) is **96 bytes** on MSVC — 8 (enum + padding) + 8
  (double) + 32 + 32 (two `std::string`) + 16 (four ints).

  **96 × 65,536 = 6,291,456 bytes ≈ 6 MB on the stack.** Windows default thread stack is
  **1 MB** → overflow, every time. Linux and macOS get 8 MB → they fit. That is the entire
  platform split; it is deterministic, not flaky.

  Three consequences beyond the red X:

  1. **138 test cases were skipped, not passed** — the crash aborted the run. Do not assume
     this fix turns CI green; assume it reveals the next problem.
  2. `AppState` holds `CaptureThread` by value (`state.hpp:170`), so **`sizeof(AppState)` is
     also >6 MB**. Production survives *only* because `main.cpp:114` happens to use
     `std::make_unique`. Any future stack-allocated `AppState` crashes the same way. This is
     a landmine, not just a test bug.
  3. Constructing a `CaptureThread` value-initializes 65,536 `CaptureEvent`s — **131,072
     `std::string` constructions** — every time.

  **Fix direction:** heap-allocating in the test is the one-line unblock but leaves the
  landmine. Better is making `RingBuffer` own its storage on the heap (`unique_ptr` to the
  array, or a `vector` sized at construction), which fixes the test, `AppState`, and every
  future caller at once. The buffer is allocated once at startup and never resized, so the
  indirection costs nothing on the hot path — the push/pop atomics and cache-line padding
  that justify this class are untouched.

  **Reproduce before fixing:** this cannot be reproduced on macOS or Linux at default stack
  size. Build on Windows, or constrain the stack locally (`ulimit -s 1024`) and watch it
  fail first.

  *C++/Rust delta: Rust's `Box`/`Vec` would have put this on the heap by default; a
  fixed-size `std::array` member is C++ silently choosing automatic storage for 6 MB.*

- **6.2 — Master has been red all day and commits kept landing.** `S` `process`

  Last five `master` runs: failure, failure, failure, success (Dependabot only), failure.
  Five commits landed anyway, including `fix: type the permission test mock state so
  typecheck passes` — a CI fix that did not fix CI and was not followed up.

  The proximate cause is 6.1. The real finding is that **a red master stopped being a
  signal.** Several items in this file describe CI as a guard; those claims are currently
  false. Fix 6.1, then decide the rule — branch protection, or a stated "red master blocks
  merges" convention.

- **6.3 — The `desktop-app-build` guard silently stops running when CI is red.** `S`

  In run `29728565319`, `Desktop app build` and `Windows desktop integration smoke` both
  show **skipped**, because they `needs:` jobs that failed.

  That job was added on 2026-07-20 specifically because the desktop app had *never* been
  linked off Windows and no CI job built it (see the P0 entry in the Done archive). It has
  therefore barely run since it was created. **A guard that only executes when everything
  else is already green does not guard the case it exists for.**

  The desktop build doesn't depend on the headless suite passing — it depends on the code
  compiling. Decouple the `needs:` graph.

- **6.4 — Dependency updates are blocked behind 6.1, and there's a deadline.** `S`

  The Dependabot PR bumping `actions/checkout` 4→7 is failing CI for the 6.1 reason. Every
  job currently annotates:

  > Node.js 20 is deprecated. The following actions target Node.js 20 but are being forced
  > to run on Node.js 24: `actions/checkout@v4`

  We are already on the forced fallback. When GitHub removes it, **every job fails at
  checkout** — the whole CI system, not one job. The fix is the PR that 6.1 is blocking.

- **6.5 — MSVC warning noise obscures real diagnostics.** `S`

  Every Windows build emits C5285 (`cannot declare a specialization for 'std::tuple'`) from
  `doctest.h`, once per translation unit. Third-party, not ours — but it buries our own
  warnings, which is part of why 6.1 took a crash to surface rather than inspection.
  Suppress at the include site.

---

## Tier 0 — Finish the port's last gaps

- **0.3 — Native macOS capture: confirm it works on a real Mac.** `S`
  **Do 7.4 and 7.10 first.** Right now a live run has no instrument that would reveal the
  tap dying — you would be verifying by vibes.

  **This item was wrong until 2026-07-20.** It described the tap as unwritten; in fact
  `src/capture/input_hook_macos.mm` has had a full `CGEventTap` + `CFRunLoop` backend all
  along (missed by audits because it's the repo's only `.mm` file). It did not work: the
  callback shelled out to `osascript` per keystroke, blew the tap's deadline, and macOS
  disabled the tap without the code ever re-arming it — capture died silently within seconds
  while `capture_running` still reported `true`.

  Fixed in `cc8bf15` (re-arm + cached foreground window + correct run-loop stop); the
  permission prompt landed in `c2a669d`. **All that's left is verification:** run on a Mac
  with Accessibility granted and confirm keystrokes keep reaching the engine under sustained
  mouse movement. No headless test can cover a live tap.

  *C++/Rust delta: Rust got global input from `rdev`; we hand-write the tap and run loop.*

- **0.4b — Provision the signing certificate.** `S` (external dependency)
  The `-SignCertificate` path is wired into `.github/workflows/release.yml` behind the
  `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT` secret; releases stay unsigned until an EV cert is
  purchased and the secret set. Document the acquisition steps in
  [PACKAGING.md](PACKAGING.md) when doing this.

---

## Tier 7 — Correctness & product findings (2026-07-20 staff review)

Covered in the review: `state.cpp`, `classifier.cpp`, `storage.cpp`, `capture_thread.cpp` +
`ring_buffer.hpp`, `tracker.cpp`, `title_parser.cpp`, the IPC/eval boundary, `main.cpp`, and
the frontend XSS surface. **Not covered — un-reviewed, not clean:** `features.cpp` extraction
maths, ONNX internals, the Windows overlay/tray implementations, frontend component
internals, and the benchmark harness.

### Correctness

- **7.1 — Analytics and summary reports silently cover only the last ~3 hours.** `S`
  **Highest user-visible impact in this file.**

  `AppState::analytics()` (`state.cpp:364`) and `summary_report()` (`state.cpp:428`) both
  begin with `recent_predictions(10000)`, then filter in C++. Predictions are throttled to at
  most one per second (`state.cpp:713`), so **10,000 rows ≈ 2 h 46 min of active use.**

  - The **weekly** report (7-day cutoff) can never see past the most recent ~2.8 hours. For
    any regular user it reports on this afternoon and labels it "week."
  - The **daily** report has the same ceiling — wrong for anyone using the app more than
    three hours a day, which is the target user.
  - Hourly buckets (`state.cpp:378`) use the same truncated set, so the "when do you focus
    best" chart only has data for the hours you most recently used the app. It looks
    plausible and is wrong.

  No error, no warning, no test — every test seeds far fewer than 10,000 rows, so the cap is
  structurally invisible to the suite. **Same shape as the `seconds_since_session_start`
  bug:** production takes a branch the tests never do.

  **Fix:** push the window into SQL (`WHERE timestamp >= ?1`) and aggregate there.
  `idx_predictions_ts` already exists for it. Interacts with 7.16 — wrapping the column in
  `datetime()` would defeat that index.

  **Regression test must fail first:** seed >10,000 predictions across several days, assert
  the weekly `sample_count` exceeds 10,000, watch it go red against today's code.

- **7.2 — Hourly analytics are bucketed in UTC and presented as local time.** `S`

  `timestamp_hour()` (`state.cpp:51`) slices characters 11–12 out of strings built by
  `now_rfc3339()` (`state.cpp:69`), which uses `gmtime_r`/`gmtime_s` and appends `Z` — UTC.
  So `AnalyticsHour::hour` is a UTC hour rendered as the user's hour. In US Pacific that is
  an 8-hour lie: "you focus best at 14:00" means 06:00 local.

  Storing UTC is correct; *presenting* it is the bug. Recommend converting in the frontend —
  timestamps are ISO-8601 with `Z`, and `new Date(ts).getHours()` is exactly right.

  Related: `cutoff_rfc3339()` (`state.cpp:37`) computes "1 day ago" as "24 hours ago," so the
  "daily" summary is a rolling 24 h window, not the user's calendar day. Possibly intended,
  nowhere written down, and users read "day" as "today."

- **7.3 — No schema migrations, on a database we deliberately share with the Rust build.** `M`
  *(Split out of 4.5 and promoted — 4.5 keeps the optional-encryption half.)*

  Schema is all `CREATE TABLE IF NOT EXISTS`. No `PRAGMA user_version`, no `schema_version`
  table, **no `ALTER TABLE` anywhere in the codebase.**

  Worse here than in a typical app: `CLAUDE.md` mandates the filename stay `focoflow.db`
  **specifically for install compatibility** with the Rust build. We are promising to open
  databases we did not create. On an existing DB, `CREATE TABLE IF NOT EXISTS` is a no-op —
  it reconciles nothing. Any column the C++ schema has that the user's file lacks produces a
  runtime `no such column` on first insert, for upgrading users only.

  Every test starts from a fresh temp DB. **We have never once opened a real pre-existing
  `focoflow.db`.**

  **Minimum viable:** set `user_version`, add an ordered migration list, and add a test that
  opens a fixture DB built from the *Rust* schema and asserts every current query still runs.
  That fixture is the artifact actually missing — see 7.11.

- **7.4 — A dead capture hook is indistinguishable from a healthy one.** `S`
  **Do before 0.3.**

  Structural version of the macOS bug in 0.3 — and **not macOS-specific.**
  `CaptureThread::start()` (`capture_thread.cpp:12`) spawns a thread whose body is one
  `hook_->run(...)` call. When `run()` returns — normally *or* because the OS tore the hook
  down — the lambda ends. Nothing clears `running_`, which only `stop()` ever clears
  (`capture_thread.cpp:38`). So `capture_.running()` reports `true` for a hook dead for
  hours. That is exactly the state the `CGEventTap` was in when it was silently disabled,
  and why the failure went unnoticed long enough to be filed as "unwritten."

  `AppState::health()` makes it unrecoverable rather than merely unreported:

  ```cpp
  h.capture_failed = false;    // state.cpp:240
  h.capture_stalled = false;   // state.cpp:242
  ```

  **Hardcoded literals.** The diagnostics panel has fields for exactly this failure, wired to
  constants — so the UI isn't just uninformed, it affirmatively reports health it never
  measured.

  **Fix, two halves:** (1) have the hook thread record that `run()` returned and surface it
  as `capture_failed`; (2) track the last-pushed event timestamp and derive `capture_stalled`
  from staleness while a session is active, gated on `IdleDetector` so AFK doesn't trip it.

- **7.5 — Sessions stopped via the no-argument path never get an auto-label.** `S`

  `stop_session(const std::string&)` (`state.cpp:210`) calls `save_auto_session_label()`.
  `stop_session()` (`state.cpp:195`), used on shutdown and internal teardown, **does not.**

  Auto-labels are training data (2.3 consumes them). Every session ending by any path other
  than an explicit UI stop is silently dropped from the corpus — biasing the eventual model
  toward sessions the user deliberately ended, i.e. probably the good ones.

  Also, the two overloads call *different* storage methods (`end_session` vs `stop_session`).
  Check whether that divergence is intentional before unifying.

- **7.9 — Privacy exclusions match by unanchored substring.** `S`

  `is_private_event_unlocked()` (`state.cpp:581`) tests
  `app.find(lower_copy(exclusion)) != npos`. Excluding `Chrome` also excludes
  `chromedriver`. A single-character exclusion — a typo, or an entry that survived trimming —
  excludes effectively everything, and **fails silently**: capture keeps running,
  `capture_running` stays true, no events are ever recorded.

  `normalize_privacy_exclusions()` (`state.cpp:566`) trims and dedupes but doesn't guard
  against over-broad patterns. Since over-exclusion looks identical to a dead capture hook
  (7.4), this needs at minimum a UI warning when a rule matches an implausible share of
  observed apps.

### Decisions — do not code these yet

- **7.7 — `focus_score` and `focus_state` can flatly contradict each other in the same row.**
  `S` `decision` — **settle together with 5.3, 5.4, and 1.2.**

  `apply_focus_guardrails()` (`classifier.cpp:186`) overrides `focus_state` to
  `"DISTRACTED"` on risk-over-threshold, `thrash >= 0.75`, or a `personal_block` rule. It
  does **not** touch `focus_score` or `distraction_risk`. Meanwhile `focus_score` is a
  probability-weighted average over all four classes (`classifier.cpp:71`).

  So a row where the model is confidently `DEEP_FOCUS` but the app matches a Block rule is
  written as `focus_state = 'DISTRACTED', focus_score = 95`. Both columns are then consumed
  independently and mixed freely — `recap()` averages score *and* counts DISTRACTED rows;
  `summary_report()` computes `longest_focus_streak` from state and `avg_focus_score` from
  score. A blocked-app session reports a high average focus score and a distracted-heavy
  state breakdown simultaneously, and the UI shows both without comment.

  Options: clamp `focus_score` in the guardrails; drop score from surfaces showing state; or
  document them as "model opinion" vs "policy verdict" and label them so in the UI.

- **7.8 — `set_focus_mode` permanently rewrites the user's default.** `S` `decision`

  `set_focus_mode()` (`state.cpp:324`) sets the live mode *and* writes
  `settings_.default_focus_mode` to disk on every call. Switching to Recovery once for a
  rough afternoon makes Recovery the startup default forever — silently overwriting the
  answer the onboarding wizard (1.1) explicitly asked for.

  Decide whether "current mode" and "default mode" are one setting or two. They're currently
  one; the wizard's existence implies two.

- **7.16 — Settle how this app represents time.** `S` `decision` → then `M` to apply

  **Four separate findings share one root cause:** timestamps are free-form text compared
  with SQL date functions.

  - **5.5** — `datetime(timestamp) < datetime(?1)` yields NULL on an unparseable value, so
    retention silently never deletes; and wrapping the column defeats `idx_predictions_ts`.
  - **7.1** — string `<` comparison against a cutoff, after a row-count cap.
  - **7.2** — UTC hour slicing presented as local.
  - **`now_rfc3339()` uses `std::time(nullptr)`** — whole-second resolution
    (`state.cpp:69`). Throttling makes collisions rare but not impossible, and ordering
    within a second is undefined for any `ORDER BY timestamp`.

  **Do not fix these separately.** Decide once — canonical format, storage type (text vs
  epoch; note `feature_snapshots.timestamp` is already REAL epoch seconds, so the schema is
  *already* inconsistent), index compatibility, presentation zone — and all four fall out.
  This is a domain-modeling conversation and an ADR, not four patches.

### Product gaps

- **7.6 — There is no way for a user to delete their own data.** `M`

  Full sweep of `src/app/commands.hpp`. Present: `set_private_mode`,
  `set_privacy_exclusions`, `get_privacy_settings`, 90-day auto-prune. **Absent:** delete all
  data now; delete a single session; export my data in a legible form (`export_training_data`
  produces ML-shaped CSV); open the data folder.

  For an app whose core function is recording every keystroke and window title, "you may
  inspect and destroy what I collected" isn't a nice-to-have — it's what makes local-only
  credible. The onboarding wizard already makes the promise; this is its enforcement.

  Ties to 7.3 (honest escape hatch for an unmigratable DB) and 8.5 (the threat model should
  drive its shape).

### Observability & test coverage

- **7.10 — Nothing measures whether predictions are still being produced.** `S`
  **Cheapest high-value item in this file.**

  `HealthStatus` reports `capture_running`, `capture_events_dropped`, and the classifier
  backend. Nothing reports **prediction freshness.** The tick can produce nothing for
  legitimate reasons (idle freeze, throttle, no session, private mode) and illegitimate ones
  (dead hook 7.4, over-broad exclusion 7.9, classifier throwing 8.1). From outside these are
  indistinguishable, and the UI shows the last prediction with no indication of its age.

  Add `last_prediction_age_secs` plus a suppression reason (`idle` / `no_session` /
  `private_mode` / `none`). **This single change makes every silent failure mode in this
  file visible** — 7.4, 7.9, and 8.1 all surface through it.

- **7.11 — No test ever opens a pre-existing database.** `M`

  The general form of 7.3. Every storage test builds a fresh temp DB. Untested as a result:
  migration (7.3), retention against aged data (5.5), index usage as tables grow (7.12),
  recovery from a corrupt or partially-written DB, and **WAL recovery after unclean
  shutdown** — which, for an always-on tray app users will kill via Task Manager, is the
  *normal* shutdown path, not an edge case.

  Build a fixture corpus (fresh, aged, large, Rust-authored, corrupt) and run the storage
  suite against each.

### Performance

- **7.12 — Analytics and history do N+1 queries under the storage lock.** `M`

  `analytics()` (`state.cpp:355`), `summary_report()` (`state.cpp:415`), and
  `session_history()` (`state.cpp:309`) all loop over sessions issuing per-session queries:

  - `analytics()`: `recent_sessions(200)` × `list_context_snapshots(…, 200)` — up to 40,000
    rows — then a **second** `recent_sessions(200)` loop calling `recap()` (itself 4 queries).
  - `summary_report()`: `recent_sessions(500)` × `list_context_snapshots(…, 200)` — up to
    100,000 rows — plus a `recap()` per completed session.
  - `session_history()`: ~4 × limit queries.

  All holding `storage_mutex_`, synchronously, on the thread answering the UI. The engine
  tick's persist phase takes the same lock (`state.cpp:655`), so **opening the analytics tab
  can stall the capture pipeline's writes** — which, with a bounded ring buffer, means
  dropped events.

  Invisible today because the DB is small; it scales with usage, so your most engaged users
  hit it first. Aggregate in SQL; add the indexes in 7.13. **Measure before rewriting** —
  4.4 wants a perf gate anyway and this is its natural first benchmark.

- **7.13 — Missing indexes on two hot foreign keys.** `S`

  Indexes exist for `predictions(session_id, timestamp)`, `predictions(timestamp)`,
  `feature_snapshots(session_id, timestamp)`, `sessions(status, started_at)`, and
  `context_snapshots(session_id, timestamp)`. **None on `snapback_events(session_id)` or
  `labels(session_id)`.** `recap()` runs `SELECT COUNT(*) FROM snapback_events WHERE
  session_id = ?1` — a full scan — and per 7.12 `recap()` runs in a loop.

### Hygiene

- **7.14 — Test-only methods on the production `AppState` class.** `S`

  `process_event_for_test`, `update_idle_for_test`, `start_pomodoro_for_test`, and
  `update_pomodoro_for_test` (`state.hpp`) are public production API compiled into the
  shipping binary. They exist because the tick reads a real clock. **The deeper fix is the
  seam:** inject a clock and they stop being necessary — which also lets you test
  idle/pomodoro/throttle interactions at real time scales without sleeping. Exactly the
  "find a testable seam" move that made 5.1's fix possible.

- **7.15 — Classifier weights are ~20 undocumented magic numbers.** `M`
  **Land before 1.2 is implemented, ideally before 2.3.**

  `classifier.cpp` carries dozens of unnamed constants: `0.45/0.25/0.30` in `thrash_score`,
  `4.0`, `10.0`, `180.0`, `8.0`, `120.0`, `0.65`, and the whole `distracted` accumulator
  (lines 161–169). Only `kThrashDistractedThreshold` and `kDriftPseudoThreshold` are named.

  Matters more than usual because **1.2** asks what "sensitivity" should tune — unanswerable
  while nobody can say which numbers are user-facing tunables vs structural — and **2.3**
  replaces some with a model while keeping others as the blend layer, and which-is-which is
  tribal knowledge living in one commit message.

  Extract to a named, documented table citing the `../FocoFlow-1/src-tauri/src/` line each
  was ported from. Not cosmetic.

- **7.17 — `stop()` never resets the dropped-event counter.** `S`

  `CaptureThread::stop()` (`capture_thread.cpp:30`) leaves `dropped_` intact, so
  `capture_events_dropped` is cumulative across start/stop cycles within a process. Probably
  intended — but undocumented, so the number can't be read as "drops this session." Decide
  and write it down.

---

## Tier 8 — Security hardening (2026-07-20 staff review)

**No exploitable vulnerability was found.** Except for 8.1 — a real availability bug in
normal use — these are defense-in-depth and fragility items.

**What is already right**, recorded so a future review doesn't re-derive it: every SQL
statement is parameterized (no string-built queries in `storage.cpp`); no `innerHTML`,
`dangerouslySetInnerHTML`, or `eval()` in the frontend; the `popen`/`std::system` call sites
in `active_window.cpp:32` and `permissions.cpp:18` take compile-time literals;
`training_deploy.cpp` quotes the user-supplied repo path; `npm audit --production` reports 0
vulnerabilities and the `security-audit` job is green; and the hook callback correctly
swallows all exceptions (`capture_thread.cpp:17`) since unwinding through an OS callback is UB.

- **8.1 — The engine thread has no exception boundary; any throw kills the app.** `S`
  **Highest-value reliability item in this tier.**

  ```cpp
  engine_thread_ = std::thread([this] {           // state.cpp:161
      while (engine_running_.load(std::memory_order_relaxed)) {
          engine_tick();
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
  });
  ```

  **No `try`/`catch` anywhere.** An exception escaping `engine_tick()` is an unhandled
  exception on a thread → `std::terminate` → the whole process dies, taking the tray, the UI,
  and any unflushed state with it.

  `engine_tick()` reaches plenty that throws:

  - `nlohmann::json(...).dump()` (`state.cpp:664`–`666`) defaults to
    `error_handler_t::strict`, which **throws `type_error.316` on invalid UTF-8**. Payloads
    embed `app_name` and `window_title`, which come from the OS. On Windows these arrive via
    `WideCharToMultiByte` and are well-formed; on Linux (X11 `WM_NAME` / evdev) a title is
    arbitrary bytes. **A program with a malformed title in its window name crashes Snapback.**
  - `persist()` → storage writes, which throw `std::runtime_error` on any SQLite failure —
    disk full, database locked, permissions. All normal, recoverable conditions.
  - `classifier_.predict(...)` and `features_.extract(...)`, which allocate.

  The author knew this mattered elsewhere: `main.cpp:161` wraps the snapback payload parse in
  `try { … } catch (...)` with the comment *"A malformed payload must never take down the UI
  thread."* The engine thread, which handles strictly more untrusted input, has no such guard.

  **Fix:** wrap the tick body, log at Error, keep looping. Decide explicitly what's fatal vs
  transient — a persistent storage failure probably *should* stop the engine and surface via
  7.10 rather than spin silently. Also consider `dump(-1, ' ', false,
  error_handler_t::replace)` so malformed titles degrade to replacement characters instead of
  throwing at all.

  *C++/Rust delta: Rust's `Result` would have forced this at the type level, and a panicking
  thread wouldn't take the process with it.*

- **8.2 — `emit()` splices JSON into a JavaScript `eval` string.** `S`

  ```cpp
  inline void emit(webview::webview& w, const char* event, const std::string& json_payload) {
      w.eval("window.__snapback && window.__snapback.emit(\"" + std::string(event) +
             "\", " + json_payload + ")");                            // commands.hpp:222
  }
  ```

  `event` is always a compile-time literal; `json_payload` is `dump()`, which escapes quotes,
  backslashes, and control characters. **Not currently exploitable.**

  It is listed because it is string concatenation into a live interpreter carrying
  attacker-influenced content (window titles), with one property holding the line: that JSON
  is a subset of JavaScript. That subset has a famous exception — **U+2028/U+2029 are valid
  raw in JSON strings but were line terminators in JS before ES2019**, and `dump()` defaults
  to `ensure_ascii = false`, so they *are* emitted raw. Modern WebView2/WKWebView/WebKitGTK
  are all ES2019+, so it holds today. The margin is one assumption wide.

  **Fix:** pass through a parse boundary instead of splicing — encode the payload as a
  properly-escaped JS string literal and `JSON.parse` it, or set it on a global and call a
  zero-argument function.

- **8.3 — No CSP, and the IPC shim exposes every command to any script in the page.** `M`

  There is **no Content-Security-Policy** anywhere — not in `frontend/index.html`, not set by
  the host. The shim resolves commands by global lookup (`ipc_shim.hpp:70`):

  ```js
  var bound = window[cmd];
  ```

  So every `webview.bind()`-exposed command is reachable from any JavaScript in the page —
  including `set_training_repo_path` and `train_from_export`, which **terminate in
  `std::system`** (`training_deploy.cpp:345`).

  The chain: any script execution in the webview → full local command surface → arbitrary
  process launch. The only thing preventing step one is that React escapes everything and the
  app has no `innerHTML` sink. That is a real defense, but a *single* one, in an app whose
  primary input is window titles from arbitrary third-party programs, rendered back into the
  UI (context timeline, `"Return to <title>"` summaries).

  Add `default-src 'self'; script-src 'self'`. Costs nothing, doesn't affect the shim (which
  is injected via `webview.init`, not fetched), and converts "one React bug from RCE" into
  "two independent failures from RCE."

- **8.4 — `SNAPBACK_FRONTEND_URL` lets any local process redirect the webview.** `S`

  ```cpp
  w.navigate(resolve_frontend_url(executable_dir(), env_var("SNAPBACK_FRONTEND_URL")));
  ```
  `main.cpp:195` — read unconditionally in **release** builds. Any process that can influence
  the environment Snapback launches with can point the webview at arbitrary remote content,
  which then inherits the entire command surface from 8.3.

  Gate behind a debug build, or allowlist `http://127.0.0.1:*` / `http://localhost:*`. The
  sibling QA hooks (`SNAPBACK_OVERLAY_TEST`, `SNAPBACK_NOTIFICATION_TEST`,
  `SNAPBACK_GUI_SESSION_SMOKE`) are benign — keep them.

- **8.5 — Write a threat model.** `M` `decision`

  `focoflow.db` is an unencrypted SQLite file holding a complete history of window titles,
  app names, and derived behavioural features. Any process running as the user can read it;
  the WAL and exported training CSVs have the same exposure.

  That may be entirely the right call for a local-only tool — but it's currently an
  **unstated** call. The onboarding wizard promises "local-only," which users reasonably hear
  as "private," and those are different claims.

  Write down who the adversary is (other local users? malware running as the user? someone
  with the laptop?), what's in scope, and what "local-only" actually promises. That document
  then decides whether SQLCipher (4.5) is a requirement or a nice-to-have, and is the honest
  input to 7.6.

- **8.6 — No dependency pinning story for FetchContent.** `S`

  C++ deps come via CMake `FetchContent` (`webview/webview`, `nlohmann/json`, `doctest`).
  Dependabot covers Actions and frontend npm but **not** these — nothing watches them for
  advisories and nothing verifies what got fetched. Pin to commit SHAs rather than tags (tags
  are mutable) and note the update process.

---

## Tier 1 — Ship a polished Windows-first v1

- **1.2 — Settings UI: distraction sensitivity tuning.** `M` `decision`
  **Settle together with 5.3, 5.4, and 7.7 — they are one question.** Also wants 7.15 done
  first, so the tunables are legible.

  App-rule management (`RulesCard`) and default focus mode are **done** — see Done archive.
  What's left is a real gap: there is no user-facing "sensitivity" concept in the backend at
  all. `risk_threshold(mode)` in `classifier.cpp` is a hardcoded function of `FocusMode`
  (deep/normal/recovery already *are* the sensitivity levers). Exposing a further per-user
  tunable requires deciding what it means — a scalar multiplier on `risk_threshold`? A
  per-mode override in `AppSettings`? Don't build a UI until that's decided.

---

## Tier 2 — Product & ML depth

- **2.3 — Model retraining loop.** `L` — **unblocked since 5.1; biggest product win left.**
  Wire the `ml/` trainer to consume the exported CSV + the user's own labels → produce a
  fresh `model.onnx`. Opens the door to on-device personalization.

  **Do not start this as one item — it is at least seven.** The operational half (versioning,
  evaluation gates, rollback, drift, and whether enough labelled data even exists) is broken
  out in **Tier 13**, and **13.5 may rescope 2.3 entirely** if the label corpus turns out too
  small to train on. Also: bundle **5.6** here (it needs both extractors changed together
  plus a retrain), and fix **7.5** first or the corpus stays biased toward
  deliberately-ended sessions.
  *Rust ref: `ml/`, `engine/onnx_model.rs`.*

---

## Tier 3 — Cross-platform breadth & packaging

- **3.0 — Autostart on macOS and Linux.** `M`
  `autostart.cpp:77-81` is a hard-coded `return false` off Windows, so the settings toggle
  correctly greys out — the feature is simply absent. Needs a launchd `LaunchAgent` plist
  (`~/Library/LaunchAgents/`) on macOS and a systemd **user** unit on Linux. Concrete scope of
  the follow-up noted on 1.3.

- **3.1 — macOS tray + native overlay.** `M`
  Replaces the no-op stubs in `src/snapback/overlay_stub.cpp` / `src/app/tray_stub.cpp`.
  `NSStatusItem` tray menu and a native always-on-top overlay panel matching Windows.
  *C++/Rust delta: Tauri's cross-platform tray/window, re-solved per-OS.*

- **3.2 — Linux tray + overlay.** `M`
  Same stubs as 3.1. `libappindicator` tray + an overlay window (X11/Wayland caveats noted).

- **3.3 — macOS packaging.** `L` — `.app` bundle + notarization + DMG.

- **3.4 — Linux packaging.** `M` — AppImage and/or Flatpak.

- **3.5 — In-app "check for updates".** `M`
  Fetch a version manifest and offer a download link — no silent install. The lightweight
  variant of the auto-updater deferred in [PACKAGING.md](PACKAGING.md).

---

## Tier 5 — Open findings from the 2026-07-20 engine/storage audit

**Verifying each against the Rust reference before fixing changed the answer twice.** 5.4 and
5.6 turned out to be faithful ports of deliberate reference behavior, not port bugs — and 5.6
would have failed the `feature-parity` CI job had it been "fixed" unilaterally. Both are now
decision items. **An audit finding is a hypothesis; check it against
`../FocoFlow-1/src-tauri/src/` before writing code.**

Done: 5.1, 5.2, 5.7, 5.8, 5.9 (details in the [Done archive](#done-archive)).

- **5.3 — `confidence.hpp` is dead code with inverted units.** `S` `decision`
  **Settle together with 5.4, 1.2, and 7.7.**

  Nothing outside its own test calls `should_nag` or `distraction_confidence`. Worse, the
  header documents risk as `[0,100]` and sets `nag_threshold = 60.0`, but the classifier emits
  `[0,1]` (`classifier.cpp:70`) — so `should_nag(0.9)` returns **false**. The tests pass only
  because they feed 0–100 values the system never produces. **Decide: wire it in on a `[0,1]`
  scale, or delete it and drop the 2.4 "confidence gating" claim** — currently in the Done
  archive on the strength of code that never runs.

- **5.4 — What should `thrash_spikes` measure?** `S` `decision`
  **Settle together with 5.3, 1.2, and 7.7.**

  `recap()` counts `distraction_risk >= 0.7 AND focus_state = 'DISTRACTED'`. The audit called
  the constant a bug (the mode's threshold is 0.55/0.70/0.85); **it isn't, or at least not
  obviously.** Two things checked before attempting a change:

  1. The Rust original hardcodes the identical 0.7 (`storage/mod.rs:633`) — a faithful port.
  2. There's a coherent reading where 0.7 is an *absolute* "strong distraction" bar,
     deliberately mode-independent so Deep mode's higher sensitivity doesn't inflate
     session-quality metrics that feed auto-labels.

  Switching to `risk_threshold(mode)` was tried and reverted: it breaks the recap test and
  *increases* mode-dependence for Deep.

  The residual oddity is different: `apply_focus_guardrails` also marks rows `DISTRACTED` via
  `thrash >= 0.75` or a `personal_block` rule with no risk floor. So a blocked-app row at risk
  0.3 is `DISTRACTED` but never a "spike," while Recovery rows between 0.7 and 0.85 are never
  `DISTRACTED` and so never counted. **7.7 is the same seam from the other side.** Decide what
  the metric means — absolute intensity, mode-relative alerting, or plain
  `COUNT(*) WHERE focus_state = 'DISTRACTED'` — then make the query say it.

- **5.5 — Retention silently no-ops on unparseable timestamps.** `S`
  **Roll into the 7.16 timestamp decision — same root cause.**

  `storage.cpp:1004`/`:1011` use `datetime(timestamp) < datetime(?1)`. If `datetime()` can't
  parse a stored value it yields `NULL`, the comparison is `NULL`, and the row is **never
  deleted** — retention degrades with nothing surfaced. Wrapping the column also defeats
  `idx_predictions_ts`, forcing a scan on every startup prune.

- **5.6 — `longest_active_stretch_5min` reports 300s for brand-new sessions.** `M` `decision`
  — **do not "just fix" this; it will fail CI. Bundle into 2.3.**

  `features.cpp:190` defaults to the whole 5-minute window when it holds no idle events, so
  ten seconds into a session the extractor claims a five-minute unbroken active stretch.

  1. The Rust original does exactly the same (`features.rs:315`).
  2. **The `feature-parity` CI job diffs every key of the C++ feature vector against the Rust
     extractor** (`scripts/run_feature_parity_dual.py`). Changing this in C++ alone fails it.

  Defensible as-is: the feature is defined over a fixed window, not the session. The real
  question is whether a feature that is constant-300 for most users carries signal at all —
  which is a 2.3 question, since answering it means changing both extractors and retraining.

---

## Tier 4 — Engineering quality & hardening (cross-cutting)

- **4.11 — `title_parser` fabricates filenames.** `M` `decision`

  Two distinct defects, one root cause — `parse_title` has no notion of "does this look like
  a filename?":

  1. **Separator case.** It splits on `" — "` / `" - "` and treats segment 0 as a filename
     with no check. `"Some Article - Google Chrome"` yields `file_hint = "Some Article"`, and
     `tracker.cpp:104` turns that into **"Editing Some Article"**.
  2. **No-separator case (worse).** `title_parser.cpp:26`:
     `if (hints.file_hint.empty()) hints.file_hint = window_title;` — with no separator at
     all, **the entire title becomes the file hint.** `build_snapback()` (`tracker.cpp:130`)
     then renders `"Return to " + file_hint`, so a fullscreen video titled
     `Why Rust Is Better Than C++` produces the overlay **"Return to Why Rust Is Better Than
     C++"** — the product's namesake feature telling you to go back to the distraction.

  **Needs a decision first:** the Rust original has the *same* bug (`title_parser.rs:35`), so
  a faithful port will not fix it — fixing means deliberately diverging from the source of
  truth. Cheapest fix is to consult `title_is_distracting`, which `app_context.cpp:125`
  already computes and `make_snapshot` ignores.
  *Rust ref: `title_parser.rs` — note it takes `app_name`, which the C++ signature doesn't,
  so per-app title conventions are currently unimplementable. The decision should settle
  whether to add that parameter.*

- **4.2 — Fuzz the untrusted boundaries.** `M`
  libFuzzer targets for `title_parser`, the JSON IPC arg parsing, **and the Windows shell
  quoting in `training_deploy.cpp`** — `cmd.exe` metacharacter handling (`^`, `%VAR%`, `&`,
  embedded quotes) is a genuinely different escaping problem from POSIX `sh`, and the same
  `quote()` serves both (`training_deploy.cpp:337`). `%` in particular is not neutralized by
  double-quoting in `cmd.exe`. Low severity (self-injection from a user-entered path), but
  it's the natural third target.

  **Consider instead:** building the process directly (`CreateProcessW` / `posix_spawn`) with
  an argv array removes the entire quoting problem class rather than escaping it correctly.
  *C++/Rust delta: Rust bounds-checks slices for free; our manual index math in the parser is
  exactly what fuzzing should hammer.*

- **4.3 — Opt-in crash reporting.** `M`
  Windows minidump capture on unhandled exceptions, written locally, opt-in only. Note 8.1
  reduces how often this fires; do 8.1 first.

- **4.4 — Perf regression gate.** `M`
  Profile `engine_tick` allocations (the compute path already defers work — measure it), then
  add a threshold to the benchmark harness so a regression fails CI. **7.12 is its natural
  first benchmark.** *See [benchmarking.md](benchmarking.md).*

- **4.5 — Optional encryption at rest.** `M`
  Optional SQLCipher for the local DB. *(The schema-versioning half of this item was split
  out and promoted to 7.3.)* **Whether this is required at all is decided by 8.5** — don't
  build it before the threat model exists.

---

## Tier 9 — Ship a v1 (release readiness)

**The gap this tier closes: there is no written definition of "shipped."** Tiers 0–8 are all
"make the thing correct." This is "make the thing releasable to a stranger." Every item was
scoped by walking the lifecycle a real user goes through — install, first run, daily use,
upgrade, failure, uninstall — and asking what's missing at each step. Most of these are
small; the tier is large because nobody has walked that path yet.

- **9.1 — Write down what v1 means.** `S` `decision` — **do this first; it scopes the rest.**
  There is no release checklist, no scope freeze, no "we ship when X." Without it, every item
  in this file looks equally required and the project never converges. Decide: which OSes at
  v1 (Windows-only is a legitimate answer and makes Tier 3 post-v1)? Which of Tiers 7/8 are
  blockers vs. fast-follows? What is explicitly *not* in v1? Output is a short checklist that
  the rest of this file gets measured against.

- **9.2 — One version number, surfaced everywhere.** `S`
  The version `0.2.0` is written in **two independent places** — `CMakeLists.txt:200`
  (`CPACK_PACKAGE_VERSION`) and `frontend/package.json:4` — with nothing keeping them in
  sync. There is **no `get_version` IPC command**, so the UI cannot display a version and
  `DiagnosticsSnapshot` cannot report one. A bug report today cannot say which build it came
  from, which makes 4.3 (crash reporting) far less useful when it lands. Single-source it in
  CMake, thread it through to the frontend and diagnostics.

- **9.3 — No CHANGELOG.** `S`
  There is no `CHANGELOG.md` anywhere. The release workflow is tag-driven, so releases
  currently ship with no human-readable statement of what changed. Start one now while the
  history is still recoverable from git and this file's Done archive.

- **9.4 — Walk the upgrade path once, deliberately.** `M`
  Nobody has ever installed version A and then upgraded to version B. Unknowns worth
  resolving before a stranger hits them: does the DB survive (7.3 says probably not); does
  the HKCU Run key survive a reinstall to a new path, or does autostart silently point at a
  deleted binary; do settings persist; does a running instance get replaced cleanly?
  Depends on 7.3.

- **9.5 — Uninstall leaves no surprises.** `S`
  Decide and implement what uninstall removes. Today it plausibly leaves behind: the
  `focoflow.db` with full window-title history, the HKCU Run key (a startup entry pointing at
  a deleted binary), the log files and rotated backups, and the exported training CSVs. For a
  keystroke-recording app, **leaving the database behind after uninstall is the worst of the
  four** — the user believes they removed it. Ties to 7.6 and 8.5.

- **9.6 — Failure UX: what does the user actually see when it breaks?** `M`
  The backend will soon report rich failure states (7.4, 7.10, 8.1), but there is no designed
  response to any of them. Specify what the UI does when: capture permission is revoked
  *mid-session* (macOS lets the user do this at any time); the hook dies; the disk is full so
  writes fail; the DB is locked by another instance; predictions have gone stale. Right now
  most of these render as a dashboard that simply stops updating, which is
  indistinguishable from "you're doing great."

- **9.7 — Empty states.** `S`
  Every analytics surface is built for a user with history. A brand-new user's first run
  shows analytics, summary reports, insights, and focus summary — all computed over zero
  rows. Verify what each renders (0? NaN? a blank chart?) and design the first-run state.
  Cheap, and it's literally the first thing every new user sees.

- **9.8 — Single-instance guard.** `S`
  Nothing prevents two Snapback processes running at once. Both would install OS-wide input
  hooks, both would open the same SQLite file, and both would write predictions for
  overlapping sessions. WAL makes this survivable rather than corrupting, but the data is
  garbage and the CPU cost doubles. Autostart plus a manual launch is the obvious way in.

- **9.9 — Support bundle export.** `S`
  4.10 added an in-app diagnostics panel; the natural completion is a one-click "export
  diagnostics" that writes health status, recent log tail, version (9.2), and OS/build info
  to a file the user can attach to a bug report — with an explicit note about what it does
  and does not contain. Pairs with 7.6's data-export work; same plumbing.

---

## Tier 10 — Frontend & UX (largely un-reviewed)

**Flagged honestly: the frontend was inventoried, not reviewed.** 40 source files, 22 test
files, all component/unit tests with mocked IPC. The items below are the ones visible from
structure alone — a real review would likely find more.

- **10.1 — Nothing tests the real binary against the real UI.** `L`
  There is **no E2E framework** — no Playwright, no Cypress, nothing in
  `frontend/package.json`. Frontend tests mock `invoke()`; C++ tests run headless. **The
  actual seam between them — the webview bridge — is tested only by
  `test_ipc_contract`'s name matching.** So a command whose *name* matches but whose payload
  shape drifted passes every test and breaks the UI at runtime, which is precisely the
  failure CLAUDE.md calls "silently breaks the UI." The `windows-desktop-integration` job is
  the closest thing and it's currently skipped (6.3).

- **10.2 — The dashboard is ~20 cards on one page.** `M` `decision`
  `App.tsx` renders 34 card/wizard references. Every feature shipped as "another card,"
  which was right while porting and is now an information-architecture problem: no
  navigation, no hierarchy, no progressive disclosure. A new user's first screen shows
  session control next to ONNX training deployment. Needs a design decision (tabs? routes?
  a settings/advanced split?) before more cards land — and 7.6, 9.6, and 9.7 all want to add
  surfaces.

- **10.3 — Accessibility has never been assessed.** `M`
  No audit has been done. Specifically worth checking: keyboard navigation through the card
  grid; focus management when the snapback overlay appears (it steals attention by design —
  does it trap focus?); screen-reader labelling of the score/state tiles; whether the
  distraction states are distinguishable without color; and whether the always-on-top
  overlay respects reduced-motion and OS contrast settings. A focus tool that fights
  assistive tech is a bad look.

- **10.4 — Re-render cost on the prediction event.** `S`
  Predictions emit up to once per second (`state.cpp:713`), and `useAppEffects.ts` runs three
  timers on top. Nobody has measured what re-renders on each event. For an app that runs all
  day in the background, idle CPU is a feature — a dashboard that burns cycles while
  minimized undercuts the product's premise. Measure before optimizing.

- **10.5 — Frontend has coverage tooling but no gate.** `S`
  `@vitest/coverage-v8` is configured (`vite.config.ts:16`, `npm run test:coverage`) but no
  CI job runs it and no threshold is enforced. Either wire it up with a floor or drop the
  dependency — right now it's a capability nobody uses.

- **10.6 — No C++ coverage measurement at all.** `M`
  The frontend can measure coverage; the C++ side cannot. Given how many bugs in Tiers 5/7
  were "the tests never exercised the production branch" (`seconds_since_session_start`, 7.1,
  5.3), a coverage report is the cheapest tool for finding the next one. `gcov`/`llvm-cov` on
  the Linux CI job.

---

## Tier 11 — Test infrastructure

- **11.1 — One crash hides every test behind it.** `S`
  6.1 made this concrete: a single SIGSEGV aborted the run and **138 test cases were
  reported as *skipped*.** The whole suite is one CTest entry (`snapback_tests`), so any
  crash blinds us to everything after it. Register test cases as separate CTest entries, or
  shard the binary, so a crash costs one result instead of the run.

- **11.2 — Property-based tests for the numeric core.** `M`
  `features.cpp` and `classifier.cpp` are pure functions over a feature vector — the ideal
  target for property testing, and currently covered only by example-based cases. Properties
  worth asserting: `focus_score` always in `[0,100]`; `distraction_risk` always in `[0,1]`;
  `focus_state` always one of the four labels (this one has already been violated — 5.2);
  probabilities sum to 1; and monotonicity where the model claims it (more thrash never
  *decreases* distraction risk). Would have caught 5.2 and 5.3 mechanically.

- **11.3 — Golden-file test for the feature vector.** `S`
  The feature-vector order is declared a contract with the model and the CSV exporter
  (CLAUDE.md), enforced only by the dual-language parity job. A checked-in golden CSV for a
  fixed input, diffed on every run, makes a reordering fail loudly and locally rather than in
  a cross-language job people may not read.

- **11.4 — A deterministic clock, injected.** `M`
  Time is read directly in at least three places (`state.cpp:69`, `:82`,
  `storage.cpp`'s `CURRENT_TIMESTAMP`). This forces sleep-based tests, blocks testing
  idle/pomodoro/throttle interactions at real durations, and is the direct cause of the
  `_for_test` methods in 7.14. One injected clock seam fixes all of it. **Pairs naturally
  with 7.16** — settle what time *is* here, then settle where it comes from.

- **11.5 — Fixture corpus for storage.** `M`
  Tracked as 7.11; listed here so the testing story is complete in one place.

---

## Tier 12 — Documentation truth

**Every item here is a doc asserting something false.** This tier exists because that has now
happened often enough to be a category, not an accident. Two root causes recur: docs written
*before* the code (plans that were never reconciled), and docs written *about* code that
later moved.

- **12.1 — `ARCHITECTURE.md`'s module map is the pre-port plan, not the built shape.** `S`
  Verified 2026-07-20: five claimed C++ paths don't exist (`app/events.hpp`,
  `engine/goal_alignment.*`, `capture/active_window_*.cpp`, `capture/permissions_*.cpp`,
  `snapback/overlay.cpp`), and two library choices were never taken (`spdlog`, `stduuid`).
  A warning table has been added inline as a stopgap; the map itself still needs
  reconciling. *Keep the Rust→C++ reasoning — that's the teaching value. Fix the file list.*

- **12.2 — Audit the remaining docs against the code.** `M`
  `system_architecture.md`, `testing_strategy.md`, `benchmarking.md`, `windows_demo.md`, and
  `PACKAGING.md` have not been verified since they were written. Given the hit rate on
  `CLAUDE.md` (six false claims), `ARCHITECTURE.md` (seven), and this file (three), assume
  they contain errors until checked. `docs-smoke` in CI only checks that docs exist.

- **12.3 — There is nowhere to record a decision.** `S` — **blocks the decision sessions.**
  No `docs/adr/`, no decision log, nothing. Meanwhile this file carries **fourteen
  `decision`-tagged items** (1.2, 4.11, 5.3, 5.4, 5.6, 7.7, 7.8, 7.16, 8.5, 9.1, 9.10, 10.2,
  13.5, 13.6) whose
  entire output is *a decision and its reasoning*. Without a home, those answers land in a
  chat log and evaporate — and the next audit re-derives the same question, which is exactly
  how 5.4 and 5.6 got "fixed" and reverted. Create `docs/adr/` with a one-page template
  before the first decision session, not after.

- **12.4 — A "how do I actually run this" doc, per OS.** `S`
  `testing_strategy.md` documents PowerShell scripts; this development machine is macOS
  (see CLAUDE.md). There is no single page saying: here's how you build, here's how you run
  the tests, here's how you launch the real app, on *your* OS — including which targets
  simply cannot be built on which host, and why (`SNAPBACK_BUILD_APP` defaults OFF, ONNX
  needs a vendored runtime, Windows-only files can't compile on macOS). Would have saved
  several sessions' worth of rediscovery.

---

## Tier 13 — Model lifecycle (breaking down 2.3)

**2.3 is currently one `L` item that hides at least six.** "Wire the trainer, produce a
`model.onnx`" is the easy half; everything about *operating* a model the user can't inspect
is unscoped. Splitting it now so 2.3 doesn't become a month-long branch.

- **13.1 — ONNX cannot compile off Windows at all.** `M`
  `onnx_model.cpp:31` calls `model_path.wstring().c_str()`, with a comment claiming it's
  "the portable way to pass a path on Windows." It's inside `#if defined(SNAPBACK_ONNX)` but
  has **no Windows guard** — and on POSIX `Ort::Session` takes `const char*`, so this is a
  compile error, not a runtime one. The only ONNX CI job is `onnx-windows`, so nothing would
  ever surface it.

  **This is the same blind spot as the `SNAPBACK_BUILD_APP` one** (Done archive, `c0cfc3f`):
  an opt-in flag that no job turns on for a given OS is an opt-in guarantee. So "ONNX is
  optional and cross-platform" is false in a way no test can currently catch. Decide whether
  ONNX is Windows-only by design (then say so, and 3.3/3.4 ship heuristic-only), or fix the
  overload and add a POSIX ONNX job.

- **13.2 — A deployed model has no identity.** `S`
  `OnnxModel` tracks only `model_path_`. There is no version, no training-run id, no input
  hash, no record of which feature-vector layout it expects. So: you cannot tell which model
  produced a given prediction row; you cannot detect that a model was trained against an
  older feature order (which CLAUDE.md calls a contract); and "model info panel" in 2.3 has
  nothing to display. Stamp predictions with a model id. Prerequisite for everything else in
  this tier.

- **13.3 — Nothing stops a worse model from being deployed.** `M`
  `train_from_export` parses `metrics.json` but no gate consumes it. A retraining run that
  produces a *less* accurate model deploys exactly like a good one, and the user's experience
  silently degrades with no signal. Needs a held-out evaluation and a threshold: refuse to
  deploy below baseline, and say why. **This is the item that makes on-device personalization
  safe rather than a coin flip.**

- **13.4 — No rollback.** `S`
  Once `model.onnx` is replaced there is no previous version to return to. Keep the prior
  model and expose a revert. Cheap once 13.2 exists; near-impossible without it.

- **13.5 — Is there enough labelled data to train on at all?** `S` `decision`
  Unexamined. Labels come from explicit user submissions plus auto-labels at session end —
  and per **7.5** the auto-label path is skipped for any session not stopped through the UI,
  so the corpus is both small and biased. Before building the loop, measure: how many labels
  does a typical week produce, and what's the class balance? If the answer is "40 labels,
  90% PRODUCTIVE," personalization is premature and 2.3 should be rescoped to *collecting*
  data well rather than training on it.

- **13.6 — Define what happens when the model and the heuristic disagree.** `S` `decision`
  5.1 established that the classifier blends model probabilities with rule/thrash/drift
  signals. Nobody has specified what *should* win, or how to tell when the model has drifted
  far enough from the heuristic to be distrusted. Relates directly to **7.7** (score vs
  state) and **5.3** (confidence gating) — arguably the same decision session.

---

## Additions to existing tiers

- **9.10 — Retention deletes the data analytics depends on, and the user has no say.** `S`
  `decision`
  The 90-day prune is hardcoded (`storage.cpp:245`) with no setting exposed. Two tensions
  nobody has resolved: a user who wants year-over-year trends silently can't have them, and
  a privacy-focused user who wants a 7-day window can't have that either. The value
  proposition ("see your focus patterns") and the privacy promise ("we don't keep it
  forever") point opposite directions, and the constant currently arbitrates. Make it a
  setting, and decide the default deliberately. Ties to **7.6** and **8.5**.

- **11.6 — Lock ordering is documented but not enforced.** `S`
  `state.hpp:161` states the invariant: *always acquire `mutex_` before `storage_mutex_`,
  never the reverse.* Nothing enforces it — no wrapper type, no runtime assertion, no test.
  It holds today because a careful author held it, and the codebase now has three mixed-lock
  methods plus every IPC command. TSan catches an *actual* inversion only if a test happens
  to exercise both orders concurrently. A debug-only lock-order assertion is a few lines and
  converts a comment into a guarantee. *C++/Rust delta: Rust wouldn't have prevented this
  either — but it's exactly the class of invariant worth making mechanical.*

- **12.5 — The operational scripts are unrunnable on the dev machine.** `S`
  Seven of eight files in `scripts/` are PowerShell (`test_local.ps1`, `run_benchmarks.ps1`,
  `package_windows.ps1`, …); the only portable one is `run_feature_parity_dual.py`. The
  development host is macOS. So the documented way to run the local test suite doesn't run
  here, which is a standing tax on every session and a likely reason CI is where problems get
  discovered. Either port the non-Windows-specific ones to `sh`/Python, or document the
  direct `cmake`/`ctest` invocations as the primary path and mark the `.ps1` files as
  Windows packaging helpers. Folds naturally into **12.4**.

---

## Recurring health checks

Checks to run on a cadence, not one-off tasks. Several are automatable; where so, that's
itself a backlog item below.

### Before every release

- [ ] Open a **pre-existing** `focoflow.db` (Rust-authored and prior-C++-authored) and run a
      full session end-to-end. *Blocked on the 7.11 fixtures.*
- [ ] Kill the process uncleanly mid-session, restart, confirm WAL recovery and that the
      orphaned `ACTIVE` session resumes (`state.cpp:158` claims to handle this — verify it).
- [ ] Run a session on each OS long enough to exceed the ring buffer under load, and confirm
      `capture_events_dropped` reflects reality. *Blocked on 7.4.*
- [ ] Confirm every `invoke(...)` string in `frontend/src/api.ts` resolves in
      `commands.hpp`. `test_ipc_contract` covers the C++ side; confirm it covers the TS side
      too. CLAUDE.md calls this contract sacred and a mismatch fails silently at runtime.
- [ ] Feed a window title containing invalid UTF-8, U+2028, quotes, and backslashes through
      the full pipeline. Covers 8.1 and 8.2 in one test.

### Monthly, or when a subsystem is touched

- [ ] **Ghost-item sweep.** For each item claiming something is missing, grep first —
      including non-`.cpp` extensions. For each Done-archive item, confirm the code has a
      **caller**. This has found real ghosts twice (0.3, 2.4); assume it will again.
- [ ] **Dead-code sweep.** Every `.hpp` in `src/` should have a caller outside its own test.
      `confidence.hpp` is the known offender (5.3); check for siblings.
- [ ] **Unit sanity sweep.** Grep thresholds and confirm each matches its producer's scale.
      5.3 shipped `[0,100]` logic against a `[0,1]` producer and the tests passed because they
      fed values the system never emits.
- [ ] **Default-build coverage.** Confirm what sits behind `SNAPBACK_ONNX` /
      `SNAPBACK_BUILD_APP` and is therefore unexercised by the default build. The 5.2 fix
      lives inside an `#if` only one CI job compiles — a standing risk, not a one-time note.
- [ ] **Stack-size sweep.** Grep for large by-value members (6.1). Anything over ~64 KB per
      object is a Windows landmine.
- [ ] Re-run `scripts/run_feature_parity_dual.py` and diff. Any `features.cpp` change without
      a matching Rust change is a CI failure waiting to happen (5.6).

### Candidates for new CI jobs

- [ ] **Schema-drift job:** diff the C++ `CREATE TABLE` statements against the Rust ones and
      fail on divergence. Guards the 7.3 compatibility promise directly.
- [ ] **Scale job:** seed a month of synthetic usage; assert analytics/summary return correct
      counts inside a time budget. Would have caught 7.1; guards 7.12.
- [ ] **Health-truthfulness job:** force each failure mode (dead hook, over-broad exclusion,
      no session) and assert `HealthStatus` reports something other than healthy. *Blocked on
      7.4 and 7.10.* The point is that health fields must never be literals again.
- [ ] **Stack-size assertion:** `static_assert(sizeof(AppState) < N)`. One line, permanently
      prevents 6.1's class of regression.
- [ ] **Dead-header job:** automate the dead-code sweep above. It's the check that would have
      caught 2.4 for free.

---

## Done archive

Completed work. Kept for history; details live in git log and
[PORT_HISTORY.md](PORT_HISTORY.md).

### Tier 5 audit fixes (2026-07-20)

- **5.1 — ONNX inference discarded user rules, thrash, and drift** (`912b01c`) — fixed by
  moving the layering boundary: `OnnxModel::infer_probabilities` returns raw class
  probabilities and the classifier combines them with `compute_context_signals` via
  `blend_model_output`. Both are free functions **specifically so the logic is testable
  without `SNAPBACK_ONNX` compiled** — the bug was invisible because it lived inside the
  ONNX-only branch. The two `predict` overloads now delegate rather than duplicate, which is
  how they drifted apart originally. **Unblocked 2.3.**
- **5.2 — ONNX failure wrote an empty `focus_state`** (`7d4e6f3`) — `OnnxModel::run` returns
  `std::optional`; both `predict` overloads fall back to `predict_heuristic` on `nullopt`. The
  fix lives inside `#if defined(SNAPBACK_ONNX)`, **not compiled in the default build** — only
  CI's `onnx-windows` job exercises it. The invariant test runs everywhere.
- **5.7 — `Storage::open` swallowed every failure into `nullopt`** (`2bd03c7`) — outer handler
  logs at Error with path and `what()`; the `sqlite3_open` branch reports `sqlite3_errmsg`.
  Guarded by a test that forces a real failure and asserts the reason reaches the logger.
- **5.8 — `std::system` exit-code check was dead on POSIX** (`8745ba1`) —
  `detail::normalized_exit_code` unwraps the wait status via `WEXITSTATUS` (signals map to
  `128 + signo`). `train_from_export` itself still has no test — it shells out to Python.
- **5.9 — CSV export never checked for write failure** (`73370b8`) — both blocks `flush()` and
  re-check the stream, throwing rather than reporting a `feature_count` for rows that never
  reached disk.

### Platform & audit fixes

- **P0 — The desktop app didn't link off Windows** (`c0cfc3f`) — `Overlay::instance()` and
  `Tray::instance()` existed only in the two `*_windows.cpp` files, which CMake added only
  under `if(WIN32)`, while `main.cpp` called them unconditionally. Both headers promised a
  no-op fallback "so the build stays green cross-platform"; it was never written. Root cause:
  **no CI job ever set `SNAPBACK_BUILD_APP=ON`**, so the real binary had never been linked by
  CI on any OS. Fixed with stubs plus a `desktop-app-build` job — *which 6.3 shows is now
  being skipped.*
- **P1 — macOS capture fixed** (`cc8bf15`, `0bc8242`) — re-arm the tap after
  `kCGEventTapDisabledByTimeout`, move `query_active_window()` off the callback into a 500 ms
  cache, stop the hook thread's run loop instead of the caller's. Plus the first capture-layer
  tests and a double-start guard. Live verification still open — 0.3.
- **Feature-vector session time** (`8e2e50f`) — all three production callers of
  `reset_for_session` passed `nullopt`, so `seconds_since_session_start` was **0.0 in every
  row ever written and every training CSV exported**. Every extractor test used an explicit
  origin, so the suite stayed green. Fixed with `begin_session()`. *The canonical example of
  "tests passing ≠ the production path runs" — see 7.1 for the same shape.*
- **Command injection in the training path** (`b5c4b1c`) — the repo path reached
  `std::system` double-quoted with only `"` escaped, so a directory literally named `$(...)`
  executed. Now single-quoted on POSIX. *Windows quoting remains un-fuzzed — see 4.2.*
- **`feature_snapshots` retention** (`1c94f8f`) — the highest-volume table (one row per tick,
  31 REAL columns) was excluded from the prune entirely and grew unbounded. Needed a numeric
  cutoff, not `datetime()`: that column is REAL epoch seconds. *That inconsistency is part of
  what 7.16 has to settle.*
- **Hot-path indexes** (`2d1290c`) — `latest_prediction()`, `active_session()`, and
  `list_context_snapshots()` were all doing a full `SCAN` plus a temp B-tree sort. Verified
  with `EXPLAIN QUERY PLAN` before and after; guarded by a test asserting no query needs a
  temp B-tree. *Two tables still lack them — 7.13.*
- **macOS Accessibility permission prompt** (`c2a669d`) — `check_capture_permissions` used
  `AXIsProcessTrustedWithOptions(nullptr)`, which checks *without* prompting, and nothing
  could raise the OS dialog. Added `request_capture_permissions()`, a `request_permissions`
  IPC command, and a "Grant access" button — kept separate from the pollable refresh path so a
  timer can never spam dialogs.

### Features

- **0.1 — Feature-parity fixture harness + dual-language CI** — `feature-parity` job replays
  shared JSON scenarios through both extractors.
- **0.2 — Storage retention prune on open** — 90-day prune with conditional VACUUM. Extended
  2026-07-20 to cover `feature_snapshots`.
- **0.4 — Signed Windows installer (CI wiring)** — signing path wired in `release.yml`; only
  the cert remains (0.4b).
- **1.1 — First-run onboarding / permissions wizard** — explained local-only capture,
  requested permissions, plus a "Default focus mode" picker. *See 7.8 — that picker's answer
  is currently overwritten by `set_focus_mode`.*
- **1.3 — Start-on-login / autostart** — Windows HKCU Run-key registration, IPC, settings
  toggle, round-trip tests. launchd/systemd are 3.0.
- **1.4 — Native notifications** — Win32 toast + payload builders, wired into the real
  `snapback` event via `build_snapback_notification()`.
- **1.5 — Idle / AFK detection** — detector state machine wired into the engine tick;
  predictions freeze while AFK.
- **1.6 — Privacy controls** — local-only statement, global private mode, per-app exclusions,
  persistence, suppression tests, frontend controls. *Matching is substring-based — 7.9.*
- **2.1 — Analytics / trends dashboard** — hourly aggregates, top context apps,
  productive-session streaks, IPC, frontend views. *Windowing is capped and UTC-bucketed —
  7.1, 7.2.*
- **2.2 — Daily / weekly summary report** — windowed aggregates, distraction and streak
  metrics, JSON export, IPC, frontend controls. *Same caps — 7.1.*
- **2.4 — Confidence calibration (gating)** — ⚠️ **this claim is disputed; see 5.3.** The code
  has no callers and its threshold can't fire against the classifier's output.
- **2.5 — Goal-alignment coverage** — editable persisted goal categories and keywords wired
  through classifier, tracker, IPC, frontend.
- **2.6 — Pomodoro** — timer state machine in AppState + engine tick, `pomodoro` events, IPC,
  `PomodoroCard` + `usePomodoro` hook. Backend and UI.
- **4.1 — Structured logging** — leveled logger + rotating file sink, adopted in `storage.cpp`
  and `state.cpp`, real file sink with stderr fallback in `main.cpp`.
- **4.6 — Dependabot** — `.github/dependabot.yml` for Actions + npm. *Doesn't cover C++ deps —
  8.6.*
- **4.7 — Security-audit CI job** — frontend `npm audit` gate.
- **4.8 — Wired `dismiss_snapback`, and fixed a real bug it exposed** — not just an unused
  command: `ContextTracker::dismiss_recovery()` is the *only* exit from `Recovering`, and
  nothing called it from any UI — so on every platform the tracker got stuck after the first
  snapback of a session and silently never fired a second one. Fixed by routing both native
  dismiss triggers through `Overlay::dismiss()` with a callback into
  `AppState::dismiss_snapback()`, plus a frontend "Dismiss" button. Added the first test for
  this path.
- **4.9 — Fixed the duplicate-library link warning** — dropped redundant
  `snapback_core`/`snapback_capture`/`sqlite3` entries that `snapback_app` already re-exports
  `PUBLIC`ly.
- **4.10 — In-app diagnostics/health view** — health snapshot plus bounded logger tail,
  diagnostics IPC, mapper, refreshable panel, tests. *Two of its health fields are hardcoded
  literals — 7.4.*
- **0.8 — `focus_summary` over IPC + UI** — `get_focus_summary` command, frontend
  mapper/hook, `FocusSummaryCard` tile row.
