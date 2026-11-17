# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is set in
`CMakeLists.txt` and a release tag has to match it.

## [Unreleased]

### Added

- The recording status in the header now updates when a pause, snooze, idle timeout or
  private-mode toggle comes from the tray or Settings, and says so when the app can't be
  reached to confirm.
- Starting a new session while one is running is now two explicit steps. If the replacement
  fails to start, you see the stopped session and its recap instead of a ghost.
- Review cards show which time range their numbers came from, and offer Retry when a load
  fails.
- The permission dialog traps focus and restores it on close.
- Every Review chart has a "Show data" table for screen readers and keyboard users.

### Fixed

- Loading Review no longer delays the engine's writes. A save now waits for at most one read,
  not all five.
- `install_windows_package.ps1` copied nothing from the ZIP: it passed a wildcard to
  `-LiteralPath`. It now copies each entry explicitly.

## [0.3.0] - 2026-08-29

First published release: a Windows installer and ZIP (unsigned).

### Added

- **Three surfaces: Now, Review and Settings.** Now is the live session. Review covers
  history, trends and per-session detail. Settings holds privacy, rules and preferences.
- **Sessions** have a goal, a focus mode and attended time (time you were actually present).
  An optional reflection at the end, optional daily targets, and a planned-vs-actual view on
  Review.
- **Overlay and alerts.** Click an alert to jump back to the window you were working in.
  Choose when and how Snapback interrupts you, and get quiet hours.
- **App rules.** Mark an app as allowed or blocked in one click from the timeline or the top
  apps list, or teach Snapback which apps count as work from inside a session.
- **Pomodoro timer** that runs alongside a session.
- **Recording status.** A single indicator for whether you're being recorded, with pause,
  snooze and private mode.
- **Desktop lifecycle.** Close to tray, start on login (Windows, macOS, Linux), and a second
  launch brings the running window back.
- **Your data.** Export everything, import a backup through a native file dialog, delete
  activity, and `snapback --purge` to remove every file the app created. Retention is 90 days.
- **macOS support:** event tap capture, browser-tab enrichment, a menu-bar item and a native
  overlay panel.
- Diagnostics panel, structured logs, first-run onboarding, and a support-bundle export.
- Optional ONNX model with safe deploy and rollback (developer tooling).
- Hosted demo on GitHub Pages, MIT license, third-party notices and a written threat model.

### Changed

- A snapback shows the overlay only, not the overlay plus a system notification.
- Model training tooling is developer-only and not part of the shipped app.
- "Export my data" includes every table.

### Fixed

- Replacing, starting and stopping sessions are atomic. A failure no longer leaves a
  half-started session or writes two conflicting labels.
- After a crash, the time the app was closed isn't counted as attended time.
- Settings are written crash-safely, and a setting that fails to save doesn't take effect.
- "Delete all activity" removes every copy, including derived tables.
- Privileged webview commands only run from the trusted bundled page.
- A failed model deployment can no longer stop the app from starting.
- The release workflow produces an installer, and a failing native command now fails the
  PowerShell script that ran it.
- Builds on MSVC and libc++ again; two high-severity frontend advisories patched.

### Security

- Content Security Policy on the bundled dashboard; host events reach the webview as escaped
  strings.
- C++ dependencies pinned to commit SHAs or hashes; ONNX Runtime archives are checked against
  a SHA-256 before extraction.
- Releases only publish a tag whose commit is on `master` and passed CI.

### Known gaps

- No macOS packaging (signed `.app`, notarization, DMG) yet.
- Windows artifacts are unsigned.
- No Linux tray, overlay or packaging.
