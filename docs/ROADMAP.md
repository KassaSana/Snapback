# ROADMAP.md — the single source of truth for what to build next

**This file is the live backlog.** Every open task lives here — feature work, wiring
gaps, chores. If it's not in this file, it's not planned; if it's done, it moves to the
[Done archive](#done-archive) at the bottom. `CLAUDE.md`'s status table is a summary
that points here — when they disagree, this file wins.

**Last synced against the code: 2026-07-20** (completed five product features in one pass:
privacy controls, analytics, summary reports, editable goal categories, and diagnostics;
then closed out Tier 0's four wiring gaps same day: logger adoption, native toast,
focus-summary IPC+UI, Pomodoro UI. Same day, a platform + engine/storage audit landed six
more fixes and opened **Tier 5** with what it found. 152 C++ tests and 38 frontend tests
green.)

**A note on trusting this file.** That audit found three items here that were simply wrong:
0.3 described work that was already written (and broken), 2.4 sits in the Done archive on
the strength of code that never runs (see 5.3), and the "Rust ref" path pointed at a
directory that doesn't exist on this machine. When an item claims something is missing,
check whether it's actually missing before rebuilding it.

The Rust→C++ port itself is done; the phase-by-phase playbook is archived in
[PORT_HISTORY.md](PORT_HISTORY.md).

## How to read an item

- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **Rust ref:** the file to consult when one exists — the original stays the behavioral
  source of truth. **It lives at `../FocoFlow-1/src-tauri/src/`**, not `../Snapback/`
  (same GitHub repo, different local directory name — CLAUDE.md's path is wrong). CI pulls
  the same tree as `KassaSana/Snapback` ref `main-fresh`.
- **C++/Rust delta:** called out when an item re-touches something Tauri/Rust gave us for
  free, since naming that gap is the whole point of this project.

Work each item on the standard loop: code → test → senior-to-junior explanation → commit
(terse one-liner, Kassa's identity, zero AI attribution). Claude commits; only Kassa pushes.

---

## Tier 0 — Finish the port's last gaps

Everything that was in "close the wiring gaps" (0.5–0.8) shipped on 2026-07-19 — see the
[Done archive](#done-archive). What's left in this tier:

- **0.3 — Native macOS capture: confirm it works on a real Mac.** `S`
  **This item was wrong until 2026-07-20.** It described the tap as unwritten; in fact
  `src/capture/input_hook_macos.mm` has had a full `CGEventTap` + `CFRunLoop` backend all
  along (missed by audits because it's the repo's only `.mm` file). It did not work: the
  callback shelled out to `osascript` per keystroke, blew the tap's deadline, and macOS
  disabled the tap without the code ever re-arming it — capture died silently within
  seconds while `capture_running` still reported `true`.
  Fixed in `cc8bf15` (re-arm + cached foreground window + correct run-loop stop) and the
  permission prompt landed in `c2a669d`.
  **All that's left is verification:** run the app on a Mac with Accessibility granted and
  confirm keystrokes keep reaching the engine under sustained mouse movement. No headless
  test can cover a live tap.
  *C++/Rust delta: Rust got global input from `rdev`; we hand-write the tap and run loop.*

- **0.4b — Provision the signing certificate.** `S` (external dependency)
  The `-SignCertificate` path is wired into `.github/workflows/release.yml` behind the
  `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT` secret; releases stay unsigned until an EV cert
  is purchased and the secret set. Document the cert-acquisition steps in
  [PACKAGING.md](PACKAGING.md) when doing this.

---

## Tier 1 — Ship a polished Windows-first v1

First-run experience, control, and respecting the user.

- **1.2 — Settings UI: distraction sensitivity tuning.** `M` (needs a product decision
  first — see below)
  App-rule management (`RulesCard`) and default focus mode (session control + onboarding
  wizard) are **done** — see Done archive. What's left is a real gap, not a wiring one:
  there is no user-facing "sensitivity" concept in the backend at all today.
  `risk_threshold(mode)` in `classifier.cpp` is a hardcoded function of `FocusMode`
  (deep/normal/recovery already *are* the sensitivity levers). Exposing a further
  per-user tunable requires deciding what it means first — a scalar multiplier on
  `risk_threshold`? A per-mode override stored in `AppSettings`? Something else? Don't
  build a UI for this until that's decided.

---

## Tier 2 — Product & ML depth

Make the insights worth coming back for, and close the training loop.

- **2.3 — Model retraining loop.** `L`
  Wire the `ml/` trainer to consume the exported CSV + the user's own labels → produce a
  fresh `model.onnx`; add model versioning and a "model info" panel. Opens the door to
  on-device personalization. *Rust ref: `ml/`, `engine/onnx_model.rs`.*

---

## Tier 3 — Cross-platform breadth & packaging

Everything Windows has, on the other two OSes — plus real installers.

- **3.0 — Autostart on macOS and Linux.** `M`
  `autostart.cpp:77-81` is a hard-coded `return false` off Windows, so the settings toggle
  correctly greys out — the feature is simply absent. Needs a launchd `LaunchAgent` plist
  (`~/Library/LaunchAgents/`) on macOS and a systemd **user** unit on Linux. This is the
  concrete scope of the follow-up noted on 1.3.

- **3.1 — macOS tray + native overlay.** `M`
  Replaces the no-op stub in `src/snapback/overlay_stub.cpp` / `src/app/tray_stub.cpp`.
  `NSStatusItem` tray menu and a native always-on-top overlay panel, matching the Windows
  behavior. *C++/Rust delta: Tauri's cross-platform tray/window, re-solved per-OS.*

- **3.2 — Linux tray + overlay.** `M`
  Same stubs as 3.1.
  `libappindicator` tray + an overlay window (X11/Wayland caveats noted).

- **3.3 — macOS packaging.** `L`
  `.app` bundle + notarization + DMG.

- **3.4 — Linux packaging.** `M`
  AppImage and/or Flatpak.

- **3.5 — In-app "check for updates".** `M`
  Fetch a version manifest and offer a download link — no silent install. The lightweight
  variant of the auto-updater deferred in [PACKAGING.md](PACKAGING.md).

---

## Tier 5 — Open findings from the 2026-07-20 engine/storage audit

A full audit of the engine, storage, and ONNX paths on 2026-07-20 found ten issues. Four
are fixed (see the Done archive: feature-vector session time, command injection,
feature-snapshot retention, hot-path indexes). These are the ones left. Each cites the code
it came from so nothing has to be re-derived.

- **5.1 — ONNX inference discards user rules, thrash, and drift.** `M`
  `classifier.cpp:90-108` calls `apply_focus_guardrails(..., 0.0, 0.0, false, mode)` when a
  model is loaded — `thrash`, `drift`, and `personal_block` are hardcoded, and
  `session_goal` / `rules` / `categories` are dropped entirely. So **deploying a trained
  model silently disables the user's Block app rules** (`personal_block` is the strongest
  guardrail signal) and pins `goal_alignment` to 0.5. `tests/test_onnx.cpp` only covers
  load/run/fallback, so CI can't see it. Blocks 2.3 — retraining is not safe to ship until
  a deployed model respects user configuration.

- **5.2 — ONNX failure writes an empty `focus_state`.** `S`
  `onnx_model.cpp:53` and `:87` `return {}` on failure, and a default `PredictionScores`
  has `focus_state == ""` (`classifier.hpp:20`). That empty string reaches the
  `focus_state TEXT NOT NULL` column, which happily accepts it; `recap()`'s
  `CASE WHEN focus_state = 'DEEP_FOCUS'` then silently excludes those rows. `predict` can't
  currently tell failure from a real prediction — `run()` should return
  `std::optional<PredictionScores>` so the caller can fall back to `predict_heuristic`.

- **5.3 — `confidence.hpp` is dead code with inverted units.** `S`
  Nothing outside its own test calls `should_nag` or `distraction_confidence`. Worse, the
  header documents risk as `[0,100]` and sets `nag_threshold = 60.0`, but the classifier
  emits `[0,1]` (`classifier.cpp:70`) — so `should_nag(0.9)` returns **false**. The tests
  pass only because they feed 0–100 values the system never produces. **Decide: wire it in
  on a `[0,1]` scale, or delete it and drop the 2.4 "confidence gating" claim** — which is
  currently in the Done archive on the strength of code that never runs.

- **5.4 — What should `thrash_spikes` measure?** `S` — **needs a product decision, not a fix**

  `recap()` counts `distraction_risk >= 0.7 AND focus_state = 'DISTRACTED'`. The audit
  called the constant a bug (the mode's threshold is 0.55/0.70/0.85); **it isn't, or at
  least not obviously.** Two things checked on 2026-07-20 before attempting a change:

  1. The Rust original hardcodes the identical 0.7 (`storage/mod.rs:633`), so this is a
     faithful port, not drift.
  2. There's a coherent reading where 0.7 is an *absolute* "strong distraction" bar,
     deliberately mode-independent so Deep mode's higher sensitivity doesn't inflate
     session-quality metrics — which is arguably what you want for comparable auto-labels.

  Switching to `risk_threshold(mode)` was tried and reverted: it breaks the existing recap
  test, and it *increases* mode-dependence for Deep rather than removing it.

  The real residual oddity is different: `apply_focus_guardrails` (`classifier.cpp:167`)
  also marks rows `DISTRACTED` via `thrash >= 0.75` or a `personal_block` app rule, with no
  risk floor. So a blocked-app row at risk 0.3 is `DISTRACTED` but never a "spike", while
  Recovery rows between 0.7 and 0.85 are never `DISTRACTED` and so never counted either.
  **Decide what the metric means** — absolute intensity, mode-relative alerting, or simply
  `COUNT(*) WHERE focus_state = 'DISTRACTED'` — then make the query say it.

- **5.5 — Retention silently no-ops on unparseable timestamps.** `S`
  `storage.cpp:1004`/`:1011` use `datetime(timestamp) < datetime(?1)`. If `datetime()` can't
  parse a stored value it yields `NULL`, the comparison is `NULL`, and the row is **never
  deleted** — retention degrades with nothing surfaced. Wrapping the column in `datetime()`
  also defeats the new `idx_predictions_ts`, forcing a scan on every startup prune. Store
  and compare a canonical format instead.

- **5.6 — `longest_active_stretch_5min` reports a full 300s for brand-new sessions.** `S`
  `features.cpp:190` defaults to the whole window when no idle events are present, so ten
  seconds in, the extractor claims a five-minute unbroken active stretch — inflating the
  deep-focus signal exactly when there's least evidence. Bound it by elapsed session time.

- **5.7 — `Storage::open` swallows every failure into `nullopt`.** `S`
  `storage.cpp:345`'s bare `catch (...)` makes a corrupt DB, a permissions error, and a
  full disk indistinguishable. The function already takes a `Logger*`; log `err.what()`.
  The inner prune/vacuum handlers already do this correctly — only the outer one is blind.

- **5.8 — `std::system` exit-code check is dead on POSIX.** `S`
  `training_deploy.cpp:306` treats the return of `std::system` as an exit code, but POSIX
  returns a wait status — exit 2 arrives as 512, so the `exit_code == 2` branch
  (`:174`) never fires and the "capture more labeled sessions" guidance is lost. Needs
  `WEXITSTATUS`. (`== 0` is coincidentally correct.) `train_from_export` has no test.

- **5.9 — CSV export never checks for write failure.** `S`
  `storage.cpp:915`/`:957` check the stream only at open. On a full disk the export returns
  a success result whose `feature_count` disagrees with the truncated file.

---

## Tier 4 — Engineering quality & hardening (cross-cutting)

Pull any of these in anytime; they pay for themselves as the surface grows.

- **4.11 — `title_parser` mislabels non-editor windows.** `M`
  `title_parser.cpp:9` says it outright: *"a faithful port sketch; extend to match
  title_parser.rs."* It splits on `" — "` / `" - "` and treats segment 0 as a filename with
  no check that it looks like one. So `"Some Article - Google Chrome"` yields
  `file_hint = "Some Article"`, and `tracker.cpp:104` turns that into **"Editing Some
  Article"** — a YouTube video reported as a file you were editing, in the product's
  namesake feature.
  **Needs a decision first:** the Rust original has the *same* bug
  (`title_parser.rs:35`), so a faithful port will not fix it — fixing means deliberately
  diverging from the source of truth. Cheapest fix is to consult `title_is_distracting`,
  which `app_context.cpp:125` already computes and `make_snapshot` ignores.
  *Rust ref: `title_parser.rs` — and note it takes `app_name`, which the C++ signature
  doesn't, so per-app title conventions are currently unimplementable.*

- **4.2 — Fuzz the untrusted boundaries.** `M`
  libFuzzer targets for `title_parser` and the JSON IPC arg parsing — the two places
  attacker-influenced strings enter the core. *C++/Rust delta: Rust bounds-checks slices
  for free; our manual index math in the parser is exactly what fuzzing should hammer.*

- **4.3 — Opt-in crash reporting.** `M`
  Windows minidump capture on unhandled exceptions, written locally, opt-in only.

- **4.4 — Perf regression gate.** `M`
  Profile `engine_tick` allocations (the compute path already defers work — measure it),
  then add a threshold to the benchmark harness so a regression fails CI. *See
  [benchmarking.md](benchmarking.md).*

- **4.5 — Schema versioning + optional encryption.** `M`
  An explicit `schema_version` table with ordered migrations, and optional SQLCipher
  encryption-at-rest for the local DB.

---

## Suggested near-term sequence

Highest value first, given what the audit turned up:
**5.2 → 5.4 → 5.3 → 0.3 (verify on a Mac) → 5.1**. The Tier 5 items are mostly `S` and
several corrupt data that feeds model training, so they pay off before new features. 5.1
blocks 2.3. 4.11 and 5.3 both need a product decision before code.

### Original sequence (superseded, kept for context)

Default order if you don't want to pick freely:
**1.2 → 0.3 → 2.3** — product sensitivity tuning needs a decision first; native macOS capture
and model retraining are the next larger workstreams.

---

## Done archive

Completed since the roadmap was drawn up (verified against code 2026-07-19). Kept for
history; details live in git log and [PORT_HISTORY.md](PORT_HISTORY.md).

- **0.1 — Feature-parity fixture harness + dual-language CI** — `feature-parity` job in
  `ci.yml` replays shared JSON scenarios through both extractors
  (`scripts/run_feature_parity_dual.py`).
- **0.2 — Storage retention prune on open** — 90-day prune of `predictions` +
  `context_snapshots` with conditional VACUUM (`storage.cpp`). Extended 2026-07-20 to
  cover `feature_snapshots` (see below).
- **P0 — The desktop app didn't link off Windows** (`c0cfc3f`) — `Overlay::instance()` and
  `Tray::instance()` existed only in the two `*_windows.cpp` files, which CMake added only
  under `if(WIN32)`, while `main.cpp` called them unconditionally. Both headers promised a
  no-op fallback "so the build stays green cross-platform"; it was never written. Root
  cause of the blind spot: **no CI job ever set `SNAPBACK_BUILD_APP=ON`**, so the real
  binary had never been linked by CI on any OS. Fixed with the stubs plus a
  `desktop-app-build` job (macOS + Linux, build-only).
- **P1 — macOS capture fixed** (`cc8bf15`, `0bc8242`) — re-arm the tap after
  `kCGEventTapDisabledByTimeout`, move `query_active_window()` off the callback into a
  500ms cache, and stop the hook thread's run loop instead of the caller's. Plus the first
  tests for the capture layer (`test_capture_thread.cpp`) and a double-start guard.
  Live-on-a-Mac verification is still open — see 0.3.
- **Audit fix — feature-vector session time** (`8e2e50f`) — all three production callers of
  `reset_for_session` passed `nullopt`, so `seconds_since_session_start` was **0.0 in every
  row ever written and every training CSV exported**. Every extractor test used an explicit
  origin, so the suite stayed green. Fixed with `begin_session()`, which seeds the origin
  from the session's first event (AppState can't supply one: `started_at` is wall-clock,
  event timestamps are monotonic-since-launch). Guarded by an end-to-end export assertion.
- **Audit fix — command injection in the training path** (`b5c4b1c`) — the repo path
  reached `std::system` wrapped in double quotes with only `"` escaped, so a directory
  literally named `$(...)` (which passes the existence check, since the filesystem treats
  the name as literal) executed. Now single-quoted on POSIX, which suppresses all
  expansion. Source is `SNAPBACK_REPO` or `training_repo.txt` — both user-writable.
- **Audit fix — `feature_snapshots` retention** (`1c94f8f`) — the highest-volume table in
  the schema (one row per tick, 31 REAL columns) was excluded from the prune entirely and
  grew without bound while the other tables stayed flat at the 90-day window. Needed a
  numeric cutoff, not `datetime()`: that column is REAL epoch seconds.
- **Audit fix — hot-path indexes** (`2d1290c`) — `latest_prediction()`, `active_session()`,
  and `list_context_snapshots()` were all doing a full `SCAN` plus a temp B-tree sort.
  `idx_predictions_session_ts` couldn't serve the first two because SQLite can't skip a
  leading index column. Verified with `EXPLAIN QUERY PLAN` before and after, and guarded by
  a test that asserts no query needs a temp B-tree.
- **macOS Accessibility permission prompt** (`c2a669d`) — `check_capture_permissions` used
  `AXIsProcessTrustedWithOptions(nullptr)`, which checks *without* prompting, and nothing
  anywhere could raise the OS dialog. Added `request_capture_permissions()` with the prompt
  option, a `request_permissions` IPC command, and a "Grant access" button in the wizard
  and Permissions card — kept separate from the pollable refresh path so a timer can never
  spam dialogs.
- **0.4 — Signed Windows installer (CI wiring)** — signing path wired in `release.yml`;
  only the cert itself remains (see 0.4b).
- **1.4 — Native notifications** — Win32 toast + payload builders, wired into the real
  `snapback` recovery event via `build_snapback_notification()` (`main.cpp`), tested.
- **1.5 — Idle / AFK detection** — detector state machine wired into the engine tick;
  predictions freeze while AFK.
- **2.4 — Confidence calibration (gating)** — low-confidence distraction calls are gated
  (`test_confidence.cpp`).
- **2.6 — Pomodoro** — timer state machine wired into AppState + engine tick, emits
  `pomodoro` events, IPC commands registered, and a frontend `PomodoroCard` + `usePomodoro`
  hook drive start/stop and live countdown. Fully done, backend and UI.
- **4.1 — Structured logging** — leveled logger + rotating file sink, adopted in
  `storage.cpp` and `state.cpp` (main.cpp wires a real file sink with stderr fallback),
  tested. In-app diagnostics view split out separately (see 4.10).
- **4.6 — Dependabot** — `.github/dependabot.yml` for Actions + npm.
- **4.7 — Security-audit CI job** — frontend `npm audit` gate in CI.
- **0.8 (ex-Tier-0) — `focus_summary` over IPC + UI** — `get_focus_summary` command,
  frontend mapper/hook, and a `FocusSummaryCard` tile row on the dashboard. First slice of
  2.2 (daily/weekly report) is now unblocked.
- **4.8 — Wired `dismiss_snapback`, and fixed a real bug it exposed** — the audit found
  this wasn't just an unused command: `ContextTracker::dismiss_recovery()` is the *only*
  exit from its `Recovering` state, and nothing called it from any UI — so on every
  platform, the tracker got stuck after the first snapback of a session and silently
  never fired a second one. Fixed by routing both native dismiss triggers (Windows
  overlay auto-timeout + click, `overlay_windows.cpp`) through `Overlay::dismiss()` with a
  registered callback into `AppState::dismiss_snapback()` (mirrors `Tray::install`'s
  callback pattern), plus a frontend "Dismiss" button on the snapback note calling the
  same IPC command. Added the first-ever test for this path
  (`test_app_state.cpp`), which proves a second snapback now fires after dismiss.
  **Caveat:** the Windows-side native wiring (`overlay_windows.cpp`) could not be
  compiled or tested on this (macOS) machine — CI or a Windows run should confirm it.
- **4.9 — Fixed the duplicate-library link warning** — `snapback_tests`, `snapback`, and
  the benchmark targets were re-listing `snapback_core`/`snapback_capture`/`sqlite3`,
  which `snapback_app` already re-exports `PUBLIC`ly; dropped the redundant entries in
  `CMakeLists.txt`.
- **1.1 — First-run onboarding / permissions wizard** — the existing `PermissionWizard`
  already explained what's captured (local-only) and requested permissions; added the
  missing third piece, a "Default focus mode" picker in the wizard itself, reusing
  `useSession`'s existing `focusMode`/`handleFocusModeChange` (no new backend needed).
- **1.3 — Start-on-login / autostart** — Windows HKCU Run-key registration with a
  cross-platform support status, IPC commands, settings toggle, and round-trip tests.
- **1.6 — Privacy controls** — local-only statement, global private mode, per-app exclusions,
  persistence, suppression tests, and frontend controls.
- **2.1 — Analytics / trends dashboard** — hourly focus/distraction aggregates, top context
  apps, productive-session streaks, analytics IPC, and frontend trend views.
- **2.2 — Daily / weekly summary report** — windowed focus/session aggregates, distraction
  and streak metrics, JSON export, IPC, and frontend report controls.
- **2.5 — Goal-alignment coverage** — editable persisted goal categories and keywords wired
  through classifier, tracker, IPC, and frontend settings.
- **4.10 — In-app diagnostics/health view** — health snapshot plus bounded recent logger tail,
  diagnostics IPC, mapper, refreshable panel, and tests.
