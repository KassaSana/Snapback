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

### Changed

- Analytics windows are bucketed by the user's local hour rather than UTC.
- Recent-focus, insights, trends and summary reports all read from the same aggregates.
- Hot live reads are served from an immutable snapshot, so opening a history view no longer
  contends with capture writes.
- Session recap aggregation moved into SQL — three queries instead of `1 + 5N` round trips.
- Release builds no longer expose the webview developer tools or honour
  `SNAPBACK_FRONTEND_URL`; both are Debug-only.

### Fixed

- **Session replacement is atomic.** Starting a session while one is running could previously
  close the old session and then fail to create the new one, leaving no active session at all.
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
