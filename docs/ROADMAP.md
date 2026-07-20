# ROADMAP.md — the single source of truth for what to build next

**This file is the live backlog.** Every open task lives here — feature work, wiring
gaps, chores. If it's not in this file, it's not planned; if it's done, it moves to the
[Done archive](#done-archive) at the bottom. `CLAUDE.md`'s status table is a summary
that points here — when they disagree, this file wins.

**Last synced against the code: 2026-07-20** (completed five product features in one pass:
privacy controls, analytics, summary reports, editable goal categories, and diagnostics;
then closed out
Tier 0's four wiring gaps same day: logger adoption, native toast, focus-summary IPC+UI,
Pomodoro UI — all with tests, C++ + frontend suites green).

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

## Tier 0 — Finish the port's last gaps

Everything that was in "close the wiring gaps" (0.5–0.8) shipped on 2026-07-19 — see the
[Done archive](#done-archive). What's left in this tier:

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

---

## Suggested near-term sequence

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
  `context_snapshots` with conditional VACUUM (`storage.cpp`).
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
