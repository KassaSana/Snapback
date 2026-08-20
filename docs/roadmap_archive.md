# ROADMAP archive — completed work

Completed items lifted out of [`ROADMAP.md`](ROADMAP.md) so the live backlog loads and diffs
sanely. Kept for history; further detail lives in the git log. Nothing here describes the
current tree — entries are deliberately stale as of their completion date.


### Tier 12 docs (2026-07-23)

- **12.1 — `ARCHITECTURE.md`'s module map was the pre-port plan** — the map now matches the
  tree: every path in it was confirmed to exist, and divergences (per-OS file splits,
  `goal_alignment` folded into `app_context`) are called out in their own column rather than
  silently wrong. The Libraries table now lists what we *actually* depend on — four
  third-party libs — after confirming `spdlog` and `stduuid` were never taken and that
  UUID, logging, and time are hand-written. **Guarded by `scripts/check_doc_paths.py`,
  wired into the `docs-smoke` CI job**, which fails if any doc names a file that does not
  exist; verified by injecting a false path and watching it fail. That guard immediately
  found a real defect: an external path written as if it were ours. Surfaced **12.6**.

- **12.4 — A "how do I actually run this" doc, per OS** — [`docs/running.md`](running.md):
  a what-builds-where matrix, prerequisites, the headless build, the desktop app, the
  permissions real capture needs on each OS, the environment variables, and a
  symptom→cause table. **The macOS claims were verified by running them**, including a
  `SNAPBACK_BUILD_APP=ON` build that produced a linked arm64 binary — so the page reports
  what happened rather than what should happen.

  The section that will earn its keep is *what cannot be built where*: ONNX needs a vendored
  runtime that is not in this repo, tray and overlay are deliberate no-op stubs off Windows,
  and four Windows-only sources cannot compile on this host at all — which is why **red
  Windows CI means those four are covered nowhere**, not merely less well.

  > *Superseded 2026-07-28 by 3.1:* "off Windows" in the paragraph above now means Linux
  > only. macOS has real ones. The page itself was updated; this entry is left as written
  > because it records what 12.4 found at the time.

- **12.5 — The operational scripts were unrunnable on the dev machine** — the two scripts
  that were never Windows-specific are now ported: `test_local.sh` and `run_benchmarks.sh`.
  **Both were verified by running them on the macOS host**, not just written: the headless
  suite configures, builds, and passes CTest, and the benchmark replay produces numbers.
  `scripts/README.md` is new and states which of the eleven scripts run where, so the next
  session does not rediscover that five of them are MSVC/`signtool`/IExpress-bound and
  portable only in CI.

  The substantive difference, and why these are ports rather than translations: **MSVC is a
  multi-config generator and Make/Ninja are not.** The `.ps1` scripts pick the build type at
  `--build` time and look in `build/Release/`; the `.sh` scripts must pass
  `-DCMAKE_BUILD_TYPE` at *configure* time and find binaries directly in `build/`. Both
  output layouts are probed so the scripts work under either generator.

- **12.2 — Audit the remaining docs against the code** — ten false claims corrected, all
  stale in the same direction: they described the design *before* the 2026-07-22 passes.
  the former architecture summary (five: the single-mutex design, the inline
  `std::array` ring, the ~5 MB `AppState` whose fix had already shipped, and the parity
  check listed as future work when it runs on every push);
  `testing_strategy.md` (four: six CI jobs listed of twelve, macOS/Linux capture called
  "stubs" when both backends are real, NSIS listed as future work when `CMakeLists.txt:226`
  configures it, signing listed as future work when only the certificate is missing);
  `benchmarking.md` (one: perf deltas cross-referenced to a section that never held them —
  plus the CI benchmark jobs and the Windows-vs-macOS baseline caveat, neither documented).
  `PACKAGING.md` was accurate.

  **The audit found a live bug, which was the point:** `-UseVite` in the Windows demo has
  been silently dead since 8.4 landed — filed as **8.7**. A doc audit is a cheap way to
  find code defects, because a doc says what the code was *supposed* to do.

- **12.3 — Nowhere to record a decision** — created [`docs/adr/`](adr/README.md) with a
  one-page template, an index, and a table of the fourteen `decision`-tagged items still
  awaiting an ADR. [ADR-0001](adr/0001-record-architecture-decisions.md) records the
  practice itself and the rule that follows from it: **`decision` items are not
  implementable until their ADR is `Accepted`.** Files are append-only — a changed mind
  writes a new ADR and marks the old one `Superseded`, so a reversal is visible as an
  addition rather than a silent edit. That is the failure this fixes: 5.4 and 5.6 were both
  implemented and reverted because the rationale lived only in a chat log. Unblocks 9.1 and
  decision sessions A and B.

### Tier 13 model lifecycle (2026-07-23)

- **13.1 — Cross-platform ONNX Runtime build** — `OnnxModel` now selects the native path
  overload required by Windows or POSIX, CMake links and stages the matching `.lib`/`.dll`,
  `.dylib`, or `.so`, and a Linux ONNX CI job runs the fixture-backed inference tests. The
  optional ONNX build is now exercised on both Windows and POSIX rather than making an
  untested cross-platform claim.

- **13.2 — Deployed model identity** — prediction rows now carry a stable identity that
  distinguishes heuristic output from ONNX output, includes the 31-feature contract version,
  and hashes the model contents. The identity is exposed in classifier diagnostics and the
  training panel; legacy databases receive the new column with a heuristic default on open.

- **13.3 — Model quality gate** — training now refuses to deploy without a held-out/validation/
  cross-validation accuracy, enforces a minimum 0.60 score, and rejects candidates below the
  accepted model's recorded baseline. The decision and reason are returned to the training UI.

- **13.4 — Model rollback** — accepted deployments preserve the previous ONNX model and its
  quality metadata; the training panel exposes a reversible rollback command that reloads the
  restored model immediately.

### Tier 6 CI fixes (2026-07-22)

- **6.1 — Windows CTest stack overflow** (`a240e11`, CI-confirmed run `29890010902`) —
  `RingBuffer` held `std::array<T, 65536>` inline; at 96 bytes per `CaptureEvent` that made
  every `CaptureThread` (and `AppState`, which holds one by value) a ~6 MB object. Windows'
  1 MB default thread stack overflowed deterministically; Linux/macOS survived on 8 MB.
  Fixed by moving storage to `std::unique_ptr<T[]>` — one allocation at construction, hot
  path untouched. Reproduced first via `ulimit -s 1024` on macOS; guarded by a
  `static_assert(sizeof(CaptureThread) < 4096)` verified to fire when the bug is
  reintroduced. The fix un-skipped 138 test cases, which then exposed the X11 macro
  collision in the desktop build (see 6.3). *A `std::array` member is C++ silently choosing
  automatic storage for 6 MB — the trap that made this a stack overflow.*

- **6.4 — GitHub Actions off Node 20** (`07e898c`, CI-confirmed run `29890010902`) —
  `checkout` 4→7, `setup-node` 4→7, `upload-artifact` 4→7, `download-artifact` 4→8 across
  all four workflows, matching the blocked Dependabot PRs. `setup-python` 5→6 followed in
  the 6.3 commit after the run's annotations flagged it too (Dependabot never PR'd it).
  `action-gh-release` 2→3 (PR #19) deliberately deferred — third-party, release-path.

### Tier 8 reliability fixes (2026-07-22)

- **8.1 — Engine-thread exception boundary** — the background tick loop catches and logs
  standard and unknown exceptions instead of allowing `std::terminate` to take down the
  process. A headless injected-hook test verifies the thread remains online after a thrown
  emit callback.

### Tier 7 observability fixes (2026-07-22)

- **7.4 — Capture health** — returned hooks are reported as failed, event arrival age is
  tracked with a monotonic clock, and finished hook threads can be restarted safely. The
  existing diagnostics UI now receives truthful capture status and failure reasons.
- **7.10 — Prediction health** — diagnostics expose last-prediction age and distinguish idle,
  no-session, private-mode, and unsuppressed states.

### Tier 7 correctness fixes (2026-07-22)

- **7.5 — Automatic labels on shutdown** — the no-argument active-session stop path now saves
  the same recap-derived training label as the explicit session-id path.
- **7.13 — Session foreign-key indexes** — recap and label queries now have indexes on
  `snapback_events(session_id)` and `labels(session_id)`.

### Tier 8 security fixes (2026-07-22)

- **8.3 — Frontend Content Security Policy** — the bundled dashboard restricts fetched scripts
  to same-origin content while explicitly allowing its existing fonts, styles, and data images.

### Tier 7 correctness and release-readiness fixes (2026-07-22)

- **7.1 + 7.2 — Analytics windows and local-hour buckets** — removed the prediction row cap
  from reports, moved timestamp filtering into SQLite, and converted UTC timestamps to the
  machine's local hour before building chart buckets.
- **7.9 — Privacy exclusion boundaries** — exclusions match whole app-name words and warn on
  unusually broad one- and two-character entries.
- **8.4 — Release frontend URL gate** — debug overrides remain available, while release builds
  cannot be redirected by the launch environment and fail closed if the bundled UI is missing.
- **9.2 — Runtime version identity** — the CMake project version is compiled into diagnostics
  and displayed in the frontend.

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
  CI's `onnx-linux` job exercises it. The invariant test runs everywhere.
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
  is currently overwritten by `set_focus_mode`; 2.12 extends setup through first value.*
- **1.3 — Start-on-login / autostart** — Windows HKCU Run-key registration, IPC, settings
  toggle, round-trip tests. launchd/systemd are 3.0.
- **1.4 — Native notifications** — Win32 toast + payload builders, wired into the real
  `snapback` event via `build_snapback_notification()`.
- **1.5 — Idle / AFK detection** — detector state machine wired into the engine tick;
  predictions freeze while AFK.
- **1.6 — Privacy controls** — local-only statement, global private mode, per-app exclusions,
  persistence, suppression tests, frontend controls. *Matching was fixed in 7.9; live/disk
  atomicity and always-visible pause controls remain in 7.26/2.10.*
- **2.1 — Analytics / trends dashboard** — hourly aggregates, top context apps,
  productive-session streaks, IPC, frontend views. *The old row cap and UTC-bucketing defects
  closed in 7.1/7.2; remaining query, chart, and shared-range work is 7.12/10.8/10.11.*
- **2.2 — Daily / weekly summary report** — windowed aggregates, distraction and streak
  metrics, JSON export, IPC, frontend controls. *Remaining aggregation/range work is
  7.12/10.11.*
- **2.4 — Confidence calibration (gating)** — ❌ **RETRACTED 2026-08-03.** This was never
  delivered: the code had no callers and its `[0,100]` threshold could not fire against a
  `[0,1]` producer. `confidence.hpp` was deleted by
  [ADR-0004](adr/0004-verdict-and-opinion.md) rather than wired in, because the debounce it
  promised already exists in `ContextTracker`. **The entry stays here, struck, because a
  silently-deleted false claim teaches nothing** — this is the ghost item the Done-archive
  sweep was invented to catch (see 5.3).
- **2.5 — Goal-alignment coverage** — editable persisted goal categories and keywords wired
  through classifier, tracker, IPC, frontend.
- **2.6 — Pomodoro** — timer state machine in AppState + engine tick, `pomodoro` events, IPC,
  `PomodoroCard` + `usePomodoro` hook. Backend and UI. *Customization, pause/skip/restart,
  hidden-window status, and durable phase UX remain in 2.13.*
- **4.1 — Structured logging** — leveled logger + rotating file sink, adopted in `storage.cpp`
  and `state.cpp`, real file sink with stderr fallback in `main.cpp`.
- **4.6 — Dependabot** — **reverted 2026-08-18.** The config for Actions + npm was removed and
  Dependabot no longer opens PRs here. It never covered the C++ deps anyway (8.6), which is why
  `check_dependency_pins.py` and the weekly pin-freshness job (4.13) exist and stay. Its 29
  historical commits keep it on the `ALLOWED_AUTHORS` list in
  `scripts/check_commit_attribution.py`: that gate walks every ref, so dropping the entry would
  fail CI on history rather than clean it up.
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
