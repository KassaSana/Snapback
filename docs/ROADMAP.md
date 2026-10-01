# ROADMAP — current priorities and open work

This is the sole live backlog. Check current code and tests before implementing an
item; a historical review is evidence to recheck, not implementation authority.
Completed resolutions live in [the archive](roadmap_archive.md). Older strategy
and audit narratives live in [planning history](archive/roadmap-planning-history.md).

## Start here — the current sequence

Open work, ordered by dependency. The tiers below hold each item's status and acceptance
criteria; completed items are indexed in [the archive](roadmap_archive.md). Historical strategy is retained in the
[planning archive](archive/roadmap-planning-history.md), not as a competing sequence.

| # | Item | Next step |
| --- | --- | --- |
| 1 | **3.3** macOS packaging and notarization | Start the Apple Developer account work; this is the remaining formal v1 blocker. |
| 2 | **4.11** title-parser decision | Settle the behavior in an ADR before changing code. |
| 3 | **10.1 / 14.3** webview and command contract | Complete real-bridge coverage and result-shape checks. |
| 4 | **4.4 / 14.5** engine performance | Measure engine-cycle allocations and finish bounded, deadline-driven work. |
| 5 | **2.3 / Tier 13** developer model tooling | Resolve label sufficiency and model-versus-heuristic policy before expanding retraining. |

[ADR-0008](adr/0008-protect-master-from-red-ci.md) records the original fifteen required
checks. GitHub branch protection for `master` was verified on 2026-09-29 to require the seven
current CI contexts described in [testing strategy](testing_strategy.md). Update that hosted
list whenever the CI job set changes.

---


## How to read an item

- **Status**, first tag on every item — exactly one of:
  - **`proposed`** — a finding or an idea. Evidence may be solid; *nobody has agreed to build
    it*. This is the default, and most of the backlog is here. A review that lands new items
    lands them `proposed`.
  - **`accepted`** — agreed to build, with the agreement citable: a row in
    [Start here](#start-here--the-current-sequence), or a release blocker in an ADR. Promotion
    is a decision someone makes on purpose, never a side effect of writing the item well.
  - **`in progress`** — part of it has landed and the item says which part and what remains.
- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **`decision`** — do **not** write code for this until the question is answered. Roughly a
  third of the open backlog is decisions mistaken for bugs; that mistake has been made
  repeatedly here and has twice produced a "fix" that had to be reverted.

Work each item on the standard loop: code → test → explanation → commit. Commit conventions
and the attribution guard are in [`CONTRIBUTING.md`](../CONTRIBUTING.md); only Kassa pushes.

---

## Tier 6 — CI health

- **6.5 — Confirm MSVC warning suppression.** `in progress` `S`
  `tests/doctest_wrapper.hpp` suppresses third-party C5285 under `_MSC_VER` with a
  warning push/pop; the implementation exists. Remaining: inspect a Windows CI build
  log to confirm the doctest noise is absent while project diagnostics remain enabled.

---

## Tier 0 — Finish the port's last gaps

- **P0-09 — Release-build soak on Windows.** `accepted` `M` (manual verification)
  Use the [Windows demo runbook](windows_demo.md) with a Release desktop build.
  Dependencies P0-01–P0-05 and P0-07 are recorded complete in planning history.
  Verify: staged distraction → snapback → Take me back activates the target;
  a dashboard external link opens the system browser; after stopping then 10+ minutes
  of activity, the stopped session's attended minutes do not grow. Record the host,
  build/commit, and result. A headless suite or prior interactive QA is not this soak.

- **0.4b — Provision the signing certificate.** `proposed` `S` (external dependency)
  Signing and artifact verification are implemented: the executable is signed before
  CPack, the NSIS installer after creation, and extracted artifacts must have valid
  signatures from the requested thumbprint. See [Packaging](PACKAGING.md).
  Remaining: provision the certificate and runner credentials, produce a signed release,
  and verify the packaged success path. Negative checks do not prove that success path;
  describe signing as incomplete until it has run end to end.

---

## Tier 7 — Correctness & product findings (2026-07-20 staff review)

### Correctness

- **7.2 — Decide rolling versus calendar Review windows.** `in progress` `S` `decision`
  UTC storage and local-hour presentation are implemented. Review presets explicitly
  say Last 24h / Last 7 days; only attendance and daily buckets snap to local calendar
  boundaries. Evidence: `frontend/src/reviewRange.ts`, `state.cpp:cutoff_unix_ms`.
  Remaining: decide whether rolling presets should change to calendar windows; the
  present labels are honest. Any change needs explicit date/DST boundary tests and
  consistent query and display semantics, rather than another timestamp conversion.

- **7.27 — Define and test one capture-event contract across platforms.** `in progress` `M` for Windows + macOS; `L` including Linux
  **Progress 2026-10-01:** Scroll activity, macOS motion coordinates/speed, Linux key/button/repeat/wheel translation, and cadence-bounded injectable Linux context are implemented with portable translation and burst fixtures. The 31-feature contract is unchanged; Linux motion calibration and live Windows/macOS distributions remain unverified, so this item stays in progress.

  Historical opening finding (2026-08-05, predating the progress above): the extractor assumes
  `CaptureEvent` has portable meaning, but each
  backend still differs. Windows counts button-down only and ignores release/wheel traffic;
  its foreground context refreshes on a 500 ms timer. macOS writes mouse speed as zero. Linux maps
  every `EV_KEY` press, including mouse buttons, to `KeyPress`, leaves kinematics empty, and
  can query foreground context through `sh`/`xdotool`/`ps` inside the input loop.

  The result is model drift by operating system: macOS has effectively dead mouse features,
  Linux can mix mouse buttons into typing while spawning work
  proportional to input volume. **0.3** proved live macOS delivery and **11.3** starts after a
  normalized event already exists; neither tests this boundary.

  **Windows mouse-speed conversion landed 2026-09-21** (Astra review slice 6). `mouse_proc`
  now measures movement with a steady-clock duration instead of reusing the millisecond event
  timestamp. The pure translation seam widens coordinates before subtraction, floors a zero or
  invalid interval, and clamps non-finite or out-of-range speeds before converting to the
  `CaptureEvent` `uint32_t` field. Tests cover ordinary movement, equal timestamps, large
  displacement, and the signed coordinate extremes. The remaining 7.27 work is the
  cross-platform event contract and provider-boundary work described below.

  Introduce pure per-platform translation fixtures against one documented event contract:
  one click per button-down, explicit wheel semantics, consistent speed units, and identical
  key/button classification. No raw callback or device-read loop may launch a child process;
  foreground context must be an injectable, cadence-bounded provider. Add live platform
  distribution smokes (expected nonzero fields). Over a fixed-duration 10,000-event burst,
  context-probe count must stay within the provider's cadence bound (plus initialization) and
  remain effectively the same as a zero-event control of equal duration; event count itself
  cannot increase probe count.

- **7.28 — Separate goal-category identity from editable display text.** `proposed` `M/L`
  Opened 2026-08-05. `GoalCategoriesCard` can edit only the rows it receives: there is no Add,
  Remove, Disable, or explicit Reset. Saving an empty list is also not stable — the getter and
  scorer silently resurrect the built-in defaults. More seriously, context compatibility
  infers semantics from category-name substrings such as `research`, `read`, `commun`, and
  `meeting`. Renaming a visible “Research” category to “Study” can change classification even
  when every keyword stays the same.

  Give built-in semantic categories stable ids/kinds independent of their display names, and
  make custom-category behavior explicit rather than guessing it from text. The UI must support
  add, remove, disable all, and **Restore defaults**, with a live preview explaining how a sample
  goal/context will score. Empty/disabled must remain empty/disabled after restart; defaults
  return only through the explicit reset action.

  Migrate recognized existing names without changing their behavior and surface ambiguous
  custom names for review. A rename-only test must leave scores byte-for-byte unchanged; add
  C++/JSON/frontend round trips for create/remove/disable/reset and include the new stable fields
  in feature-parity/model-contract review. Settings publication follows **7.26** so disk and live
  classification cannot disagree.

- **7.29 — Treat screen lock, suspend, wake, and unlock as first-class lifecycle events.** `proposed` `M/L`
  Opened 2026-08-05. The only lifecycle signal today is ordinary input inactivity, so a locked
  or sleeping machine remains “attended” until the five-minute idle threshold expires. There
  are no Windows session/power or macOS workspace sleep/wake adapters. Capture permissions,
  hooks, Pomodoro deadlines, and notification delivery are therefore all left to whatever the
  next 100 ms tick happens to observe after resume.

  Normalize native events into **Locked**, **Suspending**, **Resumed**, and **Unlocked**. Lock or
  suspend immediately closes **7.23**'s attended span, suppresses context/prediction processing
  and every intervention, and checkpoints the minimum safe state. Wake revalidates/re-arms the
  capture backend but remains paused until genuine post-unlock input; it must not manufacture
  an attended interval or replay a burst of expired notices.

  Injected lifecycle tests must cover duplicate and out-of-order events, sleep spanning a day/
  DST boundary, lock during persistence, shutdown while suspended, and each Pomodoro policy in
  **2.13**. The platform adapters stay thin over one testable state machine. Build on **7.23**,
  **7.24**, and preferably **14.2**; Linux may begin with a truthful unsupported/stub state if
  its desktop-session contract is deferred beyond v1.

### Decisions — do not code these yet

- **7.8 — `set_focus_mode` permanently rewrites the user's default.** `proposed` `S` `decision`

  `set_focus_mode()` (`state.cpp:set_focus_mode`) sets the live mode *and* writes
  `settings_.default_focus_mode` to disk on every call. Switching to Recovery once for a
  rough afternoon makes Recovery the startup default forever — silently overwriting the
  answer the onboarding wizard (1.1) explicitly asked for.

  Decide whether "current mode" and "default mode" are one setting or two. They're currently
  one; the wizard's existence implies two.

---

## Tier 8 — Security hardening (2026-07-20 staff review)

- **8.11 — Add scoped capture rules before sensitive window titles reach the pipeline.** `proposed` `M/L`
  Opened 2026-08-05. Privacy currently offers one global switch and a list of app-name
  exclusions. `is_private_event_unlocked()` checks only `event.app_name`; a user who wants to
  hide one banking, health, password-manager, or client browser context must therefore exclude
  the entire browser. Otherwise the raw title can flow through features, snapback summaries,
  context storage, exports, and notification copy.

  Add ordered rules scoped to an app plus an optional title/context matcher, with explicit
  actions **Capture full context**, **Capture app only**, and **Exclude**. Match at the capture
  boundary and pass only the sanitized event downstream; a redacted title must never survive
  in a queued event waiting to be persisted later. Use bounded literal/glob matching rather
  than an unbounded regex engine. Domain matching is allowed only on platforms where **7.27**
  establishes a real domain field — do not infer a URL from a tab title.

  Migrate today's `excluded_apps` entries to app-scoped Exclude rules. The editor must preview
  scope and precedence without writing new raw examples to disk. A secret-string fixture must
  prove that a sensitive tab is dropped or app-only while an ordinary tab in the same browser
  still records, and that the secret appears nowhere in the database, emitted events, logs,
  notifications, or every export. Build after **7.26** makes the setting atomic and align the
  storage boundary with the threat model in **8.5**.

- **8.14 — Confine privileged webview commands to the trusted packaged UI.** `proposed` `M/L`
  Opened 2026-08-05. **8.4** locks down the initial release URL and **8.3** constrains scripts
  loaded by the bundled page. Neither handles a later top-level navigation. `kIpcShim` is
  installed with `webview.init()`, whose own comment says it runs before scripts on **every**
  navigation, and every native command is bound globally. A remote page reached by a redirect
  or future Help link can therefore inherit deletion, export, settings, and training commands.

  Define one trusted main-frame origin for release assets and enforce it at navigation and
  command invocation boundaries. Block remote top-level navigation inside the privileged
  webview; explicit external links open in the system browser with no native bridge. A Debug
  loopback exception is allowed only behind the existing build-time debug gate. Do not rely on
  CSP as an origin check — CSP governs resources inside the trusted document, not which
  document owns the native bindings.

  Use **10.1**'s real-webview harness for an adversarial test: navigate/redirect to a malicious
  page and prove it cannot invoke a sentinel mutation, then prove the packaged page still can.
  Cover subframes, redirects, `file:` path aliases, navigation races, and user-initiated
  external links. Align the external opener with **2.8**'s no-shell platform adapters and the
  security policy with **8.5**.

---

## Tier 2 — Product & ML depth

- **2.9 — Complete the session explorer.** `in progress` `M/L`
  Review already lists sessions by day and opens selected-session details with recap,
  focus curve, context, apps, reflection, and recorded snapback detour. Repeat fills the
  Now form without recording; deletion is secondary and confirmed. Evidence:
  `frontend/src/SessionExplorerCard.tsx`, `frontend/tests/sessionExplorerFlow.test.tsx`,
  and `tests/test_storage.cpp`.
  Remaining: goal/app/date search, mode/verdict filters, cursor-based pagination beyond
  the 500-session range cap, and labels with **2.17**. Preserve bounded queries and the
  500-row context disclosure; do not load the database into the browser. New query shapes
  must satisfy **14.3**, and repeat must preserve ADR-0005's deliberate Start action.

- **2.17 — Give feedback an authoritative, editable label ledger.** `in progress` `M/L`
  **Progress 2026-10-01:** Limited labeling-trust fixes: empty native/demo sessions no longer infer an automatic label or create a new automatic training row. Recaps carry sampleCount and distinguish no predictions from measured zero. Keep submits an explicit survey agreement, confirms successful saves in the recap, retains the check-in on failure, and coalesces pending clicks. Live Settings feedback is disabled without an active session; new-session state and older save responses cannot leak prior feedback. The demo retains feedback in memory for the current visit, clears it on deletion, and discloses reload reset. Existing historical rows are preserved. Recap now shows `Saved: {check-in label} — Change` so a successful rating can be reopened. Review detail reads `get_session_rating` (survey, else auto) and can re-rate. **Progress 2026-10-01:** "Start a different session" is two-phase — stop, then check-in or Discard for fragments (`durationSecs < 60` and `sampleCount === 0`), then start. Fragments stay in Session Explorer (marked) but are excluded from Review aggregates (`session_window_totals`, demo summary). Remaining: authoritative scope/precedence, effective-label exports, supersession, persistent submission idempotence, and migration of existing conflicts.

  Opened 2026-08-05. Auto labels, the end-session check-in, and live verdict corrections all
  call append-only `insert_label()`. There is no list/update/supersede command, live feedback
  is attached only to a session rather than an exact prediction, and training export writes
  every row. A user-facing “Override” can therefore add a conflicting label without defining
  which one is truth.

  Define label scope and precedence: prediction-scoped corrections versus session-scoped auto
  and survey labels. Keep an append-only audit trail with stable ids and `supersedes` links,
  but expose one effective-label view; the survey supersedes the automatic session label and
  retrying the same submission is idempotent. Existing conflicts need a deterministic migration
  rule rather than “last row happened to win.” This fulfills the label-idempotence prerequisite
  identified by **13.5** without pre-deciding whether there is enough data to train.

  In **2.9** show label, scope, source, time, and provenance with Amend/Undo actions. Live
  feedback must target the prediction the user actually saw, even if a newer prediction has
  arrived. Training export emits effective labels only, while the personal export may retain
  the audit trail. Tests must cover repeated clicks, auto→survey supersession, two corrections,
  undo, session deletion, and export. Coordinate lifecycle writes with **7.25**.

- **2.18 — Scope contextual classification rules.** `in progress` `M`
  Contextual Allow/Block actions exist, but `AppRuleRecord` still has only pattern,
  rule_type, and note: these remain global substring rules. Existing global rules must
  keep their meaning. Remaining: exact-app, title-pattern, and app-plus-goal-category
  scopes; match-count preview, deterministic conflict precedence, and Undo restoring the
  exact prior rule set. Save must affect the next classification tick.
  Depends on **7.27** context identity, **7.28** stable goal-category ids, and **4.11**
  title-parser policy. Tests must show the same Slack/Chrome context can resolve
  differently for Coding and Communication. Explain that rules tune classification;
  they neither block applications nor redact capture (**8.11**).

- **2.3 — Model retraining loop.** `accepted` `L` — developer tooling under
  [ADR-0006](adr/0006-trainer-is-developer-tooling.md).
  The intended loop is exported data + labels → a fresh `model.onnx`. The training,
  deployment, and rollback plumbing exists for developers; it is not a consumer Settings
  feature. Before expanding the loop, resolve **13.5**'s label-sufficiency question and
  **13.6**'s model-versus-heuristic calibration policy.

  Work the remaining decisions through **Tier 13**; **13.5** may rescope this item to data
  collection if labels are too sparse. Bundle **5.6** with a retrain because it changes both
  extractors.

---

## Tier 3 — Cross-platform breadth & packaging

- **3.2 — Linux tray + overlay.** `proposed` `M`
  `libappindicator` tray + an overlay window (X11/Wayland caveats noted). Since 3.1 landed,
  Linux is the **only** remaining user of `src/app/tray_stub.cpp` and
  `src/snapback/overlay_stub.cpp`; both now guard on `!_WIN32 && !__APPLE__`. Read
  `src/app/tray_macos.mm` and `src/snapback/overlay_macos.mm` first — they are the worked
  example of replacing these two stubs, including the shared `tray_menu_entries()` model a
  third platform should reuse rather than re-list.

- **3.3 — macOS packaging.** `accepted` `L` — `.app` bundle + notarization + DMG.

- **3.4 — Linux packaging.** `proposed` `M` — AppImage and/or Flatpak.

- **3.5 — In-app "check for updates".** `proposed` `M`
  Fetch a version manifest and offer a download link — no silent install. The lightweight
  variant of the auto-updater deferred in [PACKAGING.md](PACKAGING.md).
  This cannot be implemented until a new ADR amends the network-silent product rule;
  the binding exclusion below applies even though the proposal remains recorded.

- **3.7 — Snapback as a real web product.** `proposed` `XL` `decision` **stated goal, not scheduled**
  Opened 2026-08-27 because it is a direction the project is aimed at, and an undocumented
  ambition turns into an accidental architecture. **3.6's demo is not a step toward this** —
  it is a static page with invented data, deliberately so.

  The blocker is not effort, it is physics: a browser tab cannot enumerate other applications'
  windows or read OS-level input idle time, and those two signals are the entire input to the
  feature vector. So there is no version of this where the browser replaces the capture agent.
  Every real design keeps a native agent on the machine and moves *storage and presentation*
  off it, which is a different product with a different privacy contract.

  What has to be settled before any code, and why each is load-bearing:

  - **It contradicts an accepted promise.** The Privacy card says "Nothing leaves this device",
    [ADR-0002](adr/0002-v1-supports-windows-and-macos.md) scopes v1 to two desktops, and 8.10
    made release builds network-silent on purpose. A sync target is a new ADR that supersedes
    part of that, not a feature added underneath it.
  - **Accounts and a server** mean auth, multi-tenancy, and an operational cost this project
    has never had. It also makes 8.5's threat model a prerequisite rather than a parallel item.
  - **What actually syncs.** Window titles are the most sensitive rows in the database. A
    defensible first version might sync only aggregates — scores, durations, session goals —
    and keep raw context local. That choice defines the whole schema.
  - **Which half owns the verdict.** Classification currently runs in-process against live
    capture. Moving it server-side changes latency, the snapback path, and what an offline
    machine can still do.

  A staged path exists if this is wanted sooner: read-only first. The desktop agent pushes the
  aggregates 7.12 already computes; the web app renders Review only, with no live Now surface
  and no controls. That version needs no capture in the browser, no round trip on the alert
  path, and can be switched off without the desktop app noticing. Depends on **8.5**, a new
  ADR, and — realistically — on v1 shipping first.

---

## Tier 5 — Open findings from the 2026-07-20 engine/storage audit

- **5.6 — `longest_active_stretch_5min` reports 300s for brand-new sessions.** `proposed` `M` `decision`
  — **do not "just fix" this; it will fail CI. Bundle into 2.3.**

  `features.cpp:extract` defaults to the whole 5-minute window when it holds no idle events, so
  ten seconds into a session the extractor claims a five-minute unbroken active stretch.

  1. It is long-standing, deliberate behaviour, not an accident.
  2. **The feature-parity golden test pins every key of the feature vector**
     (`tests/test_feature_parity.cpp`). Changing this without updating the golden fails it.

  Defensible as-is: the feature is defined over a fixed window, not the session. The real
  question is whether a feature that is constant-300 for most users carries signal at all —
  which is a 2.3 question, since answering it means changing both extractors and retraining.

---

## Tier 4 — Engineering quality & hardening (cross-cutting)

- **4.11 — `title_parser` fabricates filenames.** `proposed` `M` `decision`

  Two distinct defects, one root cause — `parse_title` has no notion of "does this look like
  a filename?":

  1. **Separator case.** It splits on `" — "` / `" - "` and treats segment 0 as a filename
     with no check. `"Some Article - Google Chrome"` yields `file_hint = "Some Article"`, and
     `tracker.cpp:make_snapshot` turns that into **"Editing Some Article"**.
  2. **No-separator case (worse).** `title_parser.cpp:parse_title`:
     `if (hints.file_hint.empty()) hints.file_hint = window_title;` — with no separator at
     all, **the entire title becomes the file hint.** `build_snapback()` (`tracker.cpp:build_snapback`)
     then renders `"Return to " + file_hint`, so a fullscreen video titled
     `Top 10 Productivity Fails` produces the overlay **"Return to Top 10 Productivity
     Fails"** — the product's namesake feature telling you to go back to the distraction.

  **Needs a decision first:** this behaviour is long-standing, so changing it is a deliberate
  break with how the app has always worked, not a bug fix. Cheapest fix is to consult `title_is_distracting`, which `app_context.cpp:classify_app_context`
  already computes and `make_snapshot` ignores.
  *Note: the parser takes no `app_name`, so per-app title conventions are
  currently unimplementable. The decision should settle whether to add it.*

- **4.2 — Fuzz the untrusted boundaries.** `proposed` `M`
  libFuzzer targets for `title_parser`, the JSON IPC arg parsing, **and the Windows shell
  quoting in `training_deploy.cpp`** — `cmd.exe` metacharacter handling (`^`, `%VAR%`, `&`,
  embedded quotes) is a genuinely different escaping problem from POSIX `sh`, and the same
  `quote()` serves both (`training_deploy.cpp:quote`). `%` in particular is not neutralized by
  double-quoting in `cmd.exe`. Low severity (self-injection from a user-entered path), but
  it's the natural third target.

  **Consider instead:** building the process directly (`CreateProcessW` / `posix_spawn`) with
  an argv array removes the entire quoting problem class rather than escaping it correctly.
  *The parser's manual index math is exactly what fuzzing should hammer.*

- **4.3 — Opt-in crash reporting.** `proposed` `M`
  Windows minidump capture on unhandled exceptions, written locally, opt-in only. Note 8.1
  reduces how often this fires; do 8.1 first.

- **4.4 — Profile engine-cycle allocations.** `accepted` `M`
  The hot-path harness measures `health()` and live reads under engine contention, with
  same-host baselines in [benchmarking.md](benchmarking.md). The remaining work is to measure
  `engine_tick` allocations under representative capture load and publish a comparable
  baseline. The [decision below](#decided-not-to-build-2026-09-22) rules out performance
  ceilings in hosted CI; do not add a threshold gate.

- **4.5 — Optional encryption at rest.** `proposed` `M`
  Optional SQLCipher for the local DB. *(The schema-versioning half of this item was split
  out and promoted to 7.3.)* [ADR-0009](adr/0009-local-first-threat-model.md) does not
  require encryption for v1; revisit only if the threat model changes.

- **4.12 — Finish static-analysis coverage.** `in progress` `M`
  `.clang-format` and the frontend Prettier config are in place for added files; existing
  files remain advisory to avoid an unrelated formatting diff. Frontend ESLint runs in CI,
  while `.clang-tidy` supplies editor checks but is not a CI gate. The remaining decision is
  whether a scoped C++ `clang-tidy` run adds enough signal to justify its build time and
  existing warning backlog. See [CONTRIBUTING.md](../CONTRIBUTING.md) for the current policy.

---

## Tier 9 — Ship a v1 (release readiness)

- **9.4 — Walk the installed upgrade path.** `proposed` `M`
  The ZIP-copy defect was fixed and NSIS now supplies the Windows installer; CI package
  validation does not prove install or upgrade behavior. Use published **v0.3.0** as the
  baseline, not the orphaned, unpublished v0.2.0 tag. Install it, create recognizable data,
  then upgrade to the candidate and verify migration, settings, autostart path, and
  replacement of a running instance. Add an install smoke; also exercise the ZIP helper
  for ZIP users. See [Packaging](PACKAGING.md); retain `focoflow.db` compatibility.

- **9.5 — Wire uninstall to the implemented purge policy.** `in progress` `S`
  The accepted policy removes app-owned data, settings, logs/rotations, exports, models,
  database companions/backups, and start-on-login registration. `src/app/uninstall.hpp`
  and `snapback --purge` implement it, report partial failure, preserve unrelated files,
  and remove the data directory only when empty. An empty target path is an error.
  Remaining: invoke purge from the NSIS uninstaller before deleting the executable;
  macOS/Linux removal integration follows **3.3/3.4**. Confirm partial failures are not
  reported as clean removal. This is distinct from Delete all activity, which keeps
  settings, the model, and the database for the still-running application.

- **9.6 — Make runtime failures visible and recoverable.** `in progress` `M`
  **Progress 2026-10-01:** Capture warm-up now has a 90-second frontend deadline with a Privacy-settings diagnosis, while health checks continue until genuine input confirms the running listener. Permissions distinguish listener-running from input-confirmed. The walkthrough now has a reachable capture-check step during blocked capture, displays its recovery guidance, and exposes Finish at the final step. Idle replay starts a fresh observed journey without reusing the prior recap. Focused deadline and onboarding checks passed; live desktop permission recovery remains unverified.

  **Progress 2026-10-01:** Native UI dispatch now preserves recorder-wide failure/recovery events across session deletion/replacement while rejecting stale activity alerts. The production dispatch filter is shared with lifecycle regression tests. Final full verification passed (770 native tests, 220 frontend component tests plus pure-unit/typecheck/lint/build/guards); event-routing ASan/UBSan and ThreadSanitizer checks passed. Live Windows/macOS recovery and Windows Release soak remain unverified.

  **Progress 2026-10-01:** Final review follow-up: saving warnings remain visible until authoritative recovery; capture/offline status keeps precedence. Unsaved snapback episodes survive alert dismissal and cannot be replaced by newer drift during backoff; session deletion invalidates them. Preview predictions are excluded from recording-loss counters. Real SQLite contention and page-limit exhaustion confirm database_busy/disk_full categories. Full verification passed (768 native cases plus frontend and guards); expanded ASan/UBSan and ThreadSanitizer runs each passed 71 focused cases without findings after correcting the maintenance test clock. Live Windows/macOS capture/recovery and release soak remain unverified, so the item stays in progress.

  **Progress 2026-10-01:** Native failure/recovery events, health degradation, capped exponential backoff, discarded-prediction counters, and retained snapback episodes are implemented. Attendance retains its original boundary through backoff; frontend health rejects stale reads. Live Windows/macOS recovery and the Release soak remain unverified; broader capture/permission UX remains open.

  Attendance transitions survive failed transactions: desired and committed attendance
  are separate, ordered transitions retain their original boundary across retries, and
  dead-session transitions are discarded. Snapback emission is acknowledged after
  persistence. Native tests cover SQLite contention and injected begin/write/commit failures.
  Remaining: designed UI responses for revoked permissions, dead hooks, disk-full or
  locked-database failures, and stale predictions. Persistence failures are logged but
  lack durable health degradation and retry/backoff policy. The planned persistence-failed
  payload and active subscription exist in the frontend; the native emitter remains absent.
  Add truthful health state, native delivery and UI consumption, bounded retries, and
  disk-full/locked failure tests. Coordinate capture diagnostics with **7.4/7.10**.

---

## Tier 10 — Frontend & UX

- **10.1 — Complete real-webview acceptance coverage.** `in progress` `L`
  Windows and macOS smokes cross the injected shim and real `webview.bind()` for health,
  Settings/summary casing and defaults, session start/stop/history, async export, and an
  error envelope (`scripts/gui_acceptance.js`). The harness requires an explicit build
  option; ordinary Debug/Release builds cannot enable it via an environment variable.
  Windows also drives Review, Settings, Start, and Stop through WebView2 CDP without a
  browser download. Native registry tests cover handlers and worker policy separately.
  Remaining: WebKitGTK-driven clicks, adversarial optional-model startup cases (**13.8**),
  and a deliberately slow job proving UI heartbeat responsiveness (**14.6**). WKWebView
  remains on page-side acceptance because it exposes no equivalent automation endpoint.
  Preserve structured verdicts, bundle-load validation, and clean run-loop exit.

- **10.3 — Complete accessibility assessment.** `in progress` `M`
  Permission-wizard focus containment/restoration and scrolling, Settings panel ownership,
  chart data tables, spoken prediction/risk labels, and goal combobox semantics are
  implemented and tested. Demo zoom/reflow was checked on 2026-09-23 at 550×380 and
  320×640; this does not establish native overlay behavior.
  Evidence: `frontend/tests/permissionWizardFocus.test.tsx`,
  `frontend/tests/settingsNavFlow.test.tsx`, `frontend/tests/chartDataTables.test.tsx`,
  and `frontend/tests/predictionHistoryLabels.test.tsx`.
  Remaining: native overlay focus, reduced-motion and OS contrast on Windows/macOS;
  decide a visible non-colour cue for the risk level. Spoken labels alone do not address
  sighted users who cannot distinguish the chip colours. Coordinate contrast with **10.10**.

- **10.6 — No C++ coverage measurement at all.** `proposed` `M`
  The frontend can measure coverage; the C++ side cannot. Given how many bugs in Tiers 5/7
  were "the tests never exercised the production branch" (`seconds_since_session_start`, 7.1,
  5.3), a coverage report is the cheapest tool for finding the next one. `gcov`/`llvm-cov` on
  the Linux CI job.

- **10.10 — Complete appearance verification.** `in progress` `M`
  Semantic colour/control/chart/focus tokens and persisted System/Light/Dark appearance
  exist. The CSS guard rejects undefined tokens, colours outside the token area, and
  duplicate selector lists. Review leads with Summary and Sessions; live predictions are
  Advanced diagnostics, and selected-session context belongs in the session detail.
  Historical browser and Windows desktop walkthroughs are preserved in the archive;
  their screenshots and test counts are dated evidence, not current verification.
  Remaining: automated light/dark snapshots for Now, Review, Settings, and the native
  overlay; comprehensive contrast assertions with **10.3**; and native window/overlay
  smoke verification after the soak. Preserve ADR-0003's palette and surface assignments.
  Keep repository-wide formatting and AppState/App composition refactors separate.

- **10.11 — Finish Review interval semantics and completeness.** `in progress` `M/L`
  Shared rolling presets, custom local-midnight conversion, loaded-range provenance,
  stale/load/failure messages, Retry, and request-generation protection exist. The UI
  shows the 500-session cap; live predictions moved to Advanced and context is per-session.
  Evidence: `frontend/src/useReviewWorkflow.ts`, `frontend/src/reviewRange.ts`, and their
  workflow/time-zone tests. Preserve the older-response guard and loaded interval labels.
  Remaining from the original acceptance: settle rolling versus calendar windows (**7.2**)
  and eliminate range/session truncation with **2.9** rather than hiding caps. Calendar
  attendance and rolling aggregates must retain explicit, truthful scopes. Implement
  further invalidation through **14.4** and check command result shapes under **14.3**.

- **10.14 — Use native destination dialogs for exports.** `in progress` `M`
  Owned Open/Save adapters over Win32 and AppKit exist; restore uses Open. Personal,
  summary, and support exports still choose app-data folders and return paths.
  Remaining: Save As for all three exports, ordinary cancellation, platform overwrite
  confirmation, filters/extensions, sibling temporary writes with atomic publication,
  and Reveal/Copy path after success. Keep the private default for internal workflows.
  Native code retains dialog/filesystem authority; expose no unrestricted path API across
  **8.14**. Test cancel, overwrite refusal, Unicode/long paths, read-only destinations,
  extension normalization, and owner-window disappearance. Run real Windows/macOS owner
  smokes and coordinate long exports with **9.16/14.6**.

---

## Tier 12 — Documentation truth

- **12.6 — Global label hotkeys remain unimplemented.** `proposed` `M`
  No native code registers an OS-global label shortcut. Dead frontend label-hotkey
  listeners were removed when **11.13** brought events into the IPC contract; do not
  mistake old notification/listener scaffolding for a working feature.
  A future implementation needs per-OS registration, prediction/label scope, and native
  event/fixture/frontend agreement. ADR-0002 excludes this from the v1 blocker list;
  revise accepted scope explicitly before treating it as a release requirement.

---

## Tier 13 — Model lifecycle (breaking down 2.3)

**2.3 was one `L` item that hid at least seven.** The deployment identity, quality gate, and
rollback are complete as 13.1–13.4 in the [archive](roadmap_archive.md). The three unresolved product decisions
below still determine whether, where, and how the retraining loop should operate.

- **13.5 — Establish whether labelled data justifies training.** `proposed` `S` `decision`
  Explicit submissions and automatic session labels are not independent ground truth.
  Lifecycle labeling was repaired under **7.25**; editable/effective-label semantics remain
  **2.17**. Before expanding **2.3**, measure labels per typical week and class balance,
  define label scope/conflicts, and distinguish automatic from adjudicated labels.
  Coordinate **13.6** and **FWD-07** as one evaluation gate: identity-pinned, session/time-
  separated held-out data, heuristic/majority/candidate comparisons, per-class metrics,
  false interruptions, predeclared improvement/non-regression limits, and runtime cost.
  Replay must respect focus_momentum rather than reuse baseline-generated feedback for
  every candidate. Sparse/skewed data should rescope the loop to collection, not retraining.

- **13.6 — Define what happens when the model and the heuristic disagree.** `proposed` `S` `decision`
  5.1 established that the classifier blends model probabilities with rule/thrash/drift
  signals. Nobody has specified what *should* win, or how to tell when the model has drifted
  far enough from the heuristic to be distrusted.

  **ADR-0004 narrowed this, and it is now a smaller question than it was.** Model-vs-*policy*
  is settled: the scores are the model's opinion, `focus_state` is the verdict, and policy
  may only demote. What remains is model-vs-*heuristic* — two producers of the same opinion
  channel — which is a calibration question, not an authority one. `state_source` is the
  instrument for it: it records which rule bound each verdict, so "how often does policy
  overrule the model, and on what" is now a query rather than a study.

  Once behavior is settled, localize ownership too: `Classifier` reaches into the process-wide
  `OnnxModel::instance()`, while `AppState` separately loads it and reads its identity, and
  tests need cleanup guards for leaked singleton state. Put model lifecycle behind an owned
  classifier adapter without changing the chosen policy. Do not make that seam change first;
  it would disguise a behavior decision as architecture cleanup.

- **13.8 — Prove optional-model recovery through the real webview.** `in progress` `S/M`
  Startup degrades instead of exiting on recovery failure; health retains preserved paths
  and Diagnostics offers Retry cleanup and Reveal files. Rollback and cleanup are consumer
  recovery, not developer-only model production (ADR-0006).
  Remaining with **10.1**: drive corrupt markers, unremovable staging, committed-but-locked
  cleanup, invalid models, and clean retry through real startup. Prove storage, capture,
  Review, and heuristic predictions remain available; diagnostics must name preserved paths
  without leaking contents. Never delete the only candidate/previous model just to start.
  Keep failure-atomic rollback and model-file exclusion coverage from Tier 13/14.6.

---

## Additions to existing tiers

- **9.10 — Retention deletes the data analytics depends on, and the user has no say.** `proposed` `S`
  `decision`
  The 90-day prune is hardcoded (`storage.hpp:kDefaultRetentionDays`) with no setting exposed. Two tensions
  nobody has resolved: a user who wants year-over-year trends silently can't have them, and
  a privacy-focused user who wants a 7-day window can't have that either. The value
  proposition ("see your focus patterns") and the privacy promise ("we don't keep it
  forever") point opposite directions, and the constant currently arbitrates. Make it a
  setting, and decide the default deliberately. Ties to **7.6** and **8.5**.

---

## Tier 14 — Architecture leverage (2026-08-01 and 2026-08-05 deep-module scans)

These are not “large file” complaints. Each item identifies a shallow seam where callers
must understand implementation details. Only work with a concrete acceptance boundary is
kept here; already-deep modules and completed performance work were rejected during the scan.

- **14.2 — Make one synchronous engine cycle the production test seam.** `proposed` `M`

  `engine_tick()` owns the real drain → idle/pomodoro → compute → persist → emit sequence,
  while tests and both benchmark targets include `tests/app_state_test_access.hpp` to reach
  three narrower private methods. Deleting that friend seam would force tests back to threads
  and sleeps; its forwarding interface is shallow because it exposes which internals to call.

  Extract a deterministic engine-cycle module whose `step` returns persistence jobs, emitted
  events, and updated live state. The production thread and tests must call the same step;
  persistence and UI dispatch remain adapters outside it. Delete the three private test
  methods and remove the benchmarks' dependency on the `tests/` include path. This is the
  concrete completion path for 7.14 and the remaining testability half of 11.4.

- **14.3 — Complete argument/result contract coverage.** `accepted` `M`
  `src/app/command_registry.hpp` owns names, real handlers, and worker policy;
  `src/app/command_handlers.cpp` registers them and `src/app/commands.hpp` binds the table.
  Native tests invoke real handlers by name, pin capability checks/worker policy, and
  compare names to the fixture. Settings, recording, analytics, summary, and history
  already have representative response-key and default-argument assertions.
  Remaining: cover the other mapped commands' argument defaults, validation, and result
  casing against frontend DTOs/mappers. Extend per-command registry tests rather than
  generating the IPC contract from the fixture (a rejected proposal). Real transport
  acceptance remains **10.1**; the two layers complement each other.

- **14.4 — Finish frontend workflow ownership and invalidation.** `in progress` `M`
  **Progress 2026-10-01:** Recording pause controls now name what they pause, with explicit elapsed-versus-attended copy and private/idle session labels. Demo predictions and attendance stop during private pauses, timed resumption counts only post-expiry time, and repeated Stop preserves the original end timestamp. ADR-0005 elapsed semantics remain intact; native desktop interaction still needs live confirmation.

  **Progress 2026-10-01:** Pomodoro countdown now advances between authoritative native snapshots, freezes while paused or awaiting acknowledgement, and rejects stale status reads after newer timer events. Demo phase transitions, skip/restart/acknowledge, long-break cadence (including intervalsBeforeLongBreak 0), and session-bound resets follow the native state machine. Review demo "Session time" uses completed-session wall-clock duration instead of prediction-count estimates, and all-time/custom ranges no longer invent a planned total. Desktop Pomodoro timing under suspension remains a live-check gate.

  **Progress 2026-10-01:** Session Control and the header Session status chip share one `{label, reason}` model (`sessionStatusView`). Private/idle/no-input pauses use "Paused — …" wording with an elapsed-vs-attended reason; the Now headline uses that label instead of inventing "Session in progress". Surface and unit tests assert both chips match for running and private pause.

  **Progress 2026-10-01:** Live "Today" attended minutes now tick locally between `get_attended_progress` baselines (60s rebaseline, plus idle/recording transitions). Under one minute while accruing shows `<1m` so the compact line does not look stuck at `0m`; idle/private freezes the offset.

  **Progress 2026-10-01:** Session switch is stop → check-in/discard → start; fragments are excluded from Review totals while remaining visible in Session Explorer.

  Review hydration is already active-surface gated, with inactive-call assertions in
  `frontend/tests/reviewWorkflow.test.tsx`; recording/privacy and session transitions have
  received narrow correctness repairs. Do not rebuild those paths or deleted hooks.
  Remaining: Now/Review/Preferences workflows own subscriptions, refresh consequences,
  and failures rather than App manually coordinating unrelated stores. Preserve ADR-0003.
  Pin action-level invalidation and command counts: zero unused Review/Settings hydration
  on Now, one fetch on first entry, cached re-entry until invalidation, in-flight deduplication,
  and no older response overwrite. Context-history reads require a visible consumer; the
  teaching card counts, so hidden Review alone is insufficient. Prefer persisted-context
  invalidation to querying on every prediction. This yields to release blockers.

- **14.5 — Make engine wakeups deadline-aware.** `in progress` `M` `performance`
  **Progress 2026-10-01:** The idle harness now reports engine-thread CPU separately from whole-process CPU on Windows/Linux. A serial same-host 60-second run measured 540 to 2 ticks and 15 ms to below-resolution engine CPU; whole-process CPU varied, so no process CPU gain is claimed. Component instrumentation calibration is documented separately and does not establish the less-than-1% whole-cycle overhead gate. Full verification and 72-case sanitizer runs passed; live desktop, statistical latency, and Release soak gates remain open.

  **Progress 2026-10-01:** Implemented coalesced C++20 semaphore notifications, authoritative deadline rechecks for idle/Pomodoro/privacy/snooze/nudges/retry/maintenance, producer-first shutdown, and maximum-drain diagnostics. The MinGW runtime uses an owned blocking event for timed waits after measurement exposed spinning. Full local verification passed (759 native cases plus frontend and guards); focused wake/timer/shutdown checks passed. Same-host 60s quiet ticks fell 548 to 2, paced-input p95 107975 to 271 us. These single headless runs exclude OS capture/GUI cost; engine-only CPU, statistical latency, less-than-1% instrumentation overhead, live platform checks, release soak remain open. Focused Linux ASan/UBSan and ThreadSanitizer checks passed (29 cases, 3014 assertions each); TSAN used a process-local ASLR workaround. No broader cycle refactor is required; item remains in progress.

  Drain work is already bounded by `state.hpp:kEngineDrainBudget` and
  `state.hpp:kEngineDrainBudgetMs`, with immediate backlog re-ticks. Shutdown joins the
  producer before empty-queue completion and drains healthy slices; consecutive failures
  retain a bounded exit. Runtime lock, busy-wait, wakeup, and ring counters exist.
  Remaining: wake on capture, stop/lifecycle requests, and idle/Pomodoro deadlines;
  expose any missing maximum-drain metric and prove timers/stop cannot starve. Build with
  or after **14.2**, preserving event order and no-session previews (P0-08/ADR-0005).
  Measure same-host idle CPU/wakeups and event-to-prediction p95 before/after, with <1%
  instrumentation overhead. A quiet minute should run deadline-required cycles rather
  than about 600 polls. The [measured baseline](benchmarking.md#measured-budgets) excludes
  real input-hook cost; do not infer whole-product idle CPU from it.

- **14.6 — Prove worker responsiveness and cancellation in the UI.** `in progress` `L`
  Training and exports already use CommandRegistry worker policy. The owned subprocess
  API uses argv, Job/process-group ownership, cancellation, reaping, and shutdown joins.
  Training is exclusive, privacy deletion is fenced, and progress arrives from the log.
  Model promotion/reload/rollback/cleanup share an exclusion gate; rollback promotes a
  journaled model/metadata pair and inference fallback preserves heuristic provenance.
  **Recorded decision:** completion stays on the command promise with progress/cancel
  events. Do not replace this with job IDs merely for development webview reloads.
  Remaining with **10.1**: a deliberately slow fake worker must leave the real UI heartbeat
  responsive; cancellation and shutdown must leave no child/worker/callback alive beyond
  AppState. Fast commands stay synchronous. Revisit job IDs only if exports demonstrate
  the need; preserve explicit concurrency policy and test real child termination.

- **14.7 — Remove retention/reclamation from launch latency.** `in progress` `M` `performance`
  **Progress 2026-10-01:** Late or duplicate frontend readiness is fenced by the maintenance control lock after shutdown; a regression test verifies it cannot leave queued work. Full headless verification passed (769 native tests), and the expanded 72-case ASan/UBSan and ThreadSanitizer runs passed. Live desktop startup and reclamation gates remain open.

  **Progress 2026-10-01:** Partially implemented: database opening no longer runs ordinary retention DELETE or automatic VACUUM. React acknowledges its first painted view after two frame boundaries through idempotent notify_frontend_ready (78-command contract, API and demo updated). The owned 256-row maintenance worker releases storage between batches, pauses before queued session starts, reports pending/running/result and cumulative rows/latest elapsed time, retries failures after 30s, and cancels/joins on shutdown. Targeted readiness/retention/IPC checks and full local verification passed (762 native cases plus frontend and guards). Focused ASan/UBSan and ThreadSanitizer checks passed. Same-host simplified aged-fixture database-open p95 improved 96.1%/98.9%; representative desktop startup-to-ready measurements and the 80% UI gate remain open. Background DELETE may not shrink the file; automatic reclamation policy is deferred, so this item remains partially complete and in progress.

  Periodic maintenance already deletes bounded batches on an owned maintenance thread,
  yields/pauses for active sessions, and does not VACUUM. Schema v8 includes
  idx_feature_snapshots_ts. Startup Storage::open still prunes and may VACUUM before the UI.
  Remaining: measure month/90-day launch-to-visible; keep lock/schema/migrations synchronous
  but schedule ordinary pruning/reclamation after first paint. Use bounded, indexable
  deletes and measured freelist/byte thresholds rather than a 500-row full-VACUUM trigger.
  Reuse **14.5/14.6** ownership; session start must yield/cancel maintenance.
  Acceptance: no pre-window full VACUUM, ≥80% same-host mature-fixture p95 improvement,
  no out-of-policy rows, write latency within its benchmark bound, safe crash/resume,
  and diagnostics for maintenance time/result/pending reclaim bytes. Align with **9.10**.

- **14.8 — Decompose AppState along its lock boundaries.** `in progress` `L`
  Concurrency follow-ups exist, but concern extraction remains. Extract types owning their
  state in this order: Pomodoro, alert policy, storage-only reporting, session lifecycle,
  then the tick as **14.2**'s deterministic seam. Reporting extraction adds no second
  connection: **14.1** closed with writer priority.
  Acceptance per extraction: test the new type independently; preserve ranked-lock order
  and TSan/RankedMutex checks; retain app-state tests except construction changes; bundle
  no behavior change. Land any extraction approaching 600 diff lines before the next.
  Settings fsync and ONNX reload remain under the state lock until deliberately extracted.
  This is no prerequisite for other work and yields to user-facing failures.

- **14.12 — Avoid repeated Review aggregates if measurement justifies it.** `proposed` `S` `performance`
  Analytics, summary_report, and focus_summary_for_window independently call
  `storage.cpp:Storage::prediction_stats` for one Review window. After sargable predicates,
  the corrected 2026-09-22 ceiling fixture measured three calls at ~165 ms for day and
  ~1.3 s of a 2.55 s 7-day load. See [measured budgets](benchmarking.md#measured-budgets).
  Remains proposed: cutoffs differ by milliseconds, so exact-key memoization misses and
  coarser keys change interval semantics. Any sharing needs a common cutoff and explicit
  invalidation. Writer priority (**14.1**) prevents a persist waiting out a whole load;
  the remaining cost is Review latency. Preserve **7.33**'s correct SQL focus stretch.

---

## Audit direction references

These IDs remain search aliases for the historical audit, not an additional queue.
Current status and acceptance belong to the named live item or completed record.

| Direction | Current owner or disposition |
| --- | --- |
| FWD-01 | Windows v0.3.0 is released; signing remains **0.4b**. |
| FWD-02 | Session-story work lives with **2.9/10.11**; a wider weekly digest remains an unscheduled proposal from the archived review. |
| FWD-03 | Developer retraining is **2.3**, gated by **13.5/13.6** and ADR-0006. |
| FWD-04 | Recovery UI must preserve **4.11**'s title-parser decision gate and **2.9**'s stored session evidence. |
| FWD-05 | Alert routing, quiet hours, snooze, and preview delivery landed under **2.16**; broader notification budgeting remains an unscheduled audit proposal. |
| FWD-06 | Update checks are **3.5**, gated by an ADR changing network silence; winget is a historical, unscheduled rider contingent on signing. |
| FWD-07 | Conditional offline evaluation gate with **13.5/13.6**; no automatic model-changing milestone. |
| FWD-08 | Deadline-aware engine work is **14.5**. |
| FWD-09 | Headless verification is `scripts/verify.py`; a running desktop dev loop uses the existing Windows runbook. A cross-platform one-command live loop remains unscheduled. |
| FWD-10 | Binding exclusions remain [below](#decided-not-to-build-2026-09-22); archived recommendations do not amend ADRs. |

## Decided not to build (2026-09-22)

Recorded so the next time one of these is proposed the answer is a paragraph read, not a
debate. Each was checked against the code before being rejected, and "a comparable product
has it" was not treated as evidence. Deferred is different from rejected: a day-timeline lane
on Review is deferred, not rejected — **7.33** has supplied a correct stretch computation; further timeline
work still needs an explicit scope decision with **2.9/FWD-02**, rather than a second Review redesign.

- **Blocking apps or sites.** Snapback reflects; it does not block. The principle is now
  stated in [`ARCHITECTURE.md`](ARCHITECTURE.md); a Block rule forces the verdict
  ([ADR-0004](adr/0004-verdict-and-opinion.md)) and never touches the window. A proposal to
  add blocking is a proposal for a different product and needs its own ADR.
- **Full-text search (FTS5) over context history.** FTS5 is not compiled in —
  `CMakeLists.txt` builds the amalgamation with no `SQLITE_ENABLE_FTS5` — and titles live only
  in `context_snapshots`, written on foreground change plus a 30-second checkpoint, so the
  table is thousands of rows a week, not millions. If search is ever wanted it is a `LIKE`;
  the "return to what you were doing" promise is already the snapback restore target.
- **A localhost HTTP API or MCP server.** A loopback listener is still a listener; **8.10**
  and [ADR-0009](adr/0009-local-first-threat-model.md) promise a release build that opens no
  sockets. The honest alternative is the documented schema plus the existing exports
  (**9.14**, **9.16**). Reversing this is a new ADR with a threat-model delta, not a feature.
- **Compacting consecutive identical predictions into runs.** The premise was per-event
  persistence; the cadence is at most one row per second while attended (**14.11**).
  `feature_snapshots` is written 1:1 with `predictions` and is the training export, and
  `focus_momentum` feeds the model from stored scores
  ([ADR-0004](adr/0004-verdict-and-opinion.md)), so runs would discard exactly what the model
  consumes.
- **Auto-update, or evaluating a packager for it, ahead of an ADR.** An update check is a
  network call. Until an ADR amends the network-silent rule the evaluation has nothing to be
  measured against; revisit when FWD-06 is scheduled.
- **Reading browser URLs through UI Automation before 8.11 exists.** URLs are more sensitive
  than titles, the read is per-browser-version brittle, and the domain field's value is
  asserted, not shown. Not before scoped capture rules can redact what it would capture.
- **A fourth, always-on-top "Now" surface.** [ADR-0003](adr/0003-three-surface-dashboard.md)
  fixes three surfaces; the overlay already owns the glanceable moment the product needs.
  There is no evidence users keep the full window open to watch the hero.
- **Generating the IPC contract from the fixture.** `tests/test_ipc_contract.cpp` already
  checks registry equals fixture and frontend invokes are a subset; **14.3** covers the
  remaining shape drift. A generator adds a build step to save two type declarations per
  command.
- **Performance ceilings asserted in CI.** Hosted runners are too noisy and the harness
  measures in-memory SQLite. Publish numbers (**14.11**); do not gate on them.
- **Auto-proposed or auto-detected sessions.** Decided in
  [ADR-0005](adr/0005-a-session-is-declared-and-attended.md); **2.7**'s untracked-work nudge
  asks and never starts a session. Reopen only with new evidence, and as a decision.
- **Issue and pull-request templates.** Sole-author repository; CI already enforces the one
  contribution rule that matters. `SECURITY.md` was the part worth doing (**9.18**).

---

## Recurring health checks

Checks to run on a cadence, not one-off tasks. Several are automatable; where so, that's
itself a backlog item below.

### Before every release

- [ ] Open a **pre-existing** `focoflow.db` written by an earlier install and run a
      full session end-to-end. The 7.11 fixture corpus now makes this directly runnable.
- [ ] Kill the process uncleanly mid-session, restart, confirm WAL recovery and that the
      orphaned `ACTIVE` session resumes (`state.cpp:AppState` claims to handle this — verify it).
- [ ] Run a session on each OS long enough to exceed the ring buffer under load, and confirm
      `capture_events_dropped` reflects reality; 7.4/7.17 expose the signal, this validates
      it under a real desktop workload.
- [ ] Confirm every `invoke(...)` string in `frontend/src/api.ts` resolves in
      `command_handlers.cpp`. The existing contract test validates native registry names,
      fixture equality, and frontend calls; **14.3** covers remaining argument/result shapes.
- [ ] Confirm the release tag equals CMake's version, names a commit reachable from protected
      `master`, and carries the full CI result required by 9.11.
- [ ] Extract every release artifact and verify the project license, dependency notices,
      frontend bundle, executable signature where required, and launchable binary are inside.

### Monthly, or when a subsystem is touched

- [ ] **Ghost-item sweep.** For each item claiming something is missing, grep first —
      including non-`.cpp` extensions. For each Done-archive item, confirm the code has a
      **caller**. This has found real ghosts twice (0.3, 2.4); assume it will again.
- [ ] **Dead-code sweep.** Every `.hpp` in `src/` should have a caller outside its own test.
      `confidence.hpp` was the known offender and is now deleted (5.3, ADR-0004); check for
      siblings.
- [ ] **Unit sanity sweep.** Grep thresholds and confirm each matches its producer's scale.
      5.3 shipped `[0,100]` logic against a `[0,1]` producer and the tests passed because they
      fed values the system never emits.
- [ ] **Default-build coverage.** Confirm what sits behind `SNAPBACK_ONNX` /
      `SNAPBACK_BUILD_APP` and is therefore unexercised by the default build. The 5.2 fix
      lives inside an `#if` only one CI job compiles — a standing risk, not a one-time note.
- [ ] **Stack-size sweep.** Grep for large by-value members (6.1). Anything over ~64 KB per
      object is a Windows landmine.
- [ ] **Fresh-clone sweep.** Run the doc-path guard and the frontend build from a *clean*
      clone, not the working tree. **2026-07-24:** `scripts/check_doc_paths.py` was green
      locally and red in CI on its first run, because this working tree still has
      `frontend/dist` from earlier builds while a fresh checkout does not. A guard that only
      passes on a developer's machine guards nothing. `git clone . /tmp/x && cd /tmp/x` is the
      whole check.
- [ ] Re-run the feature-parity golden test. Any `features.cpp` change without a matching
      golden update is a CI failure waiting to happen (5.6).

Automated checks already cover hostile titles, stack-size bounds, and dead headers;
see [Testing strategy](testing_strategy.md). Their original implementation notes are archived.

### Candidates for new CI jobs

- [ ] **Schema-drift job:** diff the current `CREATE TABLE` statements against a checked-in
      snapshot and fail on divergence. Guards the 7.3 compatibility promise directly.
- [ ] **Scale job:** seed a month of synthetic usage; assert analytics/summary return correct
      counts inside a time budget. Would have caught 7.1; guards 7.12.
- [ ] **Health-truthfulness job:** force each failure mode (dead hook, over-broad exclusion,
      persistence failure, no session) and assert `HealthStatus` reports something other than
      healthy. Capture/prediction fields are unblocked by 7.4 and 7.10; persistence waits on
      9.6. The point is that health fields must never be literals again.


---

## Completed work

**6.3** is complete: desktop jobs run independently of headless failures; the original
2026-07-25 CI confirmation and X11 reasoning are retained in the archive.

Everything finished lives in [`roadmap_archive.md`](roadmap_archive.md), with its original
wording, ID, and date. Search that file for the ID. The index below exists so this file can
still answer "is 9.15 done?" without opening it.

| Item | Item | Item |
| --- | --- | --- |
| 0.1 | 5.9 | 9.3 (2026-08-04) |
| 0.2 | 6.1 (2026-07-22) | 9.7 (2026-07-26) |
| 0.3 (2026-07-25) | 6.2 (2026-08-27) | 9.8 (2026-07-26) |
| 0.4 | 6.4 (2026-07-22) | 9.9 (2026-07-26) |
| 0.8 | 6.6 (2026-08-06) | 9.11 (2026-08-04) |
| 1.1 | 7.1 (2026-07-22) | 9.12 (2026-08-27) |
| 1.2 (2026-08-03) | 7.3 (2026-07-29) | 9.13 (2026-08-27) |
| 1.3 | 7.4 (2026-07-22) | 9.14 (2026-08-11) |
| 1.4 | 7.5 (2026-07-22) | 9.15 (2026-08-27) |
| 1.5 | 7.6 (2026-07-30) | 9.16 (2026-08-06) |
| 1.6 | 7.7 (2026-08-03) | 9.18 (2026-09-22) |
| 2.1 | 7.9 (2026-07-22) | 10.2 (2026-07-25) |
| 2.2 | 7.10 (2026-07-22) | 10.4 (2026-07-26) |
| 2.4 | 7.11 (2026-07-31) | 10.5 (2026-07-26) |
| 2.5 | 7.12 (2026-08-06) | 10.7 (2026-07-26) |
| 2.6 | 7.13 (2026-07-22) | 10.8 (2026-08-06) |
| 2.7 (2026-08-10) | 7.14 (2026-07-31) | 10.9 (2026-08-11) |
| 2.8 (2026-08-14) | 7.15 (2026-07-31) | 10.12 (2026-08-11) |
| 2.10 (2026-08-10) | 7.16 (2026-08-24) | 10.13 (2026-08-06) |
| 2.11 (2026-08-10) | 7.17 (2026-07-26) | 11.1 (2026-07-31) |
| 2.12 (2026-08-11) | 7.18 (2026-08-03) | 11.2 (2026-07-31) |
| 2.13 (2026-08-10) | 7.19 (2026-08-04) | 11.3 (2026-07-31) |
| 2.14 (2026-08-10) | 7.20 (2026-08-04) | 11.4 (2026-07-31) |
| 2.15 (2026-08-06) | 7.21 (2026-08-07) | 11.5 (2026-07-31) |
| 2.16 (2026-08-27) | 7.22 (2026-08-05) | 11.6 (2026-07-31) |
| 2.19 (2026-08-12) | 7.23 (2026-08-06) | 11.7 (2026-08-04) |
| 3.0 (2026-07-30) | 7.24 (2026-08-05) | 11.8 (2026-08-04) |
| 3.1 (2026-07-28) | 7.25 (2026-08-06) | 11.9 (2026-08-08) |
| 3.6 (2026-08-27) | 7.26 (2026-08-06) | 11.10 (2026-08-08) |
| 4.1 | 8.1 (2026-07-22) | 11.11 (2026-08-07) |
| 4.6 | 8.2 (2026-07-26) | 11.12 (2026-08-27) |
| 4.7 | 8.3 (2026-07-22) | 12.1 (2026-07-23) |
| 4.8 | 8.4 (2026-07-22) | 12.2 (2026-07-23) |
| 4.9 | 8.5 (2026-08-27) | 12.3 (2026-07-23) |
| 4.10 | 8.6 (2026-07-31) | 12.4 (2026-07-23) |
| 4.13 (2026-08-08) | 8.7 (2026-07-26) | 12.5 (2026-07-23) |
| 5.1 | 8.8 (2026-08-04) | 12.7 (2026-08-08) |
| 5.2 | 8.9 (2026-08-04) | 12.8 (2026-09-22) |
| 5.3 (2026-08-03) | 8.10 (2026-08-05) | 13.1 |
| 5.4 (2026-08-03) | 8.12 (2026-08-06) | 13.2 |
| 5.5 (2026-08-24) | 8.13 (2026-08-07) | 13.3 |
| 5.7 | 9.1 (2026-07-25) | 13.4 |
| 5.8 | 9.2 (2026-07-22) | 13.7 (2026-08-07) |
|  |  | 14.1 (2026-09-22, writer priority) |
|  |  | 14.11 (2026-09-22) |
|  |  | 14.13 (2026-09-22) |
|  |  | 14.14 (2026-09-22, not built) |
