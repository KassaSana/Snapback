# ROADMAP.md — what to build next

**The Rust→C++ port is done.** The pipeline runs end-to-end on Windows (capture → engine →
SQLite → webview IPC → reused React UI), ONNX is optional and verified, and CI guards it on
three OSes with ASan/UBSan/TSan. The phase-by-phase porting playbook that got us here is
archived in [PORT_HISTORY.md](PORT_HISTORY.md) as a teaching record.

This file is the **live backlog** — everything from here is new product and engineering work,
not porting. Items are grouped into tiers and roughly ordered, but tiers are **pull-from-any**:
grab whatever fits the moment.

## How to read an item

- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **Rust ref:** the file in `../Snapback/src-tauri/src/` to consult when one exists — the
  original stays the behavioral source of truth.
- **C++/Rust delta:** called out when an item re-touches something Tauri/Rust gave us for
  free, since naming that gap is the whole point of this project.

Work each item on the standard loop: code → test → senior-to-junior explanation → one-line
commit message. Never run git.

---

## Tier 0 — Finish the port (close the last parity gaps)

These are the few things still short of the "done" bar in
[PORT_HISTORY.md](PORT_HISTORY.md#definition-of-done-for-the-whole-rewrite). Do these first —
they're what makes the port *provably* faithful and the build *shippable*.

- **0.1 — Feature-parity fixture harness + dual-language CI.** `M`
  Replay `../Snapback/fixtures/feature_parity/scenarios.json` through both the Rust and C++
  feature extractors and assert the vectors match within epsilon, as a CI job. Today CI runs
  our `*parity*` doctests but never diffs against the live Rust code, so drift can slip
  through. *Rust ref: `engine/features.rs`, `ml/feature_parity_cli`.*

- **0.2 — Storage retention prune on open.** `S`
  Prune `predictions` + `context_snapshots` older than 90 days when the DB opens, matching the
  Rust policy, so the file doesn't grow unbounded. *Rust ref: `storage/mod.rs`.*

- **0.3 — Native macOS capture.** `L`
  Replace the polling fallback (`input_hook_posix.cpp`) with a real `CGEventTap` + `CFRunLoop`
  hook, plus the Accessibility / Input-Monitoring permission prompts. *C++/Rust delta: Rust got
  global input from `rdev`; we hand-write the tap and run loop, same as we did for the Win32
  hook and Linux evdev.*

- **0.4 — Signed Windows installer.** `S`
  Wire the existing `-SignCertificate` path in `scripts/package_windows.ps1` into
  `.github/workflows/release.yml` and document the EV-cert flow so SmartScreen stops warning on
  first run. *See [PACKAGING.md](PACKAGING.md).*

---

## Tier 1 — Ship a polished Windows-first v1

The app runs, but it isn't yet something you'd hand to a stranger. This tier is about
first-run experience, control, and respecting the user.

- **1.1 — First-run onboarding / permissions wizard.** `M`
  A short first-launch flow: explain exactly what's captured (and that it's local-only),
  request permissions, and pick a default focus mode.

- **1.2 — Settings UI + persisted config file.** `M`
  Move configuration off env-vars-only into a JSON config in the app-data dir, surfaced by a
  settings screen: app-rule management UX, distraction sensitivity/threshold tuning, default
  focus mode. *C++/Rust delta: Tauri had a config/plugin-store convention; here it's our own
  small JSON-on-disk with explicit load/save.*

- **1.3 — Start-on-login / autostart.** `S`
  Register autostart via the Windows Run key (launchd/systemd variants later), toggleable from
  settings.

- **1.4 — Native notifications.** `S`
  A native toast when a distraction is detected, complementing the overlay for when the window
  isn't focused. *C++/Rust delta: Tauri's notification plugin → Win32 toast API.*

- **1.5 — Idle / AFK detection.** `M`
  Detect when the user is away (no input for N minutes), pause capture and the active session,
  and resume on activity — so idle time doesn't pollute the feature windows or inflate session
  length.

- **1.6 — Privacy controls.** `M`
  A per-app exclusion list (never record titles from e.g. banking/password managers), a global
  "private mode" pause, and a plain-language local-only data statement in-app.

---

## Tier 2 — Product & ML depth

Make the insights worth coming back for, and close the training loop.

- **2.1 — Analytics / trends dashboard.** `L`
  Focus over time, a distraction-by-hour heatmap, top distracting apps, and session streaks —
  storage aggregate queries feeding new frontend views. *Reach for the dataviz design guidance
  before building charts.*

- **2.2 — Daily / weekly summary report.** `M`
  An in-app recap of the day/week (focus time, biggest distractions, best streak), with an
  optional export.

- **2.3 — Model retraining loop.** `L`
  Wire the `ml/` trainer to consume the exported CSV + the user's own labels → produce a fresh
  `model.onnx`; add model versioning and a "model info" panel. Opens the door to on-device
  personalization from a user's real behavior. *Rust ref: `ml/`, `engine/onnx_model.rs`.*

- **2.4 — Confidence calibration.** `M`
  Surface prediction uncertainty and stop nagging on low-confidence calls — fewer false
  "you're distracted" pops.

- **2.5 — Goal-alignment coverage.** `M`
  Broaden the app-context keyword/category tables and let the user edit categories, so the
  "is this on-goal?" signal generalizes past the seed keyword lists. *Rust ref:
  `engine/app_context.rs`, `engine/goal_alignment.rs`.*

- **2.6 — Pomodoro / focus-timer.** `S`
  An optional timer tied to sessions (work/break intervals) for people who structure focus
  that way.

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

- **4.1 — Structured logging + rotation.** `M`
  Replace scattered `std::cerr` writes with a small leveled logger writing a rotating file,
  plus an in-app diagnostics/health view.

- **4.2 — Fuzz the untrusted boundaries.** `M`
  libFuzzer targets for `title_parser` and the JSON IPC arg parsing — the two places
  attacker-influenced strings enter the core. *C++/Rust delta: Rust bounds-checks slices for
  free; our manual index math in the parser is exactly what fuzzing should hammer.*

- **4.3 — Opt-in crash reporting.** `M`
  Windows minidump capture on unhandled exceptions, written locally, opt-in only.

- **4.4 — Perf regression gate.** `M`
  Profile `engine_tick` allocations (the compute path already defers work — measure it), then
  add a threshold to the benchmark harness so a regression fails CI. *See
  [benchmarking.md](benchmarking.md).*

- **4.5 — Schema versioning + optional encryption.** `M`
  An explicit `schema_version` table with ordered migrations, and optional SQLCipher
  encryption-at-rest for the local DB.

- **4.6 — Dependabot.** `S`
  Add `.github/dependabot.yml` for GitHub Actions + npm, mirroring the Rust repo, so
  dependencies stay patched.

- **4.7 — Security-audit CI job.** `S`
  An `npm audit` gate in CI, mirroring the Rust repo's `security-audit` job.

---

## Suggested near-term sequence

If you want a default order rather than picking freely: **0.2 → 0.1 → 4.6 → 4.7 → 0.4 →
1.2 → 1.1**. That closes the cheap parity/CI gaps first, gets the release pipeline trustworthy,
then turns toward the v1 experience — leaving the large items (macOS capture 0.3, analytics 2.1,
retraining 2.3) for once the foundation is locked.
