# Changelog

All notable changes to Snapback are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**The version lives in `CMakeLists.txt`** (`project(... VERSION x.y.z)`) and is compiled into
the binary. A release tag must name it exactly — `scripts/check_release_tag.py` enforces that,
and [docs/PACKAGING.md](docs/PACKAGING.md) documents the order to cut a release in.

---

## [Unreleased]

Everything below has landed on `master` and is **not in any published release**. See the note
on `v0.2.0` at the bottom before cutting one.

### Added

- **Interactive goal history dropdown & auto-complete (2.11).** Starting focus sessions now provides
  an interactive auto-complete suggestions dropdown anchored to the goal input with full keyboard
  navigation (`↑`, `↓`, `Enter`, `Escape`) and click selection. Pinned presets and distinct recent
  goals are ranked and filtered in real time, automatically preserving each goal's preferred focus mode.
- **Close-to-tray desktop lifecycle (9.15).** Closing the main window (`X` or Alt+F4 on Windows,

  `windowShouldClose:` on macOS) now hides the window to the system tray rather than killing the
  process. Background input capture, focus tracking, active sessions, and tray controls remain
  active. Clean termination occurs exclusively when selecting "Quit Snapback" from the tray menu.
- **One-click contextual app rules from timeline and top apps (2.18).** Creating an Allow (Always

  productive) or Block (Always distracting) override rule no longer requires switching to Settings
  and typing process names into a text input. The Context Timeline on Now and the Top Apps breakdown
  on Review show one-click `+ Allow` and `+ Block` action buttons beside observed apps, and display
  active status badges for apps that already match an established rule.
- **A native file-dialog seam, used by database restore (10.14, partial).** Restoring a
  `focoflow.db` database now supports a native **Browse...** button using Win32 Common Dialogs on
  Windows and AppKit panels on macOS, rather than requiring users to manually type raw filesystem
  paths. Non-throwing cancellation semantics return a clean cancellation result when dismissed.
  This is the adapter and the **9.14** import half only: `export_my_data`, `export_summary_report`
  and `export_support_bundle` still choose a folder under the app-data directory and print the
  path, which is the behaviour 10.14 exists to replace. That item stays open.
- **Planned-versus-actual attended time on Review (2.19).** The Summary card for the shared

  Review range now leads with attended minutes from durable session spans, and — when a daily
  or weekly target applies — the planned total and percentage. Longer ranges still show
  attended time without inventing a prorated plan. Targets stay editable on Now.
- **`snapback --purge` removes everything the app created (9.5).** The database of window
  titles, settings, logs and rotations, exports, the model, SQLite's side files, every
  pre-migration backup, and the start-on-login entry. The decision behind it: someone who
  uninstalls an app that recorded their window titles believes they removed what it recorded,
  so leaving the database behind is the one outcome this product cannot afford. It reports what
  went and what did not, and exits non-zero on a partial removal rather than claiming a clean
  one. Files it did not create are left alone, as is the folder holding them.
- **One answer to "am I being recorded right now?" (2.10).** A Recording status card states
  one of five things — Recording, Paused for idle, Paused privately, No session, or Blocked —
  decided in one place in the backend so two surfaces cannot disagree about it. Privacy pause
  is one click, with 15/30/60-minute options that show the time left and end on their own. A
  timed pause is stored as a deadline rather than a countdown, so closing and reopening the
  window does not quietly restart or extend it.
- **Optional attended-time targets (2.19).** Set a daily or weekly goal in minutes and the Now
  surface shows how today and this week compare — or just what you attended, if you set no
  target. Off by default, and the copy stays flat: no streaks, no encouragement, no notification
  telling you that you are behind. The number counts durable attended spans only, never how long
  a session was left open or what the classifier thought of it, and a stretch that runs past
  midnight is split across the days it actually covered.
- **A Pomodoro timer you can actually drive (2.13).** The card now carries the controls: pause and resume, skip a phase, restart
  the one you are in, and configurable work/break lengths, long-break cadence, and whether the
  next phase begins on its own. The rhythm is a setting now rather than the app's opinion, and
  changing it applies from the next phase instead of restarting the one under way. **The timer
  survives a relaunch:** its phase and deadline are written down against the wall clock, so a
  deadline still in the future resumes with the time genuinely left — while one that passed
  while the app was closed restores at zero and waits for you, rather than silently running
  through the breaks and work blocks you were not there for. A skipped work block does not
  count toward the long break; that is a reward for work done.
- **An optional end-of-session reflection (2.14).** A card appears with the end-of-session check-in asking two free-text questions — what got done, and
  the next step — stored per session and carried into the readable personal export, so the
  thing that makes tomorrow's restart easier survives the session that produced it. Skipping
  costs nothing and records nothing: blank, whitespace, and never-asked are the same absent
  state, and clearing an answer returns it to that state. Kept out of the label table and out
  of training data on purpose; a label is a signal for the model, this is a note to yourself.
- **Pinned C++ deps are watched for new upstream releases (4.13).** A weekly workflow
  compares ONNX Runtime and the three git FetchContent pins to each latest GitHub release
  and opens an issue when any is behind. It never edits a digest.
- **macOS support.** Native `NSStatusItem` tray and a Cocoa `NSPanel` snapback overlay, a
  live `CGEventTap` capture path, and a hosted CI launch smoke that starts and quits the real
  app. Start-on-login via a launchd user agent.
- **Linux start-on-login** via a systemd user unit. (Tray and overlay remain Windows/macOS.)
- **Dashboard split into Now, Review and Settings surfaces**, with the Now surface led by
  focus *state* rather than a raw score, an explanation of each verdict, and a control to rate
  it.
- **Analytics and reporting.** Trends dashboard with hourly aggregates and top context apps,
  daily/weekly summary reports, an Insights card with stat tiles and a focus-trend chart, and
  a session history backend.
- **Pomodoro timer**, wired through app state, the engine tick, and the UI.
- **Privacy controls.** A global private mode, per-app exclusions, editable goal categories,
  a local-only statement, and privacy-scoped support bundles.
- **Data ownership.** Delete all locally collected activity, or a single session and
  everything captured during it.
- **Diagnostics panel** exposing capture and prediction health, plus a one-click JSON support
  bundle.
- **Native notifications** (Win32 toast) fired on snapback recovery, and a hyperfocus break
  nudge.
- **First-run onboarding wizard**, including the macOS Accessibility permission prompt and
  picking a default focus mode.
- **Model lifecycle.** Predictions stamped with a model identity, deployment gated on a
  held-out quality metric, and rollback to the previous model with its metadata.
- **Single-instance enforcement** — a second launch refuses to start rather than racing the
  first over the database.
- **Structured logging** with levels and a rotating file sink.
- **Storage schema versioning**: `user_version`, an ordered migration list, and a downgrade
  guard that refuses a database written by a newer build.
- **90-day retention** pruned on startup, with a `VACUUM` after a large prune to reclaim disk.
- **Attended session time.** A session's headline duration is now the time you were actually
  present, summed from durable spans that pause when you go idle and resume when you come
  back. Elapsed wall-clock time is still shown beneath it, and sessions recorded before this
  existed report "not measured" rather than a fabricated zero.
- **A configurable away threshold.** How long without keyboard or mouse input counts as away
  is a setting (30 seconds to 1 hour, five minutes by default) rather than a constant.
- **Interruptions are recorded.** When you drift away from focused work and come back, the
  episode is stored with when it started, how long you were away, and the way back — so the
  Snapback count in a session recap is a real number, and your data export lists the
  interruptions behind it.

### Changed

- **Training tooling is developer-only (ADR-0006 / 13.7).** Consumer Settings keeps Focus
  Feedback labels and no longer advertises repo-path / `ml/` train-from-export. Debug builds,
  or Release with `SNAPBACK_DEV_TRAINING`, still expose Model tooling; native train/repo
  commands refuse when the gate is off.

- Analytics windows are bucketed by the user's local hour rather than UTC.
- Recent-focus, insights, trends and summary reports all read from the same aggregates.
- Hot live reads are served from an immutable snapshot, so opening a history view no longer
  contends with capture writes.
- Session recap aggregation moved into SQL — three queries instead of `1 + 5N` round trips.
- Analytics and summary reports are aggregated entirely in SQL. Opening them no longer reads
  every stored prediction into memory while holding the lock the capture pipeline needs to
  write, so a long history no longer costs you dropped events.
- The hourly focus chart is drawn on a fixed 0–100 scale. It previously scaled every bar to
  the best hour in the data, so a poor day looked like a perfect one.
- Hours with no data are drawn distinctly from hours measured at zero, and each bar reports
  its sample count and distraction rate.
- The top-apps list says "samples" rather than "switches", which is what the underlying
  number has always counted.
- "Focus streak" is now a **duration** — the longest unbroken stretch of focused work — instead
  of a count of prediction rows shown under a time-like label. Two people doing identical work
  previously got different numbers depending on how much they typed.
- The trends tile that counts productive sessions is now labelled "Sessions in a row", so it
  cannot be mistaken for the duration above.
- **"Export my data" contains everything.** It previously stopped at 200 sessions and 500
  windows per session while the file itself said it held every session, and it could report a
  complete export after dropping windows. The file now ends with a manifest stating exactly
  what it holds, plus a checksum so a cut-short copy is detectable.
- Release builds no longer expose the webview developer tools or honour
  `SNAPBACK_FRONTEND_URL`; both are Debug-only.

### Fixed

- **The test suite compiles on MSVC and libc++ again.** `focus_window_windows.cpp` called
  `std::towlower` with only `<cctype>` included; the wide-character functions live in
  `<cwctype>`. libstdc++ happens to pull it in transitively, so the one Windows job added to
  catch toolchain divergence — `windows-gcc` — was the only Windows job that could not see
  this, while `windows-latest` and every macOS job failed to build.
- **`focus_window` refuses whitespace-only targets on every platform.** Its input validation
  had been written three times, once per platform file, and the copies disagreed: Windows
  trimmed before checking for emptiness, macOS and the stub did not. `focus_window("   ", "")`
  was therefore refused on Windows and treated as a real search target everywhere else, which
  failed CTest on Linux under the plain, sanitizer, and ONNX jobs. The shared half now lives
  once in `snapback/focus_window.cpp`; the platform files implement only
  `focus_window_supported()` and `detail::focus_window_native()`, which is the split
  `focus_window.hpp` already described. Its test no longer asserts the Windows "could not
  find" wording on platforms where no search ever runs.
- **The desktop app compiles again.** Two unqualified names, in the only translation unit no
  test compiles: `main.cpp` used `DataDirChoice` / `choose_data_dir` from an anonymous
  namespace while both live in `namespace snapback`, and `commands.hpp` used `JsonHandler`
  from `namespace snapback` while it lives in `snapback::detail`. Nothing outside `main.cpp`
  includes `commands.hpp` — `test_ipc_contract` reads it as text — so the app had been
  unbuildable on every platform with no test able to say so.
- **A failing native command fails the PowerShell script that ran it.** `cmake`, `ctest` and
  `npm` report failure through `$LASTEXITCODE`, which `$ErrorActionPreference = "Stop"` does
  not cover — so `windows_demo.ps1` built a broken binary and exited 0, keeping the Windows
  job green over the compile error above. Every native call now goes through `Invoke-Native`,
  and `scripts/check_ps_exit_codes.py` fails CI if one does not.
- **The Windows app target builds.** webview reaches WebView2, which reaches `windows.h`,
  which defines `min`/`max` as macros unless `NOMINMAX` is set — so every `std::max(a, b)`
  afterwards expanded to `std::((a) > (b) ? ...)` and MSVC rejected it (C2589) at
  `focus_summary.hpp` and `logger.hpp`. `webview_compat.hpp`, which exists to contain exactly
  this kind of pollution, now sets `NOMINMAX` before the include and scrubs both macros after.
  Only MSVC ever saw it: libstdc++ `#undef`s them itself, which is why the windows-gcc job
  stayed green throughout.
- **Windows builds no longer name a Visual Studio version.** `windows_demo.ps1` and
  `package_windows.ps1` asked CMake for "Visual Studio 17 2022"; once `windows-latest` stopped
  shipping it, configure failed with "could not find any instance of Visual Studio" — which
  would have broken packaging and releases too, not just the demo smoke. Both now let CMake
  pick the installed toolchain, as every other Windows CI job already did, and keep `-A x64`
  so the `win64` artifact name stays an assertion rather than an assumption.
- **Two high-severity advisories in the frontend lockfile.** `nanoid` (via `postcss`/`vite`)
  and `undici` (via `jsdom`) bumped to patched versions; both are devDependencies, so nothing
  shipped was affected. `npm audit` is clean again.
- **Excluded apps no longer earn the missed-session nudge (2.7).** Time in an excluded app
  resets the untracked stretch the same way private mode does, so leaving Slack does not
  immediately ask you to start recording.
- **Capture failure/running contradiction is observed from a second thread (11.9).** The
  old same-thread sample could not see inverted stores on MinGW; a paired sampler now fails
  that bug and stays quiet when the stores are ordered correctly.
- **Autostart scratch tests no longer leave registry keys after a crash (11.10).** The
  fixture sweeps `test-<pid>-*` orphans whose process is gone and leaves live concurrent
  cases untouched.
- **ADRs no longer cite a file that clones do not have (12.7).** The Darwin-dev fact lives
  inline; the doc-path guard rejects a link or relative path to the gitignored local agent
  file even when it exists on the author's machine.
- **Privileged webview commands require a trusted document (8.14).** A per-launch capability
  token is handed only to the packaged UI; external links open in the system browser.
- **Model deployment recovery no longer bricks startup (13.8).** Cleanup debris degrades
  health and offers Retry cleanup instead of exiting before the window opens.
- **Settings survive power loss after a reported save (7.21).** The atomic rename from 7.19
  is now followed by a durable flush of the temp file and of the parent directory, so a
  successful save is not only tear-free but also on disk.
- **Session replacement is atomic.** Starting a session while one is running could previously
  close the old session and then fail to create the new one, leaving no active session at all.
- **A failed start no longer half-happens.** Focus mode and the feature extractor were reset
  before the database write that could fail, so a failed start left the app recording against
  a session that did not exist.
- **Replacing a session records a verdict for it**, the same automatic label an explicit stop
  writes. Previously that depended on whether you pressed Stop or just started the next thing.
- **Stopping twice no longer writes two contradictory automatic labels.**
- **Reopening restores the session you were in**: its saved focus mode (so a Deep session is
  not silently continued under Normal's rules) and its elapsed time, which used to restart from
  zero while the recap beside it reported hours.
- **A crash no longer counts the time the app was closed as time you were present.** A session
  span left open by a dead process is closed at the last activity that session recorded.
- **A settings change that cannot be saved no longer takes effect anyway.** Turning private
  mode off could previously resume recording while the app reported that the change had
  failed. Settings are written to disk before anything changes in the running app.
- **"Delete all activity" removes every copy.** It missed the readable personal export and the
  database backups taken before each schema upgrade, and one file it could not remove used to
  stop it clearing the database at all. It now attempts every target, and reports what was
  deleted, what could not be, and what was deliberately kept.
- **Settings are written crash-safely** via a temp file and an atomic rename, with a
  last-known-good backup and a log line when a settings file cannot be parsed. Previously a
  partial write left an empty file that silently became defaults.
- ONNX inference no longer discards user rules, thrash and drift; an ONNX failure no longer
  writes an empty `focus_state`.
- `Storage::open` reports why it failed instead of collapsing every error into "no database".
- CSV export checks for write failure instead of reporting success on a full disk.
- Privacy exclusions match whole app-name words, so "Slack" no longer excludes "Slackline".
- Capture health reports a returned hook as failed, and never reports "failed" and "running"
  at the same time.
- Automatic session labels are saved on shutdown, including the no-argument stop path.
- File replacement is portable across toolchains (`copy_options::overwrite_existing` is
  ignored by libstdc++ on MinGW, which silently left stale backups and broke model rollback).

### Internal

- **A `commit-msg` hook removes AI attribution before the commit exists.**
  `check_commit_attribution.py` already rejected those trailers, but only in CI — by which
  point fixing one means rewriting history and invalidating every downstream SHA. The hook
  strips trailer-shaped boilerplate silently, then re-checks what survived by calling that
  same script's new `--message-file` mode, so the hook and the gate cannot drift apart on what
  counts as attribution. Enable it per clone with `git config core.hooksPath scripts/hooks`.
  CI runs `scripts/test_commit_msg_hook.sh`, which executes the real hook against real
  messages; each forbidden pattern has a case that fails when only that pattern is removed.
- CI builds and tests the portable core with MinGW-w64 GCC on Windows, the one toolchain
  combination that was previously never built — and the one a shipped file-replacement bug
  lived in.

### Security

- The bundled dashboard declares a Content Security Policy, and host events cross into the
  webview as escaped strings.
- Every fetched C++ dependency is pinned to an immutable revision (commit SHA or `URL_HASH`),
  enforced in CI.
- Vendored ONNX Runtime archives are verified against a recorded SHA-256 **before** extraction.
- Releases are gated on the tag naming `PROJECT_VERSION`, the tagged commit being reachable
  from `master`, and that commit having a green CI run.
- The Windows executable is signed **before** it is packaged, and the signature is verified
  inside the artifact that gets uploaded. (Signing itself still needs a certificate — see
  [ROADMAP 0.4b](docs/ROADMAP.md).)

### Known gaps

- **No macOS packaging.** No signed `.app`, notarization, or DMG yet — the one remaining
  formal v1 blocker.
- **Windows artifacts are unsigned** until a code-signing certificate is provisioned.
- **No project license.** The repository ships no `LICENSE` or third-party notice file, so it
  is not yet open source in any usable sense.
- No Linux tray or overlay, and no Linux packaging.

---

## [0.2.0] — tagged 2026-07-05, not a published baseline

A `v0.2.0` tag exists and points at commit `ba4050f`, but **that commit is not reachable from
`master` or any other branch** — history was rewritten underneath it, leaving the tag
orphaned. `master` is 361 commits ahead of it and still declares version `0.2.0`.

It is recorded here so the gap is visible rather than mysterious, not because it describes
shipped software. Before cutting a real release: bump `project(... VERSION …)`, then tag the
commit on `master` that CI has proven green. The `verify-tag` job will refuse the current tag
precisely because it is unreachable from `master`, which is the check working as intended.
