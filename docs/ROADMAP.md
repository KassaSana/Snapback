# ROADMAP.md — the single source of truth for what to build next

**This file is the live backlog.** Every open task lives here — feature work, wiring
gaps, chores. If it's not in this file, it's not planned; if it's done, it moves to the
[Done archive](#done-archive) at the bottom. `CLAUDE.md`'s status table is a summary
that points here — when they disagree, this file wins.

**Last synced against the code: 2026-07-19** (full-codebase health check: clean macOS
build, all tests green, IPC contract verified frontend↔backend, no stray TODOs).

The Rust→C++ port itself is done; the phase-by-phase playbook is archived in
[PORT_HISTORY.md](PORT_HISTORY.md).

## How to read an item

- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **Rust ref:** the file in `../Snapback/src-tauri/src/` to consult when one exists — the
  original stays the behavioral source of truth.
- **C++/Rust delta:** called out when an item re-touches something Tauri/Rust gave us for
  free, since naming that gap is the whole point of this project.

Work each item on the standard loop: code → test → senior-to-junior explanation → commit
(terse one-liner, Kassa's identity, zero AI attribution). Claude commits; only Kassa pushes.

---

## Tier 0 — Close the wiring gaps (highest priority)

The 2026-07 health check found a recurring pattern: subsystems landed backend-first with
tests, but the final "connect a consumer" commit never happened. These are all small and
they make already-written code actually do something. **A module isn't done until
something calls it.**

- **0.5 — Adopt the logger everywhere.** `S`
  `src/util/logger.hpp` (leveled + rotating file sink) is built and tested, but nothing
  uses it: `std::cerr` writes remain in `src/app/state.cpp` and `src/storage/storage.cpp`.
  Replace them with logger calls and pick sensible levels. Closes the loose end of 4.1.

- **0.6 — Fire the native toast on a real distraction.** `S`
  Win32 toast delivery + payload builders exist, but the only caller is the
  `SNAPBACK_NOTIFICATION_TEST` env hook in `main.cpp`. Wire
  `Tray::show_notification(build_distraction_notification(...))` into the real event path
  (the emit hook in `main.cpp` that already pops the overlay on `snapback`), ideally only
  when the app window isn't focused. Closes the loose end of 1.4.

- **0.7 — Pomodoro UI.** `S`/`M`
  Backend is fully wired: the engine tick polls the timer and emits `pomodoro` events, and
  `start_pomodoro` / `stop_pomodoro` / `get_pomodoro_status` are registered IPC commands —
  but the frontend never calls them and shows no timer. Add a small timer card:
  subscribe to the `pomodoro` event, call the three commands.

- **0.8 — Expose `focus_summary` over IPC + show it.** `S`
  `AppState::focus_summary()` (aggregation over recent predictions) exists with tests but
  has no IPC command and no UI. Add a `get_focus_summary` command + a frontend surface
  (e.g. extend InsightsCard). This is the first slice of 2.2 (daily/weekly report).

- **0.3 — Native macOS capture.** `L`
  Replace the polling fallback (`input_hook_posix.cpp`) with a real `CGEventTap` +
  `CFRunLoop` hook, plus the Accessibility / Input-Monitoring permission prompts.
  *C++/Rust delta: Rust got global input from `rdev`; we hand-write the tap and run loop,
  same as the Win32 hook and Linux evdev.*

- **0.4b — Provision the signing certificate.** `S` (external dependency)
  The `-SignCertificate` path is wired into `.github/workflows/release.yml` behind the
  `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT` secret; releases stay unsigned until an EV cert
  is purchased and the secret set. Document the cert-acquisition steps in
  [PACKAGING.md](PACKAGING.md) when doing this.

---

## Tier 1 — Ship a polished Windows-first v1

First-run experience, control, and respecting the user.

- **1.1 — First-run onboarding / permissions wizard.** `M`
  A short first-launch flow: explain exactly what's captured (and that it's local-only),
  request permissions, and pick a default focus mode. (A `PermissionWizard` component
  exists in the frontend — audit what it covers and extend, don't rebuild.)

- **1.2 — Settings UI.** `M`
  The backend half is **done**: `src/app/settings.{hpp,cpp}` persists `settings.json` in
  the app-data dir, and default focus mode already round-trips. Remaining: a settings
  screen surfacing app-rule management UX, distraction sensitivity/threshold tuning, and
  the default focus mode. *C++/Rust delta: Tauri had a config/plugin-store convention;
  here it's our own JSON-on-disk with explicit load/save.*

- **1.3 — Start-on-login / autostart.** `S`
  Register autostart via the Windows Run key (launchd/systemd variants later), toggleable
  from settings.

- **1.6 — Privacy controls.** `M`
  A per-app exclusion list (never record titles from e.g. banking/password managers), a
  global "private mode" pause, and a plain-language local-only data statement in-app.

---

## Tier 2 — Product & ML depth

Make the insights worth coming back for, and close the training loop.

- **2.1 — Analytics / trends dashboard.** `L`
  Focus over time, a distraction-by-hour heatmap, top distracting apps, and session
  streaks — storage aggregate queries feeding new frontend views. *Reach for the dataviz
  design guidance before building charts.*

- **2.2 — Daily / weekly summary report.** `M`
  An in-app recap of the day/week (focus time, biggest distractions, best streak), with an
  optional export. The `focus_summary` aggregation (0.8) is the first slice; this item is
  the day/week windowing + export on top of it.

- **2.3 — Model retraining loop.** `L`
  Wire the `ml/` trainer to consume the exported CSV + the user's own labels → produce a
  fresh `model.onnx`; add model versioning and a "model info" panel. Opens the door to
  on-device personalization. *Rust ref: `ml/`, `engine/onnx_model.rs`.*

- **2.5 — Goal-alignment coverage.** `M`
  Broaden the app-context keyword/category tables and let the user edit categories, so the
  "is this on-goal?" signal generalizes past the seed keyword lists. *Rust ref:
  `engine/app_context.rs`, `engine/goal_alignment.rs`.*

---

## Tier 3 — Cross-platform breadth & packaging

Everything Windows has, on the other two OSes — plus real installers.

- **3.1 — macOS tray + native overlay.** `M`
  `NSStatusItem` tray menu and a native always-on-top overlay panel, matching the Windows
  behavior. *C++/Rust delta: Tauri's cross-platform tray/window, re-solved per-OS.*

- **3.2 — Linux tray + overlay.** `M`
  `libappindicator` tray + an overlay window (X11/Wayland caveats noted).

- **3.3 — macOS packaging.** `L`
  `.app` bundle + notarization + DMG.

- **3.4 — Linux packaging.** `M`
  AppImage and/or Flatpak.

- **3.5 — In-app "check for updates".** `M`
  Fetch a version manifest and offer a download link — no silent install. The lightweight
  variant of the auto-updater deferred in [PACKAGING.md](PACKAGING.md).

---

## Tier 4 — Engineering quality & hardening (cross-cutting)

Pull any of these in anytime; they pay for themselves as the surface grows.

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

- **4.8 — Decide the fate of `dismiss_snapback`.** `S`
  The IPC command is registered in `commands.hpp` but nothing calls it — the native
  overlay self-dismisses (timeout/click) and the frontend never invokes it. Either wire a
  frontend dismiss affordance to it or delete it; an unused command on the sacred IPC
  contract is a trap for future readers.

- **4.9 — Fix duplicate-library link warning.** `S`
  `ld: warning: ignoring duplicate libraries: 'libsnapback_capture.a', 'libsnapback_core.a'`
  on the test binary — dedupe the CMake `target_link_libraries` lines.

- **4.10 — In-app diagnostics/health view.** `M`
  The deferred half of old 4.1: once the logger is adopted (0.5), surface recent log lines
  + health state in a diagnostics panel.

---

## Suggested near-term sequence

Default order if you don't want to pick freely:
**0.5 → 0.6 → 0.8 → 0.7 → 4.8 → 4.9 → 1.2 → 1.1** — clear every wiring gap first (each
is a sitting and makes existing tested code visible to the user), sweep the two chores,
then turn to the v1 experience. Big rocks (0.3 macOS capture, 2.1 analytics, 2.3
retraining) come once the small stuff is flushed.

---

## Done archive

Completed since the roadmap was drawn up (verified against code 2026-07-19). Kept for
history; details live in git log and [PORT_HISTORY.md](PORT_HISTORY.md).

- **0.1 — Feature-parity fixture harness + dual-language CI** — `feature-parity` job in
  `ci.yml` replays shared JSON scenarios through both extractors
  (`scripts/run_feature_parity_dual.py`).
- **0.2 — Storage retention prune on open** — 90-day prune of `predictions` +
  `context_snapshots` with conditional VACUUM (`storage.cpp`).
- **0.4 — Signed Windows installer (CI wiring)** — signing path wired in `release.yml`;
  only the cert itself remains (see 0.4b).
- **1.4 — Native notifications (delivery path)** — Win32 toast + payload builders +
  tests; real-event trigger remains (see 0.6).
- **1.5 — Idle / AFK detection** — detector state machine wired into the engine tick;
  predictions freeze while AFK.
- **2.4 — Confidence calibration (gating)** — low-confidence distraction calls are gated
  (`test_confidence.cpp`).
- **2.6 — Pomodoro (backend)** — timer state machine wired into AppState + engine tick,
  emits `pomodoro` events, IPC commands registered; UI remains (see 0.7).
- **4.1 — Structured logging (module)** — leveled logger + rotating file sink + tests;
  adoption remains (see 0.5), diagnostics view split out (see 4.10).
- **4.6 — Dependabot** — `.github/dependabot.yml` for Actions + npm.
- **4.7 — Security-audit CI job** — frontend `npm audit` gate in CI.
