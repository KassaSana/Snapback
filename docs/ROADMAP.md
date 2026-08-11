# ROADMAP.md — the single source of truth for what to build next

**This file is the live backlog.** Every open task lives here. Completed work moves to the
[Done archive](#done-archive) or remains marked **DONE** only when its explanation still
teaches a useful constraint. This is the source of truth; when another status document
disagrees with this one, this one wins.

**There is no second backlog.** `docs/TODO.md` used to carry open items and drifted out of
sync — it tracked `2.4b` as a task while this file correctly tracked the same work as the
decision in 5.3. It was **deleted** on 2026-07-20; its history is in git and its `[x]`
entries duplicated the [Done archive](#done-archive) below. Don't reopen a parallel list.

**Last audited against the code: 2026-08-05.** The July 31 hardening pass added ranked lock
ordering, immutable dependency pins, per-case CTest registration, classifier properties,
the large storage fixture, injected clocks, and private test seams. The August 1 performance
pass moved hot live reads to an immutable snapshot and added contention/lifecycle coverage.
The August 5 pass was read-only and did not rerun the suites; it audited production capture,
session lifecycle, reporting, training, and the full frontend composition. PR #40 earlier ran
the merged hardening baseline through all **15 hosted CI jobs**; all passed, including
`macos-gui-smoke`.

**The August 6 pass closed ten items in two batches.** First the release correction queue —
**7.23, 7.25, 7.12, 10.8, 6.6** — minus the decision it deliberately did not take. Then five
more from the audit batches: **2.15, 8.12, 7.26, 9.16, 10.13**. The local baseline is now
**400/400 C++ cases** (up from 336 after 7.22) and clean frontend unit scripts plus typecheck;
the component suite still cannot run on this machine (**11.11**), so every frontend change went
into a `tsx`-testable pure module rather than into a component. CI is now **16 hosted jobs**
with 6.6's `windows-gcc`, whose configuration was reproduced locally before being turned on.

**Three defects in that pass were found by a test rather than by reading**, which is worth
recording because each was a plausible-looking wrong number rather than a crash. 10.13's SQL
credited the time spent *being distracted* to the focused run that followed, reporting every
stretch as exactly twice its length. The same parity check caught focused runs walking across
concurrent sessions. And 8.12's first classification moved support bundles into the delete set;
an existing case asserted otherwise, and on inspection the existing decision was the coherent
one. The pattern this file already records — check the claim before rebuilding around it —
applies to one's own new code too.

The formal v1 blocker list is **five of six verified complete**. Decision session A was
settled on 2026-08-03 by [ADR-0004](adr/0004-verdict-and-opinion.md), leaving **macOS
packaging (3.3) as the only remaining blocker** — and it is paperwork with external lead
time, not a design question. The broader audit below did find release-hardening work outside
ADR-0002; those items must be closed before publishing even though they do not change the
blocker count.

**A note on trusting this file.** Past audits found items here that were simply wrong: 0.3
described work that was already written (and broken), 2.4 sits in the Done archive on the
strength of code that never runs (see 5.3), and a reference path pointed at a directory
that doesn't exist on this machine. **When an item claims something is missing, check
whether it's actually missing before rebuilding it. When an item claims something is done,
check that the code has a caller.**


## How to read an item

- **Effort:** S (a sitting), M (a few sittings), L (a mini-project).
- **`decision`** — do **not** write code for this until the question is answered. Roughly a
  third of the open backlog is decisions mistaken for bugs; that mistake has been made
  repeatedly here and has twice produced a "fix" that had to be reverted.

Work each item on the standard loop: code → test → senior-to-junior explanation → commit
(terse one-liner, Kassa's identity, zero AI attribution). Local work may be committed with
Kassa's configured identity; only Kassa pushes.

**Attribution is enforced, not trusted.** Every commit in this repository is Kassa's own
work: no `Co-Authored-By:` trailer, no "Generated with …" footer, no vendor address, ever —
in commit messages and PR bodies alike. `scripts/check_commit_attribution.py` checks the full
history of every ref on each CI run and allows exactly three author identities
(`kassasana03@`, `kassaplayz@`, and the `users.noreply.github.com` one), plus `dependabot[bot]`
for lockfile bumps.

This is a guard rather than a note because several tools append attribution automatically, at
commit time, when nobody is reading — and such a commit is permanent in a way an ordinary
mistake is not. Rewriting it changes every SHA after it, which would invalidate release tags
and the CI-conclusion check 9.11's release gate depends on. It must be caught before it lands.

The guard deliberately does **not** flag prose that merely names a tool: an existing commit
explains a filename decision by referring to `CLAUDE.md`, and naming a thing is not claiming
it wrote the code. It matches trailers, footers, and author/committer addresses only. It also
refuses to run against a shallow clone rather than report success for the one commit it can
see, which is why `docs-smoke` checks out with `fetch-depth: 0`.

---

## Start here — the current sequence

Ordered by dependency, not severity. This replaces every previous "suggested sequence."
Struck rows are done; the numbers renumber as they close, so "next" is always row 1.

| # | Item | Why now |
|---|------|---------|
| 1 | **3.3** macOS packaging + notarization | Formal v1 blocker with external lead time; start the Apple Developer account work first |
| — | ~~**13.7** settle the trainer's product boundary~~ | **Done 2026-08-07** — [ADR-0006](adr/0006-trainer-is-developer-tooling.md): developer-only tooling; consumer Settings keeps focus labels only |
| — | ~~**7.24** split monotonic and calendar time~~ | **Done 2026-08-05** |
| — | ~~**8.10** make release builds network-silent~~ | **Done 2026-08-05** |
| — | ~~**7.23 / 7.25** attended-time + atomic lifecycle~~ | **Done 2026-08-06** — crash hydration, shutdown close, configurable threshold; persist-before-mutate, one label, restored focus mode |
| — | ~~**7.12** finish the SQL aggregation~~ | **Done 2026-08-06** — four aggregates, no materialized predictions, no recap loop, query count pinned |
| — | ~~**10.8** make Review charts truthful~~ | **Done 2026-08-06** — fixed 0–100 axis, distinct no-data state, sampled-context labels |
| 2 | **6.2** red-master rule | Finish the process decision already isolated on its branch; 9.11 depends on protected master |
| 3 | **Decision session B**: 4.11, **9.13** | Settle title-parser behavior, and what happens to the orphaned `v0.2.0` tag |
| 4 | **7.16** timestamp representation | Unblocks retention, Review ranges, and time-window correctness work |
| 5 | **8.5** threat model | Determines whether encryption is required and shapes uninstall/data handling |
| 6 | **10.1 / 14.3** webview + command contract | Cover the real bridge and remove its parallel hand-maintained descriptions |
| 7 | **4.4 / 14.1 / 14.5** performance gates | Remove avoidable query work, then measure the storage lane and engine scheduler |
| 8 | **2.3 / Tier 13** model retraining | Resume only after a packaged trainer lands (ADR-0006); until then this is repository tooling |

**Eight items were opened on 2026-08-04** and are deliberately *not* in the table above,
because none of them displaces anything in it. They are listed here so they are findable:

| Item | `S`/`M` | One line |
|---|---|---|
| ~~**6.6** GCC-on-Windows CI job~~ | `S` | **Done 2026-08-06** — `windows-gcc`, verified locally at 376/376 on MinGW-w64 UCRT |
| **9.13** orphaned `v0.2.0` tag | `S` `decision` | No release baseline exists; 9.11's gate would reject the tag |
| ~~**12.7** ADR-0002's dead link~~ | `S` | **Done 2026-08-08** — Darwin-dev fact is inline; guard forbids citing the gitignored file |
| ~~**4.13** nothing watches the ONNX pin~~ | `S` | **Done 2026-08-08** — weekly pin-freshness job opens an issue, never a digest PR |
| ~~**11.9** capture invariant unverified~~ | `S` | **Done 2026-08-08** — second-thread sampler fails on inverted stores (MinGW 1157/200) |
| ~~**11.10** stale test registry key~~ | `S` | **Done 2026-08-08** — fixture sweeps `test-<pid>-*` whose process is gone |
| ~~**7.21** settings durability~~ | `S` | **Done 2026-08-07** — temp + directory durable flush after 7.19's atomic rename |
| **4.12** formatter + static analysis | `M` | Neither exists for either language |

**6.6 was the one worth doing early, and it is now done.** The others are hygiene; 6.6 was the
only one that would have *prevented* a defect that actually shipped to `master`, and 11.9 and
11.10 both get easier now that it exists.

**Three more were opened on 2026-08-05**, from reading the architecture rather than from
fixing anything. These are not hygiene — each is a hole in something the app already promises:

| Item | `S`/`M` | The hole |
|---|---|---|
| **2.7** missed-session nudge | `M` **decided** | ADR-0005 keeps declaration manual and answers forgetting with one latched prompt per active stretch |
| **9.14** no import path | `M` | Four exports, zero imports; local-only means nothing else holds a copy, so a new laptop loses everything |
| ~~**7.22** no pre-migration backup~~ | `S` | **Done 2026-08-05** — `VACUUM INTO` now creates a consistent pre-migration recovery file |

**ADR-0005 has now settled 2.7:** declaration stays manual, presence is measured, and forgetting
gets a nudge rather than an auto-started untagged session. **7.22 was the cheapest real safety
win and is now closed**: the backup is paid once per schema bump and gives a bad but
successfully-committed migration a recovery path.

**Two more on 2026-08-05, from reading the product rather than the plumbing:**

| Item | `S`/`M` | The hole |
|---|---|---|
| **2.8** snapback has no "take me back" | `M` | It reconstructs exactly where you were, then offers only Dismiss |
| **7.23** attended session time | `M` **decided / in progress** | ADR-0005 chose idle-driven spans; the schema/storage slice has landed, wiring and UI remain |

**ADR-0005 answers the shared 2.7/7.23 question:** a session is declared by the user and real
only while attended. The nudge preserves declaration; idle transitions open/close durable
active-time spans; elapsed time keeps its old meaning. **2.8 remains independent** and acts
only on the user's click. The `session_spans` migration/storage API has landed; engine wiring,
reporting, and running/paused UI are still open until 7.23 closes.

**The 2026-08-05 cross-cutting audit opened eighteen assignable items and corrected stale
claims.** It reviewed production paths rather than counting files: each item below
has a concrete user failure, acceptance boundary, and dependency in its owning tier.

| Area | Items | What the pass found |
|---|---|---|
| Correctness | ~~**7.24–7.26**~~, **7.27**, ~~reopened **7.12**~~ | ~~Clock domains are mixed~~, ~~session/settings commands are not failure-atomic~~, capture semantics differ by OS, ~~and analytics still has unbounded/N+1 work~~ — only **7.27** remains |
| Release/privacy truth | **8.10**, **13.7** | A runtime font request contradicts local-only, while consumer Settings exposes a trainer absent from both this tree and an installed app |
| Architecture/performance | **14.5–14.6**, expanded **14.4** | The engine polls forever, slow commands block the UI thread, and hidden surfaces fetch data at startup |
| Product depth | **2.9–2.14** | History is not explorable, recording state is hard to see, repeat work is slow, onboarding stops before first value, Pomodoro is skeletal, and sessions cannot hold a reflection |
| Frontend/visual quality | **10.8–10.11** | Review charts mislead, Settings leads with internals, CSS tokens are incomplete/light-only, and Review cards describe different periods |

**A second 2026-08-05 pass opened ten additional assignable items without reusing those
eighteen.** This pass followed concrete user journeys through the live implementation: a
snapback firing, correcting a verdict, hiding and reopening the desktop app, locking the
machine, deleting private history, and starting against a mature database.

| Area | Items | What the second pass found |
|---|---|---|
| Product truth & control | ~~**2.15**~~, **2.16–2.17** | ~~Snapback episodes are never persisted~~ (**done 2026-08-06**), interventions have no delivery policy, and append-only labels cannot express an authoritative correction |
| Correctness & lifecycle | **7.28–7.29**, **9.15** | Editable goal-category names secretly change semantics, OS lock/sleep is treated as ordinary idle, and the single-instance tray app has no activation/close contract |
| Privacy completeness | **8.11**, ~~**8.12**~~ | App-only exclusions cannot redact one sensitive browser context; ~~“delete all” leaves personal exports plus full migration backups behind~~ (**done 2026-08-06**) |
| Desktop quality | **10.12** | Windows overlay placement ignores the tested multi-monitor geometry and fixed pixels ignore per-monitor DPI |
| Startup performance | **14.7** | Retention and a full `VACUUM` can block first paint before the webview even exists |

The ordering signal inside this batch was **2.15 → 8.12 → 14.7**: the first repairs a metric
already shown to the user, the second repairs a privacy action already promised to the user,
and the third removes repeat launch work after measuring it. **2.15's persistence half and 8.12
are done (2026-08-06); 14.7 remains**, and it is the one of the three that cannot start yet —
its acceptance names 14.5's deadline scheduler or 14.6's owned jobs, and neither exists. The
remaining items in this batch are parallel product/desktop candidates.

**A strict third pass opened eight more and then stopped.** Each survived a direct overlap
check against the first twenty-eight additions and against the implementation that landed
while this audit was running.

| Area | Items | What the strict pass found |
|---|---|---|
| Product action | **2.18–2.19** | Rules are global substrings typed from memory, and attended-time reporting has no optional plan to compare against |
| Local security | **8.13–8.14** | App-owned files inherit ambient permissions, while privileged webview commands survive top-level navigation |
| Data ownership | ~~**9.16**~~ | ~~“Export my data” silently caps history and can report no truncation after omitting windows~~ — **done 2026-08-06** |
| UX/data truth | ~~**10.13**~~, **10.14** | ~~Three incompatible row/session counts are called streaks~~ (**done 2026-08-06**); every document export/import still lacks a native picker |
| Model availability | **13.8** | Optional model-cleanup debris can prevent the core heuristic app from opening at all |

The correction order inside this group was **8.13 → 8.14 → 9.16 → 10.13**; **9.16 and 10.13 are
done (2026-08-06)**, leaving 8.13 and 8.14 at the front of it. **2.18–2.19** are
product candidates, **10.14** is desktop polish shared by several workflows, and **13.8**
either lands or disappears when **13.7** settles the trainer boundary. Generic scheduled
backups, cohort comparison, localization, and a separate notification-action item were
deliberately not opened: their useful scope is already owned, conditional on a later product
decision, or belongs as acceptance inside an existing item.

**Do not read the first pass as eighteen equal priorities.** The release-sized correction
queue is **7.24 → 8.10 → 7.23/7.25 → 7.12 → 13.7 → 10.8**. The rest are deliberately
assignable in parallel after their stated dependencies, with the Tier 2 additions serving as
product candidates rather than excuses to delay shipping.

**A rejected idea, recorded so it is not re-proposed.** "Hyperfocus nudges must be firing
falsely on overnight sessions" looked obviously true and is **false**: `update_break_state`
resets the break clock on any idle event past the threshold, so the nudge path is already
idle-aware. Only the duration is not. Checking it took one grep and would otherwise have
become a fix for a bug that does not exist — the third time this file has recorded that
pattern.

Beyond the correction queue above, most feature work is opportunistic. **Tier 9 is what turns
this from a correct program into a shippable product** — if the goal is "someone else uses
this," its remaining release-readiness items outrank most product-depth work. 9.1 was that
argument's headline item and is now done, which is what makes the blocker table below
meaningful.

**Next up is 3.3's external paperwork.** The Apple Developer account has independent lead
time, so its application should start now; it is the **only** formal blocker left, and the
small release security and data-integrity findings that used to run in parallel with it
(8.8, 8.9, 7.19, 7.20, 9.11) are all closed as of 2026-08-04.

The formal count is unchanged, but the August 5 audit added three **ship-before-publish**
findings outside ADR-0002: **7.24** (wrong production model inputs), **7.23/7.25** (attended
time and incomplete session failure semantics), and **8.10** (an undisclosed runtime network
request). **13.7**
must also stop the normal Settings UI from promising an impossible training path. The other
release work is **9.12** (choose a licence; there is still no `LICENSE`) and the external half
of **0.4b** (buy the signing certificate; the packaging defect itself is fixed).

**ADR-0002 release-blocker status as of 2026-08-01:**

| # | Blocker | State |
|---|---------|-------|
| 1 | **0.3** live-Mac capture | ✅ Done 2026-07-25 |
| 2 | **3.1** macOS tray + native `NSPanel` overlay | ✅ Done 2026-07-28 — verified by running the app |
| 3 | **3.3** macOS packaging + notarization | ⬜ **Next.** Longest lead time, needs an Apple Developer account. **Start the account application now**, since it gates nothing else but takes the longest — and it is what unblocks macOS notifications |
| 4 | macOS launch smoke in CI | ✅ Done 2026-08-01 — PR #40's hosted `macos-gui-smoke` passed with the other 14 CI jobs |
| 5 | **Decision session A** (5.3, 5.4, 1.2, 7.7, 7.18) | ✅ Done 2026-08-03 — [ADR-0004](adr/0004-verdict-and-opinion.md) |
| 6 | **7.3** schema migrations | ✅ Done 2026-07-29 — `user_version`, an ordered migration list, and a downgrade guard |

Note the shape of the accepted ADR: **five of six are verified done and the sixth is
paperwork.** No design question remains *inside that formal list*. The code audit findings
above are separate release gates discovered later; recording them here does not rewrite an
accepted ADR or pretend the newly observed defects were part of its original six.

---

## Tier 6 — CI health

Opened by the 2026-07-20 staff review against run `29728565319`, when this tier was titled
"CI is red (blocking)". **It is not red any more:** 6.1, 6.3, and 6.4 are all done and
CI-confirmed, and run `30168981559` (2026-07-25) was green on all three OSes. The only item
left is **6.2, a process decision** — what to do when master goes red — which is not itself
a CI failure. Retitled 2026-07-29 so the heading stops claiming a blocking outage that
ended three days earlier.

- **6.1 — DONE, CI-confirmed 2026-07-22.** Moved to the [Done archive](#done-archive).
  Both Windows jobs are green as of run `29890010902`; the 138 previously-skipped test
  cases ran and passed. The predicted "next problem" did surface — the first-ever real run
  of `desktop-app-build / ubuntu-latest` failed on X11 macro pollution (see 6.3's note).

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

  > **Decoupled 2026-07-22, awaiting CI confirmation.** Both `windows-desktop-integration`
  > and `desktop-app-build` lost their `needs:` — they now run unconditionally, with a
  > comment in `ci.yml` explaining why they must never regain one. Cost: they burn runner
  > minutes even when the core is broken; that is the point — broken core is exactly when
  > the desktop guard's answer matters.
  >
  > **And the guard's first real run immediately earned its keep:** once 6.1 unblocked it,
  > `desktop-app-build / ubuntu-latest` failed for the first time ever — X11 headers
  > (pulled in via webview → GTK → GDK) `#define KeyPress`, `KeyRelease`, `None`, `Status`
  > as bare macros, clobbering `EventType::KeyPress`, `snapback::Status`, and every
  > `::None` enumerator at parse time. Fixed the same day: `src/app/webview_compat.hpp` is
  > now the only legal include site for `webview.h` and scrubs the macro pollution right
  > after the include (same pattern as `tests/doctest_wrapper.hpp`). Verified to link on
  > macOS; Ubuntu is CI-verified only, so the next master run is the proof.
  >
  > **CI-confirmed 2026-07-25** by run `30168981559`, green on all three OSes for every job
  > but `docs-smoke` — which covers both halves: the decoupled jobs ran, and the Ubuntu
  > desktop build linked. This item is **done**; it stays here rather than moving to the
  > archive because the X11 lesson above is still the reason `webview_compat.hpp` exists.

- **6.4 — DONE, CI-confirmed 2026-07-22.** Moved to the [Done archive](#done-archive).
  Remaining loose ends: `action-gh-release` 2→3 (PR #19) still open by choice, and the
  Dependabot PRs #20–#23 should auto-close now that the versions match.

- **6.5 — MSVC warning noise obscures real diagnostics.** `S`

  Every Windows build emits C5285 (`cannot declare a specialization for 'std::tuple'`) from
  `doctest.h`, once per translation unit. Third-party, not ours — but it buries our own
  warnings, which is part of why 6.1 took a crash to surface rather than inspection.
  Suppress at the include site.

  > **Done in code 2026-07-22, awaiting a Windows CI log to confirm the spam is gone.**
  > All 24 test TUs now include `tests/doctest_wrapper.hpp`, which wraps
  > `<doctest/doctest.h>` in a `#pragma warning(disable : 5285)` push/pop under `_MSC_VER`.
  > One include site, third-party noise only — our own C5285s would still fire.

- **6.6 — DONE 2026-08-06, pending its first hosted run.** `S` `ci.yml` now carries a
  `windows-gcc` job: MSYS2 UCRT64, `-G "MinGW Makefiles"`, `SNAPBACK_BUILD_APP=OFF`,
  `SNAPBACK_ONNX=OFF`, Release, build `snapback_tests` and run CTest. No `needs:`, for 6.3's
  reason — a toolchain guard that stops running when CI is red does not guard the case it
  exists for.

  **The fallout the item warned to expect did not arrive**, and that is a verified claim rather
  than a hope: this configuration was run locally first, on **MinGW-W64 x86_64-ucrt GCC
  14.2.0** — the same environment class that found 11.8 — with a clean Release configure and
  `ctest`. **376/376 passed.** Nothing needed fixing before turning the job on, which makes
  sense: 11.8's fix landed on this toolchain in the first place. The job's value is forward,
  not retrospective.

  **A pass here means slightly less than a pass elsewhere, and the job says so in a comment.**
  11.9 records that the `CaptureThread` contradiction case does not detect its own bug on GCC.
  That is a documented property of the test, not a regression, and someone reading a green
  `windows-gcc` should know it before concluding the invariant is covered.

  Only remaining unknown is the hosted runner setup — `msys2/setup-msys2` installing the
  toolchain, and FetchContent reaching git from inside the MSYS2 shell (which is why `git` is
  in the install list rather than assumed from the runner's PATH). The build itself is proven.
  This takes CI to **16 hosted jobs**. The original finding follows.

- **6.6 (original finding) — No job builds this project with GCC on Windows, and that gap has
  already cost us.** `S`
  Opened 2026-08-04. CI compiles Windows with **MSVC only**, and Linux/macOS with `clang++`.
  The one combination nobody builds — **libstdc++'s MinGW filesystem implementation** — is
  where 11.8's production bug lived: `copy_options::overwrite_existing` is silently ignored
  there, which broke model rollback outright and left `save_app_settings` writing a **stale**
  backup while reporting success.

  That bug reached `master` and was found only because a portable GCC happened to be the only
  compiler on the dev machine. Nothing about that was systematic, and the same class of defect
  can land again tomorrow.

  Add a `windows-gcc` job (MinGW-w64 UCRT, `-G "MinGW Makefiles"`, headless target only — the
  desktop app's Win32 GUI link is a separate question). Expect to fix fallout before it goes
  green rather than after.

  **Read 11.8 first.** It records a second failure on this toolchain that is *not* a product
  defect — the `CaptureThread` contradiction case — and notes that the invariant half of that
  test does not detect its own bug there. Turning this job on without reading that will look
  like a regression when it is a known, documented property.

---

## Tier 0 — Finish the port's last gaps

- **0.3 — DONE 2026-07-25.** The tap was run on real hardware with Accessibility granted,
  and it works — but **the run's value was not the confirmation, it was what it found.**

  **macOS capture was half-blind.** The tap stamped every key/mouse event with the cached
  foreground app, so features knew *which* app you were in, but `WindowFocusChange` /
  `WindowTitleChange` were emitted only by `run_polling_fallback()` — i.e. only when the tap
  failed to create. With a *working* tap the engine never saw a single window change, so
  `context_switches_30s`, `context_switches_5min`, and `window_title_changed_30s` were
  permanently zero. Measured over 714 real predictions: `thrash` caps at 0.30 without switch
  counts (threshold 0.75) and `drift` caps at 0.30 without title churn (threshold 0.55), so
  **two of the four focus states were unreachable** — every prediction ever made on this
  machine was `PRODUCTIVE` or `DEEP_FOCUS`. It also starved `ContextTracker`, which only
  advances on window changes, so snapback recovery could never fire. Fixed in `ab74fe7`.

  **A second failure compounded it:** `name of front window` errors on Chromium browsers
  (`-1719`), which aborted the whole AppleScript, so `query_active_window()` returned
  `nullopt` and the engine learned *nothing* about the foreground app at the exact moment it
  most needed to. Since a browser's title is the only thing separating work from
  distraction, an afternoon of video read exactly like an afternoon of documentation. Fixed
  in `626ad87` with a per-browser title query and a `try` that keeps the app name when the
  title lookup fails.

  **The lesson worth carrying:** every one of those symptoms was invisible to the entire
  test suite and to the diagnostics added by 7.4/7.10 — `capture_running` was `true`, events
  *were* flowing, predictions *were* fresh. Health checks proved the pipe was open, not that
  the right things were going through it. **Before tuning any threshold, check that its
  inputs are actually non-zero in a live run.**

  The original item was:

  **Confirm native macOS capture works on a real Mac.** `S`
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


- **0.4b — Provision the signing certificate.** `S` (external dependency)
  **Still open, but only on the external half.** The code defect described below was fixed on
  2026-08-04: `package_windows.ps1` now signs `snapback.exe` immediately after the build and
  before CPack, signs the IExpress installer after it exists, and then **verifies the artifact
  it is about to upload** — it extracts the ZIP and requires the `snapback.exe` inside to be
  validly signed *by the passed-in thumbprint*. [PACKAGING.md](PACKAGING.md) documents the
  order and the verification path.

  **Checking the thumbprint, not just the status, is the part that matters.** A
  `Get-AuthenticodeSignature` status check alone passes for anything validly signed by
  anyone; a stray Microsoft-signed binary satisfies it. That is not hypothetical — the first
  version of this verification was written status-only and its own negative test passed
  against `where.exe`, which is Microsoft-signed. Both failure modes are now exercised
  against the real script text: an unsigned binary in the ZIP is rejected as `NotSigned`, and
  a validly-signed binary from a different certificate is rejected on the thumbprint.

  **What remains is the certificate itself.** The success path has never executed, because no
  EV certificate exists to run it with. Until a signed build has been produced and verified
  end to end, README must keep describing Windows signing as wired but incomplete — it
  currently does. Buy the cert, set `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT`, cut one release,
  and confirm the verification step passes; only then is this done.

  The original finding was:

  `package_windows.ps1` creates the CPack ZIP and embeds that ZIP in the IExpress installer
  *before* it signs the build-tree `snapback.exe`. The uploaded ZIP and installer payload
  therefore still contain the unsigned executable even when the secret exists; only the
  loose build-tree binary and the outer installer receive signatures.

---

## Tier 7 — Correctness & product findings (2026-07-20 staff review)

Covered in the review: `state.cpp`, `classifier.cpp`, `storage.cpp`, `capture_thread.cpp` +
`ring_buffer.hpp`, `tracker.cpp`, `title_parser.cpp`, the IPC/eval boundary, `main.cpp`, and
the frontend XSS surface. **Not covered — un-reviewed, not clean:** `features.cpp` extraction
maths, ONNX internals, the Windows overlay/tray implementations, frontend component
internals, and the benchmark harness.

### Correctness

- **7.1 + 7.2 — DONE 2026-07-22.** Analytics reports now query the requested timestamp
  window in SQLite without the 10,000-row cap, and hourly buckets convert UTC timestamps
  to local time.

  The original findings were:

  **7.1 — Analytics and summary reports silently cover only the last ~3 hours.** `S`
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

- **7.2 — PARTLY STALE, corrected 2026-07-30.** `S` The UTC-bucketing half **was already
  fixed** and this entry never said so: `AppState::analytics()` calls
  `local_hour_from_rfc3339(prediction.timestamp)`, not the character-slicing `timestamp_hour()`
  this text describes. Found while picking work off this file — the third time an item here has
  described a gap that the code had already closed (see 0.3 and the note on trusting this file).

  **Still open** is the second half below: `cutoff_rfc3339()` treats "1 day" as a rolling
  24 hours rather than the user's calendar day. The Review surface now *labels* its windows
  "Last 24 hours" / "Last 7 days" (9.7), so the UI is honest about it; whether the underlying
  window should change is a product decision, not a bug.

  The original finding was:

  `timestamp_hour()` (`state.cpp:51`) slices characters 11–12 out of strings built by
  `now_rfc3339()` (`state.cpp:69`), which uses `gmtime_r`/`gmtime_s` and appends `Z` — UTC.
  So `AnalyticsHour::hour` is a UTC hour rendered as the user's hour. In US Pacific that is
  an 8-hour lie: "you focus best at 14:00" means 06:00 local.

  Storing UTC is correct; *presenting* it is the bug. Recommend converting in the frontend —
  timestamps are ISO-8601 with `Z`, and `new Date(ts).getHours()` is exactly right.

  Related: `cutoff_rfc3339()` (`state.cpp:37`) computes "1 day ago" as "24 hours ago," so the
  "daily" summary is a rolling 24 h window, not the user's calendar day. Possibly intended,
  nowhere written down, and users read "day" as "today."

- **7.3 — DONE 2026-07-29.** `M` — sixth and last ADR-0002 release blocker that was pure
  implementation. `migrate()` now reads `PRAGMA user_version`, applies only the steps above
  it from an ordered append-only list inside one transaction, and stamps the result. SQLite
  makes DDL transactional, so a failed upgrade rolls back to the version it started at
  instead of leaving a database that is neither shape.

  Two design points worth keeping. **`user_version` 0 is ambiguous** — it means both "brand
  new file" and "install from before versioning", which describes every database in the
  field today. Nothing can tell them apart after the fact, so the runner replays from 0 on
  both and depends on every migration being idempotent; that rule and "never edit a released
  migration" are stated on `kSchemaVersion`, and a `static_assert` ties it to the last entry.
  And a database **stamped newer than the build refuses to open**: a later Snapback could add
  a `NOT NULL` column this build knows nothing about, so failing closed keeps the file
  recoverable instead of writing rows the newer build considers malformed.

  > *Two claims in the original finding were false when checked, and are preserved here
  > because the lesson is the point.* It said "**no `ALTER TABLE` anywhere in the
  > codebase**" — there was one, `ensure_prediction_model_id_column` at `storage.cpp:215`,
  > which is now migration 2 rather than an ad-hoc special case. And it said "**we have never
  > once opened a real pre-existing `focoflow.db`**" — `tests/test_storage.cpp` already had a
  > case that built a legacy schema on disk and opened it. What was genuinely missing was the
  > version stamp, the ordering, and the downgrade guard. Checking before building changed
  > what got built; see also the note at the head of Tier 5.

  The original finding was:

  **No schema migrations, on a database earlier installs already wrote.** The compatibility
  contract keeps the filename `focoflow.db`, so we promise to open databases we did not
  create. On an existing DB `CREATE TABLE IF NOT EXISTS`
  is a no-op — it reconciles nothing — and any column the C++ schema has that the user's file
  lacks produces a runtime `no such column` on first insert, for upgrading users only.

- **7.4 — DONE 2026-07-22.** `CaptureThread` now marks a returned hook as stopped and failed,
  records a diagnostic reason, tracks monotonic event arrival age, and safely joins a finished
  hook before restart. `AppState::health()` exposes `capture_failed`, `captureFailureReason`,
  `captureRunning`, and active-session staleness through the existing diagnostics contract.

  The original finding was:

  **A dead capture hook is indistinguishable from a healthy one.** `S`
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

- **7.5 — DONE 2026-07-22.** Both session-stop paths now use the same warning-safe helper to
  save an automatic recap label, and a regression test verifies the no-argument shutdown path
  includes that label in exported training data.

  The original finding was:

  **Sessions stopped via the no-argument path never get an auto-label.** `S`

  `stop_session(const std::string&)` (`state.cpp:210`) calls `save_auto_session_label()`.
  `stop_session()` (`state.cpp:195`), used on shutdown and internal teardown, **does not.**

  Auto-labels are training data (2.3 consumes them). Every session ending by any path other
  than an explicit UI stop is silently dropped from the corpus — biasing the eventual model
  toward sessions the user deliberately ended, i.e. probably the good ones.

  Also, the two overloads call *different* storage methods (`end_session` vs `stop_session`).
  Check whether that divergence is intentional before unifying.

- **7.9 — DONE 2026-07-22.** Privacy exclusions now match whole app-name words
  case-insensitively, so `Chrome` does not match `chromedriver`; the UI warns before saving
  one- and two-character exclusions that are likely to hide unrelated apps.

  The original finding was:

  **Privacy exclusions match by unanchored substring.** `S`

  `is_private_event_unlocked()` (`state.cpp:581`) tests
  `app.find(lower_copy(exclusion)) != npos`. Excluding `Chrome` also excludes
  `chromedriver`. A single-character exclusion — a typo, or an entry that survived trimming —
  excludes effectively everything, and **fails silently**: capture keeps running,
  `capture_running` stays true, no events are ever recorded.

  `normalize_privacy_exclusions()` (`state.cpp:566`) trims and dedupes but doesn't guard
  against over-broad patterns. Since over-exclusion looks identical to a dead capture hook
  (7.4), this needs at minimum a UI warning when a rule matches an implausible share of
  observed apps.

- **7.19 — DONE 2026-08-04.** `S` `save_app_settings()` now stages into `settings.json.tmp`,
  flushes, **checks the stream before the rename**, copies the current file to
  `settings.json.bak`, and renames the temp over the destination. `load_app_settings()` takes
  an optional `Logger*`, falls back to the backup, and reports what happened. The schema is
  unchanged; `tests/test_settings.cpp` is new (10 cases) — there were no settings tests at all.

  **The stream check is the actual fix, not the rename.** The old code checked only that the
  file *opened*; a disk that filled up mid-write produced a truncated `settings.json` and
  returned success. `flush()` forces that failure to surface where it can be thrown rather
  than in the destructor, where it would be discarded.

  **A missing file is not an error, and neither is an unknown key.** Only a file that exists
  and cannot be used is reported. `get_or` treats missing/null as "use the default" on
  purpose so a settings file survives schema drift both ways, and a test pins that: warning
  on every unrecognised key would make each upgrade log something the user cannot act on.
  A *present* key of the wrong type does throw, and that is reported.

  Writing this found a wrong assumption worth recording: the first draft asserted that
  `{"private_mode": ...}` was malformed. It is not — the keys are camelCase, so that is an
  unknown key and correctly defaults in silence. The test was wrong, not the code.

  **Residual limitation, deliberate.** The rename is atomic, so `settings.json` is never
  observed empty. It is not `fsync`ed, so a power loss can still lose a just-completed save
  from the OS cache — the file will be the *old* valid contents, never a torn one. Closing
  that needs `_commit`/`fsync` behind a platform seam; it is a different (and much rarer)
  failure than the one this item describes. Revisit with 8.5 if durability is in scope.

  Suite: **328 cases, 327 pass**; the one failure is the pre-existing MinGW-only case in 11.8.

  The original finding was:

  `load_app_settings()` catches every parse/type error and returns defaults without logging.
  `save_app_settings()` opens the only `settings.json` with `trunc`, writes directly to it,
  and never flushes or checks the stream afterward. A crash, disk-full condition, or partial
  write can therefore leave an empty/corrupt file that the next launch silently converts to
  defaults.

- **7.20 — DONE 2026-08-04.** `S` `create_session()` now closes the old session and inserts
  the replacement in one atomic step. The failure seam is a `BEFORE INSERT` trigger installed
  from a second connection: `session_id` is a random UUID, so no constraint fixture can
  predict a collision, and a trigger fails the insert on demand without putting a test hook
  in production code.

  **It is a `SAVEPOINT`, not a `Transaction`, and that was not the first answer.** Wrapping
  the two statements in `Transaction` — exactly what this item asked for — passed the new
  test and broke three others with *"cannot start a transaction within a transaction"*.
  `LargeFixture::seed` creates sixty sessions inside one outer transaction, and SQLite has no
  nested `BEGIN`. `SAVEPOINT` opens a transaction when none is active and nests when one is,
  so the same code is correct from both call shapes. A second test now pins that: an inner
  failure rolls back only its own work and leaves the caller's transaction committable.

  Two details worth keeping. `ROLLBACK TO` is not a pop — the savepoint stays on the stack
  until `RELEASE`, so the destructor must issue both or it holds the enclosing transaction
  open forever. And the negative case was verified by *removing* the fix and watching the
  test fail on `active_session()` returning nothing, which is the actual user-visible bug.

  Suite: **318 cases, 317 pass**; the one failure is the pre-existing MinGW-only
  `rollback_model` case tracked in 11.8, identical before and after this change.

  The original finding was:

  `Storage::create_session()` first marks every active session completed and only then inserts
  the replacement, without a transaction spanning both statements. If the insert fails, the
  old session is already closed and the requested new one does not exist.

- **7.23 — Report attended time by pausing sessions across idle spans.** `M`
  **DECIDED by [ADR-0005](adr/0005-a-session-is-declared-and-attended.md); PARTIAL
  2026-08-05.** Elapsed `duration_secs` keeps its historical wall-clock meaning. The new
  headline is active duration: the sum of durable spans during which a manually declared
  session was attended. Idle pauses; activity resumes; auto-stop is optional cleanup rather
  than the correctness mechanism.

  **Landed:** schema migration 4, `session_spans`, its session index, and storage operations to
  open, close, query, and detect spans. Pre-existing sessions deliberately return "not
  measured" rather than receiving fabricated active time.

  **Landed 2026-08-05 (engine + read path):** `WentIdle`/`WokeUp` now close/open spans, the
  first span opens at session start, and it closes on stop, on `stop_session(id)`, and on
  **replacement**. Nullable active duration flows through `recap()`, the batched
  `recent_session_summaries()` (as a grouped query — a per-session call would have undone
  7.12's `1 + 5N` fix), the JSON wire as `activeSecs`, and the frontend type/mapper. Review
  headlines attended time with elapsed beneath it and falls back to "Duration" on legacy
  sessions.

  Two findings from doing it:

  - **Replacement leaked an open span.** `create_session` completes the running session (7.20)
    but nothing closed its span, so a replaced session counted attended time to "now" forever
    and would eventually claim more of it than it was ever open for. Found by checking this
    item's own "still open" list against the code rather than assuming the wiring was complete.
  - **Two clocks were being compared.** The first attempt passed an `AppState`-clock timestamp
    into Storage, which stamps from the system clock and has no injected one. Durations came
    out as 0. `close_session_span_now(id, secs_ago)` takes an offset so every timestamp on a
    session comes from one clock.

  **Running/Paused landed 2026-08-05.** The engine's `idle` event was emitted from the day
  idle detection shipped and consumed by nothing, so an active session that had actually
  stopped counting still displayed as "active". The frontend now subscribes to it and the
  Session Control card reads **running** / **paused** / **no session** / **completed**. The
  rule is a pure function in `frontend/src/sessionStatus.ts` with its own test in the `tsx` runner —
  the component suite cannot run on this machine at all (**11.11**), so anything reachable
  only through a rendered component is untested locally.

  **DONE 2026-08-06 — the four remaining pieces landed.**

  **Crash hydration, and the design change it forced.** `Storage::close_dangling_session_span`
  closes a span a dead process left open, at the newest wall-clock evidence the session
  recorded — the last prediction, context snapshot, or snapback event. Not at "now": an app
  that crashed on Friday would otherwise report the weekend as attended. With no evidence at
  all the span collapses to zero length rather than guessing. `feature_snapshots` is
  deliberately not consulted; its `timestamp` is monotonic uptime (**7.24**), not wall clock.

  Writing that exposed a real hole in the edge-driven design. Hydration closes the span with
  **no idle edge on the way back in** — the user never went idle, the process died — so an
  edge-driven rule records nothing more until they walk away for five minutes and return.
  Attendance is now a **level**: `session_attended_` versus "should there be a span open",
  compared once per tick. That subsumes the old edges and covers start, stop, replacement,
  shutdown, and hydration with the same two lines. It also required marking activity on
  `start_session` — starting a session while the detector was already Idle would otherwise
  open a span and immediately close it on the next tick.

  **Clean shutdown closes the span** in `stop_engine()`, after the engine thread joins so
  nothing can reopen it behind us. It is `noexcept` and best-effort: a failure there lands in
  exactly the case hydration already handles.

  **The threshold is a setting.** `AppSettings::idle_threshold_secs`, bounded to
  [30 s, 1 h], applied to the running detector rather than only stored, persisted through
  `settings.json`, exposed as `set_idle_threshold`, and offered as a picker in Settings.
  Out-of-range is **rejected before any mutation**, not clamped — a clamped value is
  indistinguishable from one the user chose. A pre-7.23 settings file has no such key and
  lands on the default. `IdleDetector::set_threshold_ms` keeps the activity baseline, so
  lowering the threshold can conclude the user is *already* idle on the next poll.

  **Scroll and mouse movement do count as presence**, and a case now pins it. Requiring
  keystrokes would report a reviewer or a reader as absent for their entire session, and every
  OS idle timer the user has already calibrated on agrees. Windows currently classifies wheel
  traffic as `MouseClick` (**7.27** owns that), which is input either way.

  **The injected clock Storage does not have turned out not to be needed.** Reopen is testable
  without one by writing the spans with explicit timestamps and reopening a file-backed
  database — which is closer to the real failure anyway, since a crash is two processes rather
  than one clock. Ten new cases across storage and app state; suite **366 pass**.

  **Still open, and deliberately elsewhere:** the lifecycle atomicity questions (a failed start
  leaving the old session live, restart not restoring the saved focus mode) belong to **7.25**
  rather than to a second owner here.

- **7.22 — DONE 2026-08-05.** `S` `Storage::migrate()` now copies the database to
  `focoflow.db.pre-v<N>.bak` immediately before applying any migration, named for the version
  it came *from* so two upgrades leave distinguishable files. Four cases cover it.

  **`VACUUM INTO`, not a file copy.** The database is open in WAL mode, so the bytes in
  `.db` are not the whole story — recent commits live in `-wal`, and copying the main file
  alone can produce a torn snapshot. `VACUUM INTO` asks SQLite for a consistent single-file
  copy, which is the guarantee a restore actually needs. It also cannot run inside a
  transaction, which is why the backup happens before `migrate()` opens one. The destination
  is a **bound parameter**: a data directory can contain a quote, and hand-quoting a path into
  SQL works until it doesn't.

  **A failed backup is logged, not fatal.** Refusing to start because a backup failed would
  turn a full disk into "the app will not open" — worse than the risk it guards, given the
  migration is transactional either way.

  **No backup for a brand-new database.** Version 0 means both "new file" and "pre-versioning
  install" and cannot be told apart afterwards, so the presence of a user table decides it.
  Backing up an empty file on every first run is noise, and noise is what makes a real backup
  message easy to miss.

  **The failure test was vacuous on the first attempt.** It put an empty directory where the
  backup belongs — but `back_up_before_migration` calls `std::filesystem::remove` first (so a
  stale backup cannot pass as a fresh one), and **`remove()` deletes empty directories**. The
  seam was cleared, the backup succeeded, and the test asserted nothing while passing. Now the
  directory is non-empty, and the case asserts the path is *still a directory* afterwards so
  the failure is proven rather than assumed.

  Suite: **336 cases, 336 pass.** The original finding follows.

- **7.22 (original finding) — A schema migration that succeeds and is wrong is unrecoverable.** `S`
  Opened 2026-08-05. `Storage::migrate()` runs the whole upgrade in one transaction, so a
  migration that *fails* rolls back cleanly to the version it started at — that half is
  already right, and 7.3 got it right deliberately.

  The unhandled case is a migration that **succeeds and is wrong**: a bad `UPDATE`, a dropped
  column, a botched backfill. The transaction commits, so there is nothing to roll back, and
  `kSchemaVersion`'s own rule that **a released migration is never edited** means the damage
  is permanent and ships to everyone who upgrades. The user's only recovery is an
  `export_my_data` they had to think to run beforehand.

  Copy the database to `focoflow.db.pre-v<N>.bak` immediately before the first schema-changing
  migration runs, and only then. This costs nothing on a normal launch, because `migrate()`
  already early-returns when `from == kSchemaVersion` — it is paid once per schema bump, not
  per start. Keep exactly one such backup, log where it went, and surface the path in
  diagnostics so a support answer can be "your data is still there, at this path."

  Ties to **9.4** — walking the upgrade path deliberately is what would demonstrate this
  working, and neither is much use without the other.

- **7.21 — DONE 2026-08-07.** `S` `save_app_settings` now `fsync`s / `_commit`s the temp
  file before rename and the parent directory after, via `util/durable_file`. A save that
  cannot flush fails closed instead of reporting success over cached bytes. Suite covers the
  sync seam on missing paths and a successful round-trip that leaves no `.tmp` behind.

  The original finding was:

- **7.21 (original finding) — A just-saved settings.json can still be lost to power failure.** `S`
  Opened 2026-08-04 as 7.19's stated residual, recorded here so it is a task rather than a
  footnote inside a closed item.

  7.19 made the write **atomic** — `settings.json` is replaced by renaming a fully-written
  temp file, so it is never observed empty or half-written. It did not make it **durable**:
  neither the temp file nor the containing directory is `fsync`ed, so an OS-level crash or
  power loss can discard a save the application already reported as succeeded.

  **The failure is bounded and much rarer than the one 7.19 fixed**, which is why it was
  deliberately left: the file reverts to its previous *valid* contents, never to a torn one.
  A user loses one settings change, not their configuration.

  Closing it needs `_commit` on Windows and `fsync` on POSIX behind a small platform seam,
  applied to the temp file before the rename and to the directory after it — the directory
  entry is a separate write, so syncing only the file still permits losing the rename. Weigh
  it against the cost first: this adds a synchronous disk flush to every settings write, and
  8.5's threat model should say whether that trade is worth making.

- **7.24 — DONE 2026-08-05.** `M` **release correctness** `CaptureEvent` now carries
  `wall_clock_secs` beside the monotonic `timestamp_secs`, all three backends stamp it from
  one shared `wall_clock_secs_now()` in `input_hook.hpp`, and `fill_time_fields` derives the
  calendar features from it. Monotonic time keeps durations, ordering, debounce, and the
  rolling windows; wall time owns only `hour_of_day` and `day_of_week`.

  **The bug was measured, not assumed.** Reverting the one-line fix and re-running the new
  case produces `hour_of_day = 0` and `day_of_week = 3` for an event 10 seconds after boot —
  midnight on a Thursday, because 1 Jan 1970 was a Thursday. That is what production was
  feeding the model.

  **The fallback is what preserves the trainer contract.** With no wall clock supplied the
  extractor still uses `timestamp_secs`, so `scenarios.json` — which feeds an epoch-shaped
  `base_time` through `timestamp_secs` and sets no wall clock — produces byte-identical golden
  features. Both halves are pinned by tests: one asserts the split, the other asserts the
  fallback, because removing it would silently rewrite every golden `hour_of_day` to whatever
  the clock said when the suite ran.

  **UTC kept deliberately.** It is what the deployed model was trained against and what
  `golden.json` pins. Local-versus-UTC is a change in the *meaning* of a model input and needs
  a retrain and a version bump, not a quiet edit — left to **7.16** and Tier 13 as the item
  asked.

  One incidental find: doctest treats commas in `--test-case` filters as separators, so a case
  named "a, b" silently matches nothing and reports success. A mutation check appeared to pass
  because of it. Test names here avoid commas.

  Suite: **350 cases, 350 pass**, including feature parity unchanged.

  The original finding was:

- **7.24 (original finding) — Split monotonic event time from calendar time.** `M`
  Opened 2026-08-05. All three production capture backends timestamp events with an uptime
  clock: `GetTickCount64()` on Windows and `steady_clock`/process-relative seconds on macOS
  and Linux. `FeatureExtractor::fill_time_fields()`, however, passes that number to
  `system_clock::to_time_t()` and derives `hour_of_day` and `day_of_week` as if it were Unix
  epoch time.

  That makes two model inputs depend on when the machine or app last started, not when the
  user worked. The fixture misses it because `fixtures/feature_parity/golden.json` supplies
  real epoch-shaped timestamps directly. This is the same dangerous shape as the old
  `seconds_since_session_start` bug: extractor tests are green because they do not use the
  production clock contract.

  Carry separate monotonic and wall-clock values, or inject a clock that can supply both.
  Monotonic time owns durations, ordering, debounce, and rolling windows; wall time owns
  calendar features and persisted presentation timestamps. A production-shaped regression
  must use something like `monotonic = 10s` plus a known wall time and assert both domains.
  Decide local-versus-UTC calendar semantics explicitly and version/re-evaluate the deployed
  model contract before silently changing its inputs. Coordinate with **7.16** and Tier 13,
  but do not let the broader storage-time ADR delay this production-input correction.

- **7.25 — DONE 2026-08-06.** `L` All four symptoms closed, with the storage boundary as the
  commit point.

  **Start persists first and mutates second.** The savepoint covers closing the replaced
  session's span *and* `create_session`, so a rolled-back replacement leaves the old session
  both running and still attended. Every in-memory change — focus mode, extractor, tracker,
  Pomodoro, attendance — happens after `release()`, where nothing can throw.

  **The failure in the test is real, not injected.** A second connection holds a
  `BEGIN IMMEDIATE`, and SQLite is opened with no busy timeout, so the write gets `SQLITE_BUSY`
  immediately. That needed no test-only seam in the shipping class (7.14's objection) and it is
  the honest production scenario: another process or a backup tool holding the file.

  **Replacement writes the same automatic label a stop does.** Previously the only thing
  deciding whether a finished session got a verdict was whether the user pressed Stop or just
  started the next thing — not a distinction they made deliberately. It stays outside the
  savepoint and best-effort for the same reason the stop path is: a label that could not be
  written must not cost you the session it describes.

  **One label, enforced in Storage.** `save_auto_session_label` returns the existing `auto`
  label instead of appending a second. `stop_session` was already idempotent, so a double-click
  wrote two inferred verdicts that differed whenever a prediction landed between them — and
  the labels table is append-only by design, so nothing could clean that up afterwards. The
  *existing* label wins, not the newer inference: it is the one the user was shown.

  **Restart restores the state that belongs to the session, not just the row.** The saved focus
  mode comes back (it sets the risk threshold and the hyperfocus window, so a Deep session was
  silently continuing under Normal's rules), and `FeatureExtractor::resume_session` back-dates
  the session origin by `Storage::session_elapsed_secs` so
  `seconds_since_session_start` continues instead of restarting at zero while the recap beside
  it reported hours. The break clock is deliberately **not** back-dated — nothing is known
  about what happened while the process was gone, and back-dating it would push the hyperfocus
  nudge most of the way to firing the instant the app reopened. Running/paused span state on
  restart is **7.23**'s hydration, already landed.

  Six new cases; suite **372 pass**. The original finding follows.

- **7.25 (original finding) — Make session start, replace, stop, and restart one failure-atomic
  lifecycle.** `L`
  Opened 2026-08-05. **7.20 fixed the SQLite half only.** `Storage::create_session()` now
  closes the prior row and inserts its replacement atomically, but `AppState::start_session()`
  changes focus mode and resets extractor/tracker/Pomodoro state before that storage call can
  fail. A failed insert can therefore leave the old database session active with new in-memory
  state.

  The other paths also disagree. Replacement completes the old session without the automatic
  label that an explicit stop writes. Storage stop is idempotent, while `AppState` can append
  another auto-label on a repeated Stop. On restart, the active row is hydrated but its saved
  focus mode is not restored, and extractor session elapsed time starts again at zero. These
  are four symptoms of session lifecycle being spread across storage, live state, feature
  state, and labels without one owner.

  Build one lifecycle operation with a committed storage result as the boundary: no live
  mutation before persistence succeeds; failure preserves the exact prior session; replace
  and stop produce exactly one label, enforced idempotently; and restart restores the active
  row's saved focus mode plus ADR-0005's running/paused span state. Cover failed start,
  replacement, double-stop, and process reopen. Implement with **7.23** and read **7.16**
  because active-duration and resume semantics are part of the same aggregate.

- **7.26 — DONE 2026-08-06.** `M` All five setters go through one
  `commit_settings_unlocked(candidate, publish)`: write the candidate to disk, then commit it
  in memory, then publish it to live state. The write takes a **copy**, so a throw leaves
  `settings_` and every live field exactly as they were.

  **The privacy case is what gives this teeth, and the test drives it directly.** A failed
  request to turn private mode *off* used to resume recording while IPC reported that the
  change had failed — the user's belief about whether they were being recorded and the app's
  behaviour disagreed, in the direction that matters. Same shape for focus mode, which sets the
  classifier's threshold: a failed save left the live mode changed and silently rescored the
  session against a mode that was never written.

  **The injected failure is real.** `save_app_settings` stages through `settings.json.tmp`, and
  an `ofstream` cannot open a path that is a directory — so the write fails at step 1, before
  anything the user depends on is touched, which is precisely the case the old ordering got
  wrong. Verified by mutation: restoring the mutate-then-save order turns six assertions red
  across all five settings, including the private-mode one.

  Five fields covered (private mode, exclusions, goal categories, focus default, and 7.23's
  idle threshold), each asserting the returned error *and* the live snapshot *and* the file.
  A concurrent-writer case races two threads on different fields and proves disk and memory
  still agree afterwards — they serialize on `mutex_`, and the file is a complete document
  matching what the process believes rather than a torn write or a stale losing field.

  **7.21's durability question is untouched and still open**: this makes the write atomic
  against *failure*, not durable against power loss, which remains 7.21's call to weigh against
  **8.5**. Two new cases; suite **392 pass**. The original finding follows.

- **7.26 (original finding) — Make settings commands atomic across disk and live behavior.** `M`
  Opened 2026-08-05. Focus mode, private mode, and exclusions mutate `settings_` (and in some
  cases publish live state) before `save_app_settings()` runs. If the save throws because the
  directory is read-only or the disk is full, IPC reports failure but the process keeps the
  new behavior.

  The privacy case gives this teeth: a failed request to turn private mode *off* can resume
  processing even though the UI reports that the change failed. **7.19** guarantees a valid
  JSON file and **7.21** discusses durability; neither gives the user action a strong failure
  guarantee.

  Validate and persist a candidate first, then commit and publish it as one serialized
  operation. On failure, retain the exact previous runtime state. Add injected-write-failure
  and concurrent-setter cases for private mode, exclusions, goal categories, and focus
  defaults; assert both the returned error and the live snapshot.

- **7.27 — Define and test one capture-event contract across platforms.** `M` for Windows +
  macOS; `L` including Linux
  Opened 2026-08-05. The extractor assumes `CaptureEvent` has portable meaning, but each
  backend currently invents a different one. Windows treats every non-move mouse message as a
  click, including button-up and wheel traffic. macOS writes mouse speed as zero. Linux maps
  every `EV_KEY` press, including mouse buttons, to `KeyPress`, leaves kinematics empty, and
  can query foreground context through `sh`/`xdotool`/`ps` inside the input loop.

  The result is model drift by operating system: macOS has effectively dead mouse features,
  Windows over-counts clicks, and Linux can mix mouse buttons into typing while spawning work
  proportional to input volume. **0.3** proved live macOS delivery and **11.3** starts after a
  normalized event already exists; neither tests this boundary.

  Introduce pure per-platform translation fixtures against one documented event contract:
  one click per button-down, explicit wheel semantics, consistent speed units, and identical
  key/button classification. No raw callback or device-read loop may launch a child process;
  foreground context must be an injectable, cadence-bounded provider. Add live platform
  distribution smokes (expected nonzero fields). Over a fixed-duration 10,000-event burst,
  context-probe count must stay within the provider's cadence bound (plus initialization) and
  remain effectively the same as a zero-event control of equal duration; event count itself
  cannot increase probe count.

- **7.28 — Separate goal-category identity from editable display text.** `M/L`
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

- **7.29 — Treat screen lock, suspend, wake, and unlock as first-class lifecycle events.** `M/L`
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

- **7.7 — DONE 2026-08-03.** Settled by
  [ADR-0004](adr/0004-verdict-and-opinion.md) as part of Decision session A. The
  contradiction is now a *sentence*, not a defect: the scores are the **model's opinion**
  and policy never edits them; `focus_state` is the **policy verdict**, the one value the
  app acts on. A row reading `focus_state = 'DISTRACTED', focus_score = 95` says "the model
  thought you were deep in; policy says the app is blocked."

  **Clamping the score was the trap.** `focus_score` feeds back into the feature vector as
  `focus_momentum` (`state.cpp`), so clamping it in the guardrails would have dragged the
  model's own inputs toward whatever policy decided — a feedback loop nobody designed — and
  destroyed the model-vs-policy disagreement signal 2.3 needs. The seam that looked like the
  obvious fix was the one that had to stay untouched.

  What changed instead: `predictions.state_source` (migration 3) records which rule decided
  the verdict, so the UI can finally *explain* an override. That was the gap with teeth —
  `explainPrediction` could previously render "**Distracted** because no app switching ·
  settled in one window" for a Block-rule row, evidence flatly contradicting the verdict.
  The hero's colour now comes from the verdict rather than `riskLevel(distraction_risk)`,
  which is the same contradiction inside a single element.

- **7.18 — DONE 2026-08-03.** Settled by
  [ADR-0004](adr/0004-verdict-and-opinion.md): **policy is demote-only.** A guardrail may
  move a state toward distraction, never away from it. The drift branch now fires only on
  `PRODUCTIVE`:

  ```cpp
  } else if (drift >= tuning::policy::kDriftPseudo && scores.focus_state == "PRODUCTIVE") {
  ```

  Rule 1 was already demote-only in effect, so the `!= "DEEP_FOCUS"` condition was the sole
  violation — an oversight, not a considered asymmetry. The `DEEP_FOCUS` exemption survives
  on its own merits: deep evidence outweighs title churn.

  **The feared cost did not materialise, and the reason is worth keeping.** This item warned
  that changing the rule "moves the classifier parity fixture." It did not: `golden.json`
  pins the *feature vector*, not classifier output, and none of the three scenarios in
  `classifier_scenarios.json` exercised the drift branch at all. The branch that produced a
  `decision` tag partly on fixture-churn grounds turned out to be **unfixtured** — checking
  cost one read and removed the largest stated cost. A fourth scenario now covers it.

  The characterization test in `tests/test_classifier_properties.cpp` did exactly what it was
  built for: it failed on this change and is now the assertion *guardrails never raise the
  state*, the property that originally failed on 246 of 188,502 assertions.

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

  > *Observed 2026-07-29, not theorized.* Writing 7.12's tests surfaced the fourth bullet in
  > practice: two sessions created in the same wall-clock second **tie** under `ORDER BY
  > started_at DESC`, and which one comes back first is undefined. Two tests were written
  > against the assumption that the newest session sorts first and failed. They now set
  > `started_at` explicitly through `Storage::backdate_session_for_test`. That seam is a
  > workaround for this item and should be reconsidered when 7.16 is settled — if timestamps
  > gain sub-second resolution, the tests can go back to relying on insertion order.
  >
  > Note this also affects users, not just tests: the history list's order is arbitrary among
  > sessions started in the same second. Rare, but it is the same root cause.
  >
  > *Partly mitigated 2026-07-29.* Every session-selection query now orders by
  > `started_at DESC, session_id DESC`. Because `session_id` is the primary key that is a
  > **total order**, so the history list is at least *stable* — the same sessions come back
  > in the same order every time, and 7.12's three queries agree on which sessions they are
  > talking about. It is explicitly **not** a fix for this item: `session_id` is a random
  > UUIDv4, so ties are broken arbitrarily rather than chronologically. Recovering true
  > within-second order still needs the decision below.

  **7.3 is now done, which changes the shape of applying this.** There is an ordered
  migration list to append to, so converting a column's storage type is a migration rather
  than a rewrite — the mechanism is no longer part of the cost.

### Product gaps

- **7.6 — DONE 2026-07-30.** `M` Every slice landed; the header lagged the body until
  2026-08-04, which is the failure mode this file's own preamble warns about — an item that
  reads as open while the work sits finished underneath it. The history is kept below.

  **PARTIAL 2026-07-26, extended 2026-07-29:** Settings provides a two-step, permanent
  “delete all activity data” action. The native command removes sessions, predictions,
  feature/context snapshots, labels, and Snapback events atomically, resets live session
  state, and deliberately preserves privacy settings and app rules.

  **Delete a single session landed 2026-07-29** — `delete_session` in `commands.hpp`, backed
  by `Storage::delete_session` and `AppState::delete_session`. It clears live engine state
  when the deleted session is the active one, and returns whether a row was actually removed
  so the UI can tell a stale list entry from a successful delete.

  **DONE 2026-07-30.** The three remaining slices landed together:

  - **Delete-session UI** — the Insights card lists each session with a two-step delete.
    `useInsights` prunes the row locally *before* refetching, because `refreshInsights`
    swallows its errors by design and would otherwise leave a deleted session on screen; a
    regression test drives the failing-refetch path specifically. The `false` return is
    surfaced as "That session was already gone" rather than "deleted".
  - **Open the data folder** — `open_data_folder` in `commands.hpp`, backed by
    `src/app/reveal_path.hpp`. `NSWorkspace` on macOS and `ShellExecuteW` on Windows, so
    neither starts a shell or a child process; POSIX spawns `xdg-open` with an argv array. The
    command returns the path whether or not the open succeeded, because "here is where it is"
    is the only answer an unsupported platform can give.
  - **Export my data in a legible form** — `export_my_data`, backed by
    `src/app/data_export.hpp`, writes one Markdown file of sessions and the windows captured
    during them. The renderer is pure and takes already-fetched rows, which is what makes the
    Markdown-table escaping testable: an unescaped `|` in a window title shifts every later
    column and quietly turns the archive into an inaccurate record. Truncation is stated in
    the file rather than applied silently.

  For an app whose core function is recording every keystroke and window title, "you may
  inspect and destroy what I collected" isn't a nice-to-have — it's what makes local-only
  credible. The onboarding wizard already makes the promise; this is its enforcement.

  Ties to 7.3 (honest escape hatch for an unmigratable DB) and 8.5 (the threat model should
  drive its shape).

### Observability & test coverage

- **7.10 — DONE 2026-07-22.** `HealthStatus` now reports nullable monotonic age for the last
  prediction and a suppression reason: `idle`, `no_session`, `private_mode`, or `none`.
  The existing Diagnostics card displays both fields, making a stale prediction distinguishable
  from a legitimate suppression.

  The original finding was:

  **Nothing measures whether predictions are still being produced.** `S`
  **Cheapest high-value item in this file.**

  `HealthStatus` reports `capture_running`, `capture_events_dropped`, and the classifier
  backend. Nothing reports **prediction freshness.** The tick can produce nothing for
  legitimate reasons (idle freeze, throttle, no session, private mode) and illegitimate ones
  (dead hook 7.4, over-broad exclusion 7.9, classifier throwing 8.1). From outside these are
  indistinguishable, and the UI shows the last prediction with no indication of its age.

  Add `last_prediction_age_secs` plus a suppression reason (`idle` / `no_session` /
  `private_mode` / `none`). **This single change makes every silent failure mode in this
  file visible** — 7.4, 7.9, and 8.1 all surface through it.

- **7.11 — DONE 2026-07-31.** `M` — the sixth shape, the **large** fixture, landed today:
  60 sessions × 200 predictions = **12,000 rows**, seeded across 20 days in one transaction
  (12,000 autocommitted inserts would be 12,000 fsyncs; the whole fixture runs in 0.3s). It
  buys three things the small fixtures structurally could not.

  **1. 7.1's own regression test, which was never written.** 7.1's write-up specified it —
  *"seed >10,000 predictions across several days, assert the weekly `sample_count` exceeds
  10,000, watch it go red"* — and 7.1 was marked DONE without it. The fix is real, but
  **nothing pinned it**, so a reintroduced cap would have left every existing test passing.
  Before today no test in this repo seeded more than a handful of rows, which is exactly what
  made the original bug invisible to the suite.

  **2. Index usage under real statistics.** The existing plan assertions run against an
  *empty* database, and that cannot fail for the reason we care about: with no stats SQLite
  plans structurally, so it has no basis on which to prefer a scan. The new case runs
  `ANALYZE` (new `Storage::analyze_for_test()`) over 12,000 rows, which is the adversarial
  case for 7.13's indexes — the planner now *can* decide a scan is cheaper, and doesn't.
  Verified by deleting `idx_predictions_ts` and watching the `TEMP B-TREE` assertion fail.

  **3. Batched-vs-per-session parity at a scale that exercises the window function.** 7.12
  proved parity on a few rows, which does not touch `ROW_NUMBER()`'s partitioning, the
  per-session cap, or the tie-breaking 7.16 forced into the `ORDER BY`. The comparison now
  runs field by field across 60 sessions.

  One thing worth recording from writing it: the first run reported **0 rows for every
  assertion**, because `Storage::Transaction` rolls back unless `commit()` is called. *A seed
  that silently seeds nothing is indistinguishable from a query that correctly returns
  nothing* — which is the same failure shape as everything else in this tier.

  **Still not done: the timing measurement.** This fixture is 4.4's entry point, but nothing
  here asserts a duration; wall-clock bounds on a shared CI runner buy flakes, not signal.
  7.12 remains structural-not-benchmarked until 4.4 lands a real harness.

  The original partial entry follows.

- **7.11 (earlier state) — five of six shapes, 2026-07-29.** `M` — five of the six fixture
  shapes now exist in
  `tests/test_storage.cpp`, built in-process rather than committed as binary `.db` files (a
  checked-in database cannot be reviewed, and stops representing "what an old build wrote"
  the moment someone regenerates it from a current one).

  Covered: **unclean shutdown / WAL recovery** — the database is copied together with its
  `-wal` and `-shm` sidecars while the original connection is still open, so the committed
  rows are still in the WAL; **corrupt**, which must be refused with a logged reason rather
  than crashing or starting with a silently empty history; **aged**, pruned on open without
  taking its sessions with it; **foreign-authored**, carrying unknown tables and columns;
  and a full close/reopen round trip across all five activity tables.

  **The missing *large* fixture is now DONE 2026-07-31** — see the entry above.

  The original finding was:

  **No test ever opens a pre-existing database.** The general form of 7.3. Untested as a
  result: migration (7.3), retention against aged data (5.5), index usage as tables grow
  (7.12), recovery from a corrupt or partially-written DB, and **WAL recovery after unclean
  shutdown** — which, for an always-on tray app users will kill via Task Manager, is the
  *normal* shutdown path, not an edge case.

### Performance

- **7.12 — DONE 2026-08-06.** `M` The reopened half is closed: neither `analytics()` nor
  `summary_report()` materializes a `PredictionRecord` any more, and no path calls `recap()`
  in a loop.

  Four new aggregates, each returning final numbers: `prediction_stats` (count, average,
  distracted count, and the longest non-distracted run), `hourly_focus_buckets`,
  `productive_session_streak`, and `session_window_totals`. `analytics()` went from *every
  retained prediction plus a 200-session `recap()` loop* to four queries;
  `summary_report()` from every prediction in its window plus 500 fully-built recaps to three.

  **The streak is a gaps-and-islands query, not a loop.** The difference between "position in
  the whole ordering" and "position among rows of the same distracted-ness" is constant inside
  a run and changes at every boundary, so grouping by it groups by run. The ORDER BY carries
  `id DESC` alongside `timestamp DESC` because predictions written inside the same second are
  otherwise ordered arbitrarily — a run's *length* is the same read either way, but its
  *grouping* is not.

  **Local hour survived the move**, as 7.16's unsettled state requires. SQLite's `localtime`
  modifier calls the same C library conversion `local_hour_from_rfc3339` does, so the two agree
  including across DST — and the parity test proves it rather than assuming it.

  **The gate is query count, not wall clock.** A per-session loop returns exactly the same
  numbers as one aggregate, just N times more slowly, which is how the N+1 path survived being
  called fixed the first time — no correctness test could see it. `count_statements_for_test`
  wraps `sqlite3_trace_v2`, so SQLite counts its own statements; the counter lives in that
  function's frame rather than in a member, since `Storage`'s move operations carry only `db_`
  and `stmt_cache_` and a member added to one and not the other fails silently. The new
  aggregates are pinned at 2/1/1/1 statements against 12,000 rows; the loop they replaced
  measures at more than 5 × 60.

  **Parity is against the C++ fold, not against hand-written numbers.** The reference
  implementation in the test *is* the code that used to run in `AppState`, so a disagreement
  means the move changed an answer the user was already being shown. Both mutations were
  checked: dropping `localtime` and dropping the window function's `PARTITION BY` each turn
  the suite red, so the test is not vacuous.

  Still not benchmarked — **4.4**'s perf gate and **14.1**'s storage-lane measurement are
  unchanged by this. The win here is structural and now bounded, which is what the item asked
  for. Six new cases; suite **376 pass**. The reopened finding follows.

- **7.12 (reopened finding) — PARTIAL, REOPENED 2026-08-05.** `M` The 2026-07-29 rewrite improved two query
  families, but its headline claim that "all three call sites now aggregate in SQL" is false.
  `AppState::analytics()` still materializes every retained prediction through
  `predictions_since()`, then loops over `recent_sessions(200)` and calls `recap()` for each
  completed row — an N+1 path whose per-session query cost grows whenever recap gains another
  aggregate, all under the UI-facing storage lock.
  `summary_report()` also materializes every prediction in its day/week window.

  Finish the job before benchmarking **14.1**: SQL should return final counts, averages,
  hourly buckets, and streaks in constant query count, with no full `PredictionRecord` vector
  and no per-session `recap()` calls. Preserve local-hour behavior while **7.16** is unsettled.
  The 12,000-row fixture must prove field-for-field parity; add a 90-day same-host benchmark
  that records p50/p99, peak memory, query count, and concurrent-writer delay. Use relative
  improvement and bounded materialization as the gate, not a flaky absolute CI duration.

  **What did land on 2026-07-29:** `Storage::recent_session_summaries()` replaced the
  `recent_sessions()` + `recap()` loop in history and summary paths with three queries, and
  `Storage::context_app_counts()` replaced the snapshot loops with one.

  The part worth reviewing is what was **preserved**: the per-session snapshot cap. It exists
  only because the old code passed a limit to a paginated API, but it changes the answer — it
  is what stops one very long session from dominating the app ranking — so it survives as a
  `ROW_NUMBER()` window function instead of being quietly dropped. Its tests pin both the cap
  and that it keeps the *oldest* rows, matching `list_context_snapshots`' `ORDER BY timestamp
  ASC`. The prediction aggregates are copied verbatim from `recap()`, **including the
  deliberate absolute 0.7 bar 5.4 warned against unifying** (ADR-0004 has since dropped the
  `AND focus_state` conjunct from both copies — still together), and the parity test
  compares batched output against `recap()` field by field rather than against hand-written
  numbers.

  **Not covered: whether even the completed part is fast enough.** The batched history and
  summary paths remove O(N) round trips, but nothing measures them. 7.11's large fixture
  landed 2026-07-31 and now pins *correctness* at
  12,000 rows — batched output matches the per-session path field by field across 60 sessions
  — but it deliberately asserts no timing, because wall-clock bounds on a shared CI runner buy
  flakes rather than signal. Still needs 4.4's perf gate. Treat the win as structural, not
  benchmarked.

  The original finding was:

  **Analytics and history do N+1 queries under the storage lock.** `analytics()`,
  `summary_report()`, and `session_history()` all loop over sessions issuing per-session
  queries:

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

- **7.13 — DONE 2026-07-22.** Added `idx_snapback_events_session` and `idx_labels_session`,
  with schema and `EXPLAIN QUERY PLAN` regression tests proving the session-filtered queries
  use them.

  The original finding was:

  **Missing indexes on two hot foreign keys.** `S`

  Indexes exist for `predictions(session_id, timestamp)`, `predictions(timestamp)`,
  `feature_snapshots(session_id, timestamp)`, `sessions(status, started_at)`, and
  `context_snapshots(session_id, timestamp)`. **None on `snapback_events(session_id)` or
  `labels(session_id)`.** `recap()` runs `SELECT COUNT(*) FROM snapback_events WHERE
  session_id = ?1` — a full scan — and per 7.12 `recap()` runs in a loop.

### Hygiene

- **7.14 — DONE 2026-07-31.** `S` **None of the four is public API any more**, verified by
  compiling a non-friend caller and watching all three surviving ones fail with *"is a private
  member of `snapback::AppState`"*.

  - **`start_pomodoro_for_test` is deleted outright.** 11.4's injected clock made it
    redundant: its test now sets a `ManualClock` and calls the real `start_pomodoro()`, which
    is strictly better than what it replaced — it exercises the production path instead of a
    parallel one that could drift from it.
  - **The other three are private**, reachable only through `AppStateTestAccess`
    (`tests/app_state_test_access.hpp`), which `AppState` befriends and nothing else can use.
    49 call sites converted mechanically.

  **What is honestly *not* fixed.** The item says these are "public production API compiled
  into the shipping binary." The public half is fixed; **the compiled-in half is not.** Those
  three cannot be deleted the way the pomodoro one was, because their production caller is the
  engine *tick thread* rather than a method — driving them through public API would mean
  starting the engine and waiting out real durations, which is exactly the sleep-based testing
  11.4 exists to avoid. Closing that gap needs a synchronous `tick_once` seam, which is a
  design question and not an access-control one. Recorded rather than glossed, because "no
  longer public" and "gone" are different claims and only one of them is true.

  `start_engine_for_test(InputHook*)` is deliberately left alone and is **not** an instance of
  this problem: injecting a fake hook is ordinary dependency injection, not a workaround for
  an unreachable clock.

  > **The benchmarks were a caller too, and only CI knew.** The first version of this change
  > converted the call sites in `tests/` and never grepped `benchmarks/` — but
  > `bench_snapback.cpp` and `bench_hotpaths.cpp` drive the same event seam, and
  > `Benchmark smoke / linux` went red on PR #40 with *"is private within this context"*.
  > It is invisible locally because benchmarks build only behind
  > `-DSNAPBACK_BUILD_BENCHMARKS=ON`, which no default configure sets, so a full local
  > `ctest` run stays green while the tree does not compile.
  >
  > **A grep scoped to `tests/` is not a grep for "who calls this."** Both benchmark targets
  > now take `tests/` on their include path and go through the same `AppStateTestAccess`
  > friend — non-shipping harness code either way, and the alternative was putting a test hook
  > back on the production class's public API. Verified afterwards by configuring with
  > benchmarks ON and running `snapback_benchmarks` the way the CI job does.

  The original finding was:

  `process_event_for_test`, `update_idle_for_test`, `start_pomodoro_for_test`, and
  `update_pomodoro_for_test` (`state.hpp`) are public production API compiled into the
  shipping binary. They exist because the tick reads a real clock. **The deeper fix is the
  seam:** inject a clock and they stop being necessary — which also lets you test
  idle/pomodoro/throttle interactions at real time scales without sleeping. Exactly the
  "find a testable seam" move that made 5.1's fix possible.

- **7.15 — DONE 2026-07-31.** `M` Every literal is now named in
  `src/engine/classifier_tuning.hpp` and grouped by **role rather than by location**, because
  role is what 1.2 and 2.3 actually need:

  - `policy` — what counts as distracted. Survives 2.3 (a model predicts a class; it does not
    decide that thrashing means distracted) and is **1.2's answer space**.
  - `scale` — normalisers turning a raw count or duration into 0..1. Claims about human
    behaviour: "four app switches in 30s is as thrashy as it gets."
  - `weight` — the linear-blend coefficients. **This is the group 2.3 replaces**; nothing
    else in the file is learnable.
  - `shape` — how the four class scores are carved out of one another. Structural, not dials.

  **The convex combinations are now `static_assert`ed.** `thrash`, `drift`, and `deep_work`
  each sum to 1, which is what keeps their scores in 0..1 without leaning on the clamp.
  Changing one weight without the others rescales the score instead of reweighting it, and
  that is now a compile error rather than a silent drift in every stored prediction.

  **On provenance: there is none, and that is the real finding.** The item asked to record
  "where each value came from." Git says every value arrived in the port commit `1cffcb9
  refactor: C++` with no derivation and no rationale, and history goes no further back for
  this file. So the header says that plainly rather than inventing a story. These are
  hand-tuned numbers never validated against labelled data; **naming them does not make them
  right, it makes them arguable**, which is the point.

  Two things the naming exposed that were invisible as bare literals. The **distracted
  accumulator is deliberately not a convex combination** — it sums to 0.80 with a negative
  term, so behaviour alone cannot reach a distraction of 1.0; entertainment context and goal
  misalignment carry the rest. Nobody recorded whether that ceiling was intended. And the
  **goal bias is applied twice at different strengths** (0.25 on drift, 0.35 on distraction)
  with nothing saying why distraction should be more goal-sensitive.

  **Verified as a pure extraction.** The feature-parity golden pins the *feature vector*, not
  classifier output, so it could not have caught a moved number here. Predictions were hashed
  over a 2,700-point grid of feature/mode combinations before and after: identical
  (`15294597689842095536`). 296/296 tests green.

  The original finding was:

  **Land before 1.2 is implemented, ideally before 2.3.**

  `classifier.cpp` carries dozens of unnamed constants: `0.45/0.25/0.30` in `thrash_score`,
  `4.0`, `10.0`, `180.0`, `8.0`, `120.0`, `0.65`, and the whole `distracted` accumulator
  (lines 161–169). Only `kThrashDistractedThreshold` and `kDriftPseudoThreshold` are named.

  Matters more than usual because **1.2** asks what "sensitivity" should tune — unanswerable
  while nobody can say which numbers are user-facing tunables vs structural — and **2.3**
  replaces some with a model while keeping others as the blend layer, and which-is-which is
  tribal knowledge living in one commit message.

  Extract to a named, documented table recording where each value came from and why.
  Not cosmetic.

- **7.17 — DONE 2026-07-26.** `capture_events_dropped` now means drops in the current
  capture run. `CaptureThread::start()` resets the counter only after it accepts a real
  restart, so a duplicate `start()` cannot erase live backpressure evidence. A restart
  regression test pins that health contract.

---

## Tier 8 — Security hardening (2026-07-20 staff review)

**No exploitable vulnerability was found.** Except for 8.1 — a real availability bug in
normal use — these are defense-in-depth and fragility items.

**What is already right**, recorded so a future review doesn't re-derive it: every SQL
statement is parameterized (no string-built queries in `storage.cpp`); no `innerHTML`,
`dangerouslySetInnerHTML`, or `eval()` in the frontend; the `popen`/`std::system` call site
in `permissions.cpp:18` takes a compile-time literal;
`training_deploy.cpp` quotes the user-supplied repo path; `npm audit --production` reports 0
vulnerabilities and the `security-audit` job is green; and the hook callback correctly
swallows all exceptions (`capture_thread.cpp:17`) since unwinding through an OS callback is UB.

> **This list went stale in the unsafe direction on 2026-07-25, which is worth recording as
> a pattern.** It used to say `active_window.cpp:32` also took a compile-time literal. It no
> longer does: `626ad87` changed `run_command()` from `const char*` to `const std::string&`
> so it could build a per-browser `osascript` command, and that command is chosen by the
> foreground **app name, which comes from the OS**. The code is still safe — it interpolates
> the matched allowlist *literal* rather than the caller's string, with a comment saying
> exactly why — but the property holding it safe changed from "the type system makes this
> impossible" to "a loop is careful," and nothing but that comment now enforces it.
>
> The general lesson: **a "what is already right" list is a claim with an expiry date.** It
> reads as reassurance, so it is the least likely thing in this file to be re-checked, and
> `check_doc_paths.py` cannot catch it — the path still exists, only the claim about it
> died. Re-verify this block whenever a signature it names changes.

- **8.1 — DONE 2026-07-22.** The engine loop now catches standard and unknown exceptions
  around each tick, logs the failure at Error level, and keeps the engine alive. The
  injected-hook regression test exercises the real background thread and verifies it stays
  online after an emit failure. The logger itself is guarded so a logging failure cannot
  escape the thread boundary.

  The original finding was:

  **The engine thread has no exception boundary; any throw kills the app.** `S`
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

  *A thread that dies on an exception must not take the process with it.*

- **8.2 — DONE 2026-07-26.** Host events now cross into the webview as escaped string
  literals and reconstruct payloads through `JSON.parse`; neither the event nor its JSON is
  executable source. ASCII-only encoding also keeps U+2028/U+2029 out of the evaluated
  script, with a webview-free regression test covering quotes and the line separator.

- **8.3 — DONE 2026-07-22.** Bundled `frontend/index.html` now declares a Content Security
  Policy with same-origin scripts, explicit font/style origins, and no arbitrary script sources.
  A frontend-assets regression test guards the policy's presence.

  The original finding was:

  **No CSP, and the IPC shim exposes every command to any script in the page.** `M`

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

- **8.4 — DONE 2026-07-22.** `SNAPBACK_FRONTEND_URL` is now debug-only; release builds load
  the bundled frontend and fail closed to `about:blank` when the bundle is absent.

  The original finding was:

  **`SNAPBACK_FRONTEND_URL` lets any local process redirect the webview.** `S`

  ```cpp
  w.navigate(resolve_frontend_url(executable_dir(), env_var("SNAPBACK_FRONTEND_URL")));
  ```
  `main.cpp:195` — read unconditionally in **release** builds. Any process that can influence
  the environment Snapback launches with can point the webview at arbitrary remote content,
  which then inherits the entire command surface from 8.3.

  Gate behind a debug build, or allowlist `http://127.0.0.1:*` / `http://localhost:*`. The
  sibling QA hooks (`SNAPBACK_OVERLAY_TEST`, `SNAPBACK_NOTIFICATION_TEST`,
  `SNAPBACK_GUI_SESSION_SMOKE`) are benign — keep them.

- **8.7 — DONE 2026-07-26.** `windows_demo.ps1 -UseVite` now builds and tests Debug,
  validates a loopback HTTP URL, and verifies the server is reachable before continuing.
  `-UseVite -SkipFrontend` reuses an existing server and fails loudly when none is running.
  Release demos retain the 8.4 bundled-frontend security boundary. The Windows CI smoke
  exercises the Vite/Debug route.

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

- **8.6 — DONE 2026-07-31.** `S` All three git dependencies are pinned to commit SHAs with
  the human-readable version in a trailing comment; SQLite already had a `URL_HASH`.
  [`docs/dependencies.md`](dependencies.md) is the update process, and
  `scripts/check_dependency_pins.py` fails CI on any `GIT_TAG` that is not a 40-character
  hash or any `URL` without a `URL_HASH` — verified by putting `v2.4.11` back and watching it
  go red. Policies that live only in a doc are the ones that rot.

  **The trap worth knowing about: `webview` uses an *annotated* tag.** `git ls-remote
  refs/tags/0.12.0` answers `782c12cc`, which is the **tag object**, not a commit — CMake
  cannot check that out. The commit is the peeled ref, `refs/tags/0.12.0^{}`. `nlohmann/json`
  and `doctest` use lightweight tags where the distinction does not arise, so the obvious
  command gives the right answer for two of the three pins and the wrong one for the third.
  That asymmetry is written down in `docs/dependencies.md` because it is exactly the kind of
  thing that gets rediscovered by a failing build a year from now.

  **`GIT_SHALLOW TRUE` survives the change** — GitHub serves `git fetch --depth 1 <sha>`, so
  the pins cost nothing in clone time. That was measured, not assumed. And the pins were
  verified by configuring a *fresh* build tree: `FetchContent` will not re-fetch into an
  existing `build/_deps`, so a configure against the usual tree would have proved nothing.

  **Deliberately still open:** nothing watches these projects for advisories. Pinning buys
  reproducibility, not currency, and arguably makes staleness *more* likely now that no tag
  quietly pulls in patch releases. That trade is recorded in `docs/dependencies.md` and
  belongs with 8.5's threat model rather than here.

  The original finding was:

  C++ deps come via CMake `FetchContent` (`webview/webview`, `nlohmann/json`, `doctest`).
  Dependabot covers Actions and frontend npm but **not** these — nothing watches them for
  advisories and nothing verifies what got fetched. Pin to commit SHAs rather than tags (tags
  are mutable) and note the update process.

- **8.8 — DONE 2026-08-04.** `S` `main.cpp` now passes `kWebviewDebugEnabled` instead of a
  literal `true`, and the rule lives in `src/app/frontend_assets.hpp` beside
  `resolve_frontend_url` — the same file already owns the other half of this boundary, so
  the two Debug-only concessions are now readable together instead of one being a flag at
  the top of `main.cpp` and the other an `#if` three hundred lines below it.

  **Verified in both configurations, which is the part the item asked for.** A test binary
  compiles in exactly one build kind, so the decision is split: `webview_debug_for_build()`
  is the rule as a pure function (both answers asserted from either build), and
  `kWebviewDebugEnabled` is this build's answer, with a third assertion tying the constant
  back to the rule. Then the suite was actually run twice — Debug (surface on) and Release
  (surface off) — rather than trusting the `#if`. Asserting only the constant would have
  left the release half unchecked in every local Debug run, and the release half is the
  entire point.

  The original finding was:

  `main.cpp` constructs `webview::webview` with `debug=true` unconditionally. That is useful
  for local development, but it belongs behind the same Debug-build boundary as the frontend
  URL override. The page owns a privileged native command surface, so production developer
  tools are a materially different risk than ordinary browser debugging.

- **8.9 — DONE 2026-08-04.** `S` Both ONNX jobs now read the version, URL, filename, and
  expected SHA-256 out of [`third_party/onnxruntime-pins.json`](../third_party/onnxruntime-pins.json)
  and verify the digest **before** extracting. The digests were captured by downloading both
  1.20.1 archives and hashing them, not copied from anywhere.

  **The manifest is what makes the guard non-trivial.** Recording digests inline in the
  workflow would have satisfied the letter of this item and still allowed the failure it
  exists to prevent: a version bump that edits the URL and leaves the old hash. Because the
  filename lives beside the digest and `scripts/check_onnx_pins.py` requires it to contain
  the manifest's version, that edit cannot pass — the two must move together.

  The guard was then written against its own failure modes rather than trusted: eight
  mutations were each confirmed to fail it (version bumped without re-hashing, one platform's
  digest pasted over the other's, a placeholder digest, verification moved after extraction,
  verification deleted, version or digest hardcoded back into the workflow, and the vendor
  step renamed). That last case is the one that matters most — a parser that silently stops
  finding steps reports success forever, which is the failure mode 8.6's guard also had to
  defend against.

  Verification runs before extraction, not after. Extraction is what writes archive-controlled
  paths to disk, so a check afterwards would already have lost.

  The original finding was:

  The Windows and Linux ONNX jobs download executable runtime archives from release URLs and
  extract them without verifying a digest. Item 8.6's guard covers CMake downloads, not these
  workflow downloads, so its broad claim that fetched dependencies are immutable was
  incomplete.

- **8.10 — DONE 2026-08-05.** `S` **release privacy** The Google Fonts stylesheet link and both
  remote CSP origins are gone from `frontend/index.html`, typography moved to system stacks in
  `styles.css`, and the dead `frontend/public/snapback.html` was deleted.
  [`scripts/check_no_remote_subresources.py`](../scripts/check_no_remote_subresources.py) now
  fails CI on any remote subresource, and it checks `dist/` too when a build is present, so
  the guard sees the artifact that actually ships.

  **System stacks rather than bundled font files**, which the item allowed and which also
  avoids adding font licences to 9.12's inventory. The named families are still listed first,
  so anyone who has them installed keeps the intended look.

  **`snapback.html` was a webview overlay from before 3.1.** Nothing referenced it — the
  overlays are native Win32/`NSPanel` windows — but it sat in `public/`, so Vite copied it into
  every build, shipping a second page that fetched fonts. Verified dead before deleting; it is
  in git history.

  **The guard was wrong twice before it was right**, both times in the direction that matters:

  - It **passed a CSP that re-permitted `fonts.googleapis.com`**. A policy is full of
    single-quoted keywords like `'self'`, so a `[^"']+` capture stopped at the first one and
    matched two words of the policy. Fixed with a captured-and-back-referenced quote character.
  - It **failed the real built page** over `rel="preload"` — which turned out to be a DOM
    selector string inside the inlined React bundle, not a link tag. Script and style bodies
    are now stripped before the markup checks (opening tags survive, so `<script src="https…">`
    is still caught), and only `preconnect`/`dns-prefetch` are flagged by `rel` alone.

  Seven cases are exercised against the real script: remote stylesheet, preconnect,
  re-permitted CSP origin, protocol-relative URL, remote script `src`, JS that merely mentions
  preload (must pass), and the clean page.

  The original finding was:

- **8.10 (original finding) — Make the shipped application network-silent by default.** `S`
  Opened 2026-08-05. `frontend/index.html` makes one Google Fonts stylesheet request covering
  two font families, and the CSP explicitly permits `fonts.googleapis.com` and
  `fonts.gstatic.com`. `frontend/public/snapback.html` contains its own separate request. At
  the same time the
  Privacy card says **"Nothing leaves this device"**, and `inline-bundle.mjs` says the shipped
  page makes zero subresource requests. Both promises are currently false on an online first
  launch; offline launch silently gets different typography.

  Use a system stack or bundle licensed font files locally, including their licence under
  **9.12**. Remove the remote CSP origins and the stale secondary HTML dependency. Add a
  release-build guard that rejects any `http:`/`https:` script, style, font, image, preconnect,
  or other automatic subresource in the final HTML. Verify the packaged page with networking
  disabled and with an outbound-request probe. Explicit user actions such as a future update
  check are a separate, disclosed policy; normal rendering must perform none.

- **8.11 — Add scoped capture rules before sensitive window titles reach the pipeline.** `M/L`
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

- **8.12 — DONE 2026-08-06.** `M` The matrix is written down in one place, both missed copies
  are removed, and the command reports what actually happened instead of returning void.

  **Two copies of the user's history survived every "delete all" the feature ever ran.**
  `exports/personal` is the worse of them: the most legible copy that exists — window titles
  verbatim, in Markdown — left on disk while the command reported success. `focoflow.db.pre-v<N>.bak`
  is a *complete* database copy that 7.22 writes before every schema upgrade and never
  removes. Backups are found by scanning for the filename shape rather than by asking the
  current schema version, because a database two upgrades old leaves two and a build that has
  since bumped `kSchemaVersion` would not know to look for the older one.

  **Source first, then every replica, and never stop at a failure.** The old order deleted
  exports first and *threw* on the first error, so one stale file held open by another program
  meant the database was never cleared at all — the user asked to erase their history, saw an
  error, and kept everything. That is the item's sharpest requirement and it now has a test
  with a real injected failure (a non-empty directory where a backup file belongs, which
  `remove` refuses), following 7.22's warning that its own failure test was vacuous first time.

  **`ActivityDeletionResult` replaces void**, listing deleted / failed / retained. The UI
  derives its message from it, so a partial erasure reads as "most of it, and here is what
  remains" and is styled as a warning rather than success. `activityDeletion.ts` holds that
  wording as pure functions with a `tsx` test — a wrong string here is a privacy claim, not a
  cosmetic slip, and the component suite cannot run locally (**11.11**).

  **One classification changed under test pressure, and the test was right.** Support bundles
  were initially moved into the delete set; an existing case asserted they are preserved, and
  on inspection the existing decision is the coherent one. A bundle's own privacy notice says
  it contains no database, session history, window titles, or captured input — it is a copy of
  the *log*, which this matrix keeps because it records operational events rather than captured
  content and is what a user needs most right after a destructive action. Deleting the copy
  while keeping the original would have been incoherent. Both are retained, and now **reported
  as retained** rather than silently omitted.

  Deferred as the item directs: the SQLite free-page/WAL question stays an output of **8.5**,
  and uninstall behaviour remains **9.5**'s. Four new C++ cases and one frontend module;
  suite **390 pass**. The original finding follows.

- **8.12 (original finding) — Make “Delete all activity” cover every app-owned copy of that
  activity.** `M`
  Opened 2026-08-05. `delete_activity_exports()` removes only `exports/training` and
  `exports/summaries`. It misses the readable `exports/personal/snapback_my_data.md`, and every
  successful schema migration can leave a complete `focoflow.db.pre-v<N>.bak`. The command
  can therefore report success while window titles and full session history remain under the
  app's own data directory.

  Define one tested artifact/privacy matrix covering the live DB and WAL, personal/training/
  summary exports, migration backups, support bundles, logs, settings, and model artifacts.
  “Delete activity” must remove every artifact classified as activity-bearing while preserving
  explicitly non-activity configuration. Make the SQLite free-page/WAL policy an output of
  **8.5**, not an undocumented assumption.

  Return a structured result listing what was deleted and what remains; success means the
  matrix is clean. Attempt every target even if one stale export is locked, so failure to
  remove a replica cannot also skip deletion from the source DB. Tests must create a personal
  export and a pre-migration backup, inject one filesystem failure and one SQLite failure, and
  prove the UI never says “permanently deleted” for a partial result. This is the in-app
  privacy boundary; **9.5** separately owns uninstall behavior.

- **8.13 — DONE 2026-08-07.** `S/M` **release privacy** `src/util/private_dir.cpp` centralizes
  the data root: created owner-only, verified on every launch, repaired where safe, and
  reported where not.

  **The predictable-temp fallback was the widest hole and it is gone.** Both platforms ended
  `return temp_directory_path() / "snapback"` — the same path for every account, inside a
  directory every local account can write to. A user with no `HOME` (or no `%APPDATA%`) got a
  *working* app quietly recording their window titles somewhere anyone could read, with nothing
  said. There is no safe automatic answer, so there is no automatic answer: the app now refuses
  to start and names `SNAPBACK_DATA_DIR` as the fix.

  **Race-safe creation, as the item requires.** `mkdir(0700)` applies the mode as the directory
  comes into existence, and `umask` can only clear bits — never add them — so no umask can
  widen it. Create-then-`chmod` would leave the directory readable for the interval between the
  two calls, which is exactly the window an attacker on a shared machine wants.

  **The directory is the boundary and the file sweep is defence in depth**, stated that way
  rather than left implied: a `0700` directory cannot be traversed, so files beneath it are
  unreachable at any mode. The sweep still tightens them, because an upgraded install has
  `0644` files that stay readable if one is ever copied out.

  Fails closed on a symlink, a non-directory, or a root owned by another account — all three
  are substitution attacks on a predictable path and none has a safe automatic resolution. A
  symlink *inside* the tree is reported rather than `chmod`-ed through, which would let a
  planted link retarget the permission change at a file outside the data directory. `chmod` is
  verified by re-`lstat` rather than trusted, because it succeeds silently on filesystems that
  do not store the bits.

  **Honest limit on verification.** The POSIX permission cases — the substance — are
  `#if`-guarded and run in CI only; this machine is Windows. So the *rules* were extracted into
  `private_mode_for` and `mode_exposes_others` and tested everywhere, including that the mask
  is `0077` rather than `~0700` (a real `st_mode` carries file-type and setuid bits above
  `0777`, and confusing them would report every directory as exposed). What is left
  CI-only is `lstat`/`mkdir`/`chmod` plumbing. Twelve new cases; suite **408 pass**.

  **Not done, and not claimed:** the Windows DACL walk. The item asks to validate that the
  selected root's inherited ACL grants no unintended local principals; what landed checks the
  root is a real directory under the user's own `%APPDATA%`/`%LOCALAPPDATA%` and creates it
  there. A `GetNamedSecurityInfo` ACE enumeration is a separate piece of work and is left
  open here rather than reported as covered. Encryption and the full adversary model remain
  **8.5**'s; deletion remains **8.12**'s. The original finding follows.

- **8.13 (original finding) — Enforce an owner-only boundary around app-owned local data.**
  `S/M` **release privacy**
  Opened 2026-08-05. On POSIX, `~/.snapback` and its DB/settings/log/export children are created
  with ordinary `create_directories`/ofstream/SQLite defaults, so a common umask of `022` may
  leave the directory `0755` and files `0644`. When no home directory is available, both
  Windows and POSIX fall back to the same predictable `<temp>/snapback` path. The lock file is
  the only artifact that explicitly requests `0600`.

  Centralize creation of the data root and every sensitive app-owned artifact. POSIX requires
  directory `0700` and file `0600` even under a permissive umask; use race-safe creation rather
  than creating broadly and tightening later. Never fall back to a cross-user predictable temp
  directory: create a verified private per-user directory or fail closed with a visible reason.
  On Windows, validate that the selected root's inherited DACL grants no unintended local
  principals rather than assuming `LOCALAPPDATA` is always configured normally.

  Audit and repair safe existing installs, but report entries whose owner/ACL cannot be changed
  instead of claiming protection. Tests must cover a fresh tree under umask `022`, a permissive
  upgrade tree, symlink/reparse-point substitution, missing HOME/LOCALAPPDATA, and default
  exports/backups/log rotation. A user-selected external export may inherit the chosen
  destination; the app-owned default stays private. **8.5** still decides encryption and the
  full adversary model, while **8.12** owns deletion rather than access control.

- **8.14 — Confine privileged webview commands to the trusted packaged UI.** `M/L`
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

## Tier 1 — Ship a polished Windows-first v1

- **1.2 — CLOSED 2026-08-03, no code.** [ADR-0004](adr/0004-verdict-and-opinion.md):
  **the focus mode *is* the sensitivity control.** Deep / Normal / Recovery are three
  settings of `risk_threshold()` (0.55 / 0.70 / 0.85), chosen per session, with the
  onboarding wizard setting the default. A second control would be a fourth way to move one
  number.

  The observation that closed it: 7.15 found these values have **no provenance** — hand-tuned
  in the port commit, never validated against labelled data. Layering a user multiplier on
  top of three unjustified constants compounds the guess instead of resolving it. If evidence
  ever demands finer control, the extension point is a per-mode threshold override in
  `AppSettings` — an offset to *policy*, never a transform on the model's opinion.

  App-rule management (`RulesCard`) and default focus mode were already done — see Done
  archive. This item was the last `decision` on the v1 blocker list.

---

## Tier 2 — Product & ML depth

- **2.7 — PARTIAL 2026-08-10; dismissal interval DONE.** `M` The nudge is
  implemented and wired end to end. After **15 minutes** of sustained activity with no
  session, the engine emits `untracked_work` once and the Session Control card shows a
  dismissible notice — placed there, not as a toast, because the Start button is on the same
  card.

  **Latched, and reset by idle, private mode, and excluded apps.** One nudge per stretch, not
  one per tick. Going idle ends the stretch, so returning to a machine the user already
  declined to track starts a fresh count. **Private mode and excluded apps do not advance the
  timer and reset it**, because prompting someone to start recording while they have said "do
  not record" (globally or for this app) is the wrong direction — and leaving that state must
  not fire a nudge earned while it was on. Eight cases cover it, including the silent ones
  (session running, stretch too short, private mode, excluded app, stretch reset by switching
  into an excluded app).

  Dismiss now suppresses the nudge for one hour, persisted as a wall-clock deadline so closing
  and reopening the app cannot immediately nag again; a failed settings write leaves the notice
  visible. **Still open from this item's own list:** the notice does not itself open the goal/mode
  start flow (it relies on the adjacent Start button, so coordinate with **2.11** before adding a
  second entry point). The threshold is a named constant rather than a setting — that belongs
  with 7.23's inherited five-minute idle threshold, which raises the same question.

  The original finding was:

- **2.7 (original finding) — Remind an active user who forgot to start a session.** `M`
  [ADR-0005](adr/0005-a-session-is-declared-and-attended.md) settles the consent question:
  sessions remain manually declared and Snapback never auto-starts recording. With no active
  session, capture events still pass through in-memory feature extraction/classification, but
  no prediction, feature snapshot, or context row is persisted. The product closes the missing
  history gap with a latched nudge rather than an inferred/untagged session.

  Implement "you have been active N minutes with no session; start one?" using the existing
  idle/activity signal. It fires once per continuous active stretch, resets after idle or an
  explicit dismissal interval, and opens the deliberate goal/mode start flow rather than
  creating a session itself. Private mode and excluded apps cannot advance the nudge timer.
  Add injected-clock tests for activity, idle reset, dismissal, session start/stop, and app
  restart. Coordinate the prompt with **2.11** so it reuses the same validated start action.

  The rejected auto-start options remain important context: a session has a declared goal,
  `goal_alignment` feeds the model, and window-title capture is consent-sensitive. Guessing a
  goal or writing untagged rows would weaken both the training corpus and the privacy promise.

- **2.8 — The snapback knows exactly where you were and cannot take you there.** `M`
  Opened 2026-08-05. `SnapbackPayload` carries `app_name`, `window_title`, `file_hint`, and a
  rendered `summary` — "You were editing auth.ts in Cursor". The only command bound to it is
  `dismiss_snapback`. The user reads where they left off and then navigates back by hand.

  **This is the namesake feature stopping one step short of its own promise.** Everything
  needed to finish the job is already captured and already on screen; the product recognises
  the return from distraction, reconstructs the context, renders it, and then asks the user to
  do the part it could do for them. The gap is an action, not information.

  Add a "Take me back" action beside Dismiss that raises the recorded window.
  [`src/app/reveal_path.hpp`](../src/app/reveal_path.hpp) is the worked example of the shape
  this needs: `NSWorkspace` on macOS, `ShellExecuteW` on Windows, an argv array on POSIX,
  never a shell. Window activation is the harder cousin of path revealing —
  `SetForegroundWindow` has focus-stealing restrictions on Windows and macOS needs the target
  app's activation policy to cooperate — so treat "could not raise it" as an ordinary outcome
  with an honest message, the same way `open_data_folder` returns the path whether or not the
  open worked.

  Two things to decide while building rather than after. `file_hint` is produced by
  `title_parser`, whose fabrication bug is **4.11** — acting on a fabricated filename is worse
  than displaying one, so this should land after that decision. And raising a window is a
  visible interruption, which is exactly what a focus tool must be careful with: it should be
  the user's click, never automatic.

- **2.9 — Turn session history into a real session explorer.** `M/L`
  Opened 2026-08-05. Past sessions currently appear chiefly inside the destructive "Delete a
  session" area, history is capped at 20, and the context timeline is tied to the current
  session. The app records the detail needed to explain a workday, but Review cannot answer
  the basic question "what happened in that session last Tuesday?"

  Add ordinary selectable history, separate from deletion. A detail view should combine the
  goal/mode/times, recap, focus curve, snapbacks, captured context, and labels for any chosen
  session; support goal/app/date search, mode/verdict filters, and cursor-based pagination.
  Deletion stays a secondary confirmed action, and "repeat this goal" should hand off to the
  deliberate start flow rather than begin recording on selection. Do not load the entire
  database into the browser. This needs per-session query commands and should follow **7.16**
  and align with **14.3**'s command contract.

- **2.10 — DONE 2026-08-10.** `M`
  Opened 2026-08-05. The header says capture is running or idle without incorporating private
  mode, the private toggle is buried in Settings, and the tray offers only Show and Quit. For
  software that observes window titles, "am I recording right now?" should never require
  navigation.

  Define one status model shown in the header and tray: **Recording**, **Paused for idle**,
  **Paused privately**, **No session**, and **Blocked/error**. Provide one-click privacy pause/
  resume plus 15/30/60-minute pauses with a visible remaining time. A timed pause must warn
  before processing resumes and survive window close/reopen without silently changing meaning.
  The tray and dashboard must drive the same command and cannot disagree. Build after **7.26**
  and consume **7.23**'s running/paused state rather than inventing a frontend-only version.

  **Done:** the five-state model — Recording, Paused for idle, Paused privately, No session,
  Blocked — derived by `derive_recording_state` as a pure function of capture health, private
  mode, session presence and 7.23's idle state, and exposed through one command. Precedence is
  pinned by test: Blocked outranks everything because nothing else can be true when capture
  cannot run, and Paused privately outranks both No session and idle because it is the reason
  the *user* chose and stays true when they start a session or return to the keyboard. Also
  one-click privacy pause/resume and 15/30/60-minute pauses with visible remaining time, stored
  as a wall-clock deadline in settings.json so a pause survives close/reopen without silently
  changing meaning, and lapsing itself once the deadline passes. A resume that cannot be
  written down leaves the app paused — failing closed, because resuming on a promise the next
  launch will not remember is the worse error for this particular feature.

  **The tray is wired on Windows and macOS:** opening its menu asks the same native status
  command source used by the dashboard, shows the five-state answer, and offers Pause or Resume
  as appropriate. The actions call the same durable privacy-pause methods as the dashboard;
  neither platform derives a parallel answer. Linux remains under its existing tray stub.

- **2.11 — DONE 2026-08-10 except the Pomodoro preset and Running/Paused elapsed split.** `M`
  Opened 2026-08-05. Start and Stop are always enabled, a blank goal silently does nothing,
  duplicate clicks can issue duplicate requests, and the prominent running-session metadata
  is a raw UUID rather than elapsed work time. Every new UI session also starts with an empty
  goal even though recent history already knows what the user repeats.

  Idle state should offer inline goal validation, Enter-to-start, recent unique goals,
  **Repeat last**, and reorderable pinned presets containing goal + focus mode (optionally a
  Pomodoro preset). Active state should show goal, mode, **Running/Paused** from 7.23, live
  elapsed/active time, one Stop action, and an explicitly guarded "start a different session"
  path. Disable actions while
  pending, make duplicate clicks idempotent, and move the UUID into copyable technical
  details. Presets never auto-start, so this reduces friction while preserving ADR-0005's
  explicit-declaration boundary. Backend timestamps, not a browser-only counter, remain the
  source of truth.

  **Done:** `frontend/src/sessionCockpit.ts` holds the rules as pure functions — goal
  validation, recent-goal dedup, pinned presets with reorder/persistence, elapsed formatting,
  and the two action gates. The card grew inline validation, Enter-to-start, recent-goal chips,
  **Repeat last**, pinned presets with keyboard-reachable up/down, a live elapsed readout, and a
  guarded "start a different session" that stops the old one first. The session UUID moved into
  a collapsed **Technical details** block — still copyable, because support instructions ask for
  it, just no longer occupying the spot where elapsed time belongs.

  **Duplicate clicks are stopped twice, and the second guard needed its own test to be real.**
  The card disables its controls while a request is in flight, which also closes the Enter path
  a `disabled` attribute does not. Underneath, `useSession` holds a ref that is read and written
  synchronously, because `sessionPending` is React state and cannot disable anything until the
  next render — two calls in the *same* turn both see the old value. The first draft asserted
  that ref through the DOM and **passed with the ref deleted**: the card's own gating masked it.
  It is now asserted at the hook, where it is the only thing standing between two calls and two
  session rows, and it fails when the ref is removed. This is the file's own rule about empty
  checks, met by a check that was empty until it was mutated.

  **Left open, both because they depend on work that has not landed:** the optional Pomodoro
  preset waits on 2.13's remaining tray/native half, and splitting the elapsed readout into
  elapsed *and attended* time needs 7.23's spans — inventing a second attended number here is
  how two figures that must agree stop agreeing. Running/Paused itself is already shown, from
  7.23's `sessionStatusLabel`.

- **2.12 — DONE 2026-08-11.** `M`
  Opened 2026-08-05. `PermissionWizard` disappears once capture is available. It explains OS
  permission and default mode, but never teaches the product loop: choose a goal, start, read
  a verdict, correct it, stop, and inspect the recap. A successful permission grant is not
  the same as reaching value.

  Add an optional, state-driven continuation that walks through one short real session and
  its Review result. Each step advances from actual app state rather than a Next button;
  failures route to the relevant recovery UI. It must be skippable, resumable from Help, and
  safe to repeat without generating fake records or labels. Instrument only local completion
  state — no analytics service is implied by this item. This extends completed **1.1** rather
  than replacing the OS-permission wizard.

  **Done:** `onboardingJourney.ts` derives the current step from app state; `OnboardingGuide`
  renders it on Now, above the cockpit, from capture-ready until the recap has been read. Six
  steps — goal, start, verdict, correct, stop, review — each advancing because the state
  changed, not because a button was pressed. There is no Next button anywhere in it, and a
  test asserts that there isn't.

  **The design decision that makes the rest fall out: the guide is a pure observer.** It
  issues no commands and writes no rows. That is what makes it safe to repeat — a walkthrough
  that started a session to demonstrate starting a session would manufacture exactly the fake
  records and labels the item forbids — and it is why "resumable from Help" cost nothing:
  replaying it creates no second session and no duplicate label, which is asserted directly.
  Skip and finish write the same single local flag; nothing else is instrumented.

  **Deriving the step instead of counting it is what handles the user who ran ahead.** Each
  step's evidence is a superset of the ones before it, so someone who started a session without
  reading the guide lands on "wait for your first reading" rather than being told to type a goal
  over a running session. Failures hand off rather than talking over the problem: a dead capture
  or private mode routes to 10.9's Privacy & permissions section instead of growing a second
  set of remediation copy that would have to be kept true.

  **Two things this pass got wrong.** The progress track labelled every dot with its step title
  for screen readers, which meant a reader would recite the whole journey on each advance; the
  dots are decorative now, and the live region plus the step count carry the meaning. And the
  new state shifted timing enough to expose that two of 10.9's own tests were passing on luck —
  `App.test.tsx`'s default health has capture blocked, so the auto-reveal was racing the
  explicit navigation those cases were about. They now opt into a healthy capture and test one
  thing each; the suite was run three times to confirm it.

- **2.13 — DONE 2026-08-10 except tray countdown and native alerts.** `M`
  Opened 2026-08-05. The existing state machine hardcodes 25/5/15-minute phases, while IPC and
  `PomodoroCard` expose only start and stop. There is no pause/resume, skip, restart, long-break
  cadence setting, or durable indication when the main window is hidden.

  Persist work/break lengths and long-break interval; add pause/resume, skip, and restart;
  explicitly decide optional automatic phase starts; and deliver in-app/native phase alerts.
  Remaining time must be visible from the tray while the dashboard is hidden. Persist the
  active phase and its wall-clock deadline: relaunch resumes a future deadline, while an
  already-passed deadline restores at zero and waits for acknowledgement rather than silently
  advancing through phases while the app was absent. State-machine tests must cover restart,
  that relaunch policy, session replacement, and simultaneous session stop.
  macOS native alerts remain downstream of **3.3**, but the portable behavior need not wait.

  **Decided while building, as the item asked:** automatic phase starts are a setting,
  **default on**. That is what the timer has always done, and changing the rhythm under
  existing users to settle a design question would be the worse answer. Turned off, a phase
  that ends reports its boundary once — so the UI can still alert — and then waits for
  `acknowledge()`. **Also decided:** a *skipped* work phase is not credited as a completed
  interval, because the long-break cadence is a reward for work actually done and counting
  skips would let four clicks earn a long break.

  **Done:** `PomodoroConfig` carries the lengths, the long-break cadence and the auto-start
  flag, persisted in settings.json and applied from the next phase rather than restarting the
  one in progress; pause/resume, skip, restart and acknowledge on the timer, AppState and IPC;
  and the relaunch policy via a wall-clock snapshot — a monotonic deadline is meaningless
  after a restart, since that timeline begins again with the process. A deadline still in the
  future resumes with the time genuinely left; one that passed while the app was closed
  restores **at zero, awaiting acknowledgement**, never chaining through phases nobody was
  present for. All four named test cases exist, plus pause/resume, skip, auto-start-off, and
  the stopped/paused/awaiting restore paths.

  **The card is wired:** pause/resume swap in place, skip and restart act on the phase in
  progress, and an ended phase shows "Done" with a single *Start <next phase>* button — skip and
  restart are withheld there, because there is no phase running to skip or replay. Covered by
  `frontend/tests/pomodoroFlow.test.tsx` driving the real card, hook and api against a mocked boundary.

  **Still open:** remaining time in the tray while the dashboard is hidden, and in-app/native
  phase alerts. Both are platform surfaces rather than state-machine behaviour — the timer
  already reports each boundary exactly once, which is the hook they need. The Now card now
  exposes **Customize rhythm**, hydrates all duration/cadence fields from persisted settings,
  and saves them through the existing command without restarting the phase in progress.

- **2.14 — DONE 2026-08-10.** `M`
  Opened 2026-08-05. The current check-in records only a coarse focus label and the recap is
  metrics-only. That helps model training, but it does not preserve what the user accomplished
  or the next step that makes tomorrow's restart easier.

  Add optional "What got done?" and "Next step" fields, with Skip remaining one click. Store
  them separately from ML label notes; exclude them from training unless a later decision says
  otherwise. Reflections are editable from the session detail in **2.9** and appear in the
  readable personal export. This requires an append-only schema migration; use **7.22**'s
  pre-migration backup and add a real prior-version upgrade case for the new field.

  **Done:** schema v6 adds two nullable columns to `sessions` (append-only, idempotent, no
  backfill); `save_session_reflection` on Storage and AppState; the `save_session_reflection`
  command with trim/blank/length validation; the reflection section in the readable personal
  export; and `saveSessionReflection` in the frontend API with the mapped record fields. The
  prior-version upgrade case the item asks for is real — it rewinds a v6 database to v5 by
  dropping the columns, reopens, and proves the session survives and the columns are writable.
  Answers are stored on `sessions`, never on `labels`, which is what keeps them out of training
  data by construction rather than by the exporter remembering.

  **The form is wired:** a Reflection card appears with the end-of-session check-in on Review,
  Save is disabled until there is something to save, and Skip is one click that writes nothing
  at all — so a skipped reflection stays indistinguishable from one never offered, which is the
  promise the storage half was built around. Covered by `frontend/tests/reflectionFlow.test.tsx`.

  **Editing is wired too:** every completed-session row exposes an editor prefilled from the
  stored answers. Saving updates that row immediately through the same validated command,
  including clearing either answer back to unset. **2.9** may move this editor into its richer
  session-detail view later; it no longer blocks correcting a reflection today.

- **2.15 — PERSISTENCE DONE 2026-08-06; the explorable half stays open.** `M`
  The item's own two halves, split as it describes them: "first persist", then "compose those
  rows into 2.9 and 10.11". The first is done and the second is still owned by those items.

  **The defect was as bad as it reads.** There was no production `INSERT INTO snapback_events`
  anywhere in the tree, and `recap()` has counted rows in that table since the baseline schema.
  Every user's Snapback count was therefore **zero**, always — and the only non-zero values
  ever observed came from hand-seeded test databases, which is precisely why nothing caught it.

  Migration 5 extends `snapback_events` with `started_at`, `duration_secs`, `app_name`, and
  `file_hint`. Columns added, not a table recreated: a pre-versioning install already has the
  table, so `CREATE TABLE IF NOT EXISTS` would skip it and the columns would never appear —
  the same trap `migrate_prediction_model_id` documents.

  **Idempotence is a UNIQUE index, not caller discipline.** An episode is identified by
  `(session_id, started_at)`, so a duplicate tick, a delivery retry, or a restart that
  re-drains the payload lands on `INSERT OR IGNORE`. The first write wins, so a retry that
  re-derives slightly different text cannot quietly rewrite what the user was shown. SQLite
  treats NULLs as distinct, so pre-2.15 rows neither collide nor block the index.

  **The distracting app is deliberately not recorded.** The episode stores where the user
  *was* — the route back the overlay offered — not where they went. Storing the other half
  would turn an interruption log into a browsing history, which is not what this table is for.

  **Two guards found while wiring it.** `compute_event` returned early on both the AFK-freeze
  and prediction-throttle paths unless a context snapshot existed; an episode usually rides
  along with one, but nothing guarantees it (the snapshot is suppressed for an unnamed window),
  and the job would have been dropped. Both guards now check for either.

  The regression drives capture → tracker → persistence → recap, as the item demands —
  inserting a row in a storage test would have reproduced the blind spot rather than closed it.
  Deleting a session takes its episodes; private mode records none; the readable personal
  export now lists them under **Interruptions** instead of stating a count the reader cannot
  check. Verified by mutation: disabling the one new `persist()` branch turns four cases red.
  Ten new cases; suite **386 pass**.

  **Still open here:** the exploration surface — what interrupted a selected session, total
  time lost, common contexts — which belongs to **2.9** and **10.11**, and calendar grouping
  after **7.16**. The original finding follows.

- **2.15 (original finding) — Persist distraction episodes, then make them explorable.** `M`
  Opened 2026-08-05. This begins as a correctness repair. `ContextTracker` produces a rich
  `SnapbackPayload`, `AppState` emits it, and `Storage::recap()` reports a count from the
  `snapback_events` table — but there is no production `INSERT INTO snapback_events` anywhere.
  `PersistJob` saves only context, prediction, and feature rows. Consequently the recap's
  Snapback count is effectively always zero outside manually seeded test databases.

  First persist exactly one episode on the same activity-epoch/transaction boundary as the
  event that emits it. Extend the schema beyond summary/timestamp with the minimum useful
  facts: episode start and return time, duration, and privacy-safe context references. A
  duplicate tick, dismissal, restart, or delivery retry must not create another row. The
  production-path regression must drive capture → tracker → persistence → recap; inserting a
  row directly in a storage test would preserve the current blind spot.

  Then compose those rows into **2.9** and **10.11**: show what interrupted a selected session,
  total time lost, common episode contexts, and the route back, with honest no-data states.
  Deleting a session and the readable personal export must include the same episodes. No row
  or raw context may be produced while private/excluded, and one deterministic fixture must
  prove that the recap count and episode rollup describe exactly the same population. Land
  calendar grouping after **7.16**.

- **2.16 — Let the user control when and how Snapback interrupts.** `M`
  Opened 2026-08-05. A snapback currently triggers both the native overlay and a native
  notification unconditionally; hyperfocus has its own unconditional toast, while Settings
  has no delivery fields and the tray offers only Show and Quit. The same snapback summary can
  also put a filename or project hint into OS notification history.

  Add per-event delivery preferences for snapback, hyperfocus, and Pomodoro; channel choices
  for in-app/overlay/native delivery; quiet hours; a tray **Snooze alerts for 30 minutes**
  action; and **Detailed / Generic** lock-screen preview copy. Recording continues during an
  alert snooze and the status model in **2.10** must say so — silencing an intervention is not
  privacy mode. Default routing should produce one visible intervention per logical event,
  with “both” available only as an explicit preference.

  Quiet ranges must work across midnight and local clock changes. Snooze must survive a hidden
  window/reopen, and a generic preview may contain no app, title, file, project, or goal text.
  Most importantly, suppressing a snapback must still acknowledge/re-arm `ContextTracker`;
  otherwise it stays Recovering and the user's first quiet-hour event silently disables every
  later snapback. Use injected clocks and delivery fakes. Depends on **7.16**, **7.26**, and
  the shared recording state in **2.10**.

  **Action routing belongs in this delivery layer rather than a second notification item.** A
  click on the missed-session nudge opens the session composer without auto-starting; a
  snapback routes to the user-initiated Return/Dismiss actions in **2.8**; Pomodoro/hyperfocus
  opens its current phase or break workflow. Carry stable event/session identity, activate the
  owner window through **9.15**, and make stale or duplicate clicks harmless. Generic-preview
  mode changes copy, not destination authority. macOS action delivery remains downstream of
  **3.3**, while the portable intent/idempotence contract can be tested now.

- **2.17 — Give feedback an authoritative, editable label ledger.** `M/L`
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

- **2.18 — Teach Snapback from the context the user is already looking at.** `M`
  Opened 2026-08-05. Personal Rules currently asks the user to navigate to Settings and type a
  substring from memory. That substring matches both app name and title, and every rule is
  global. Meanwhile verdict feedback and the Review timeline already hold the exact app,
  title, file, project, and session goal that caused a wrong reading.

  Add **Usually on task** / **Usually distracting** actions beside a live correction,
  distraction episode, and context row. Before saving, require an explicit scope — exact app,
  title pattern, or app plus stable goal-category id from **7.28** — and preview how many stored
  examples would match. Existing rules remain global; this adds a narrower tool rather than
  silently changing their meaning. One-click means one place to start, not an unreviewed rule.

  Save applies to the next classification tick and offers Undo that restores the exact prior
  rule set. Pin conflict precedence when a global and goal-scoped allow/block both match, and
  prove the same Slack/Chrome context can resolve differently for Coding and Communication.
  State clearly that this tunes classification; it neither blocks applications nor redacts
  capture (that is **8.11**). Depends on stable context identity in **7.27**, stable category
  identity in **7.28**, and title-parser policy in **4.11** for title-derived suggestions.

- **2.19 — FIRST SLICE DONE 2026-08-09; Review comparison stays open.** `M`
  Opened 2026-08-05. Sessions have a free-text goal and Review has retrospective totals, but
  nothing records the user's intention for today or this week. A focus score is unsuitable as
  a target — it is a model opinion — while **7.23** now provides the honest quantity a user can
  plan: minutes actually present in declared sessions.

  Add opt-in daily and weekly attended-minute targets, off by default. Show unobtrusive live
  progress on Now and planned-versus-actual in Review; do not add guilt copy, forced streaks,
  or notifications unless separately enabled through **2.16**. The first slice is total
  attended time only. Category allocations and calendar scheduling can wait for evidence that
  the simpler plan is useful.

  **Done:** opt-in daily and weekly attended-minute targets in settings.json, **0 meaning no
  target** so there is no separate enabled flag to drift out of step with the number; an
  Attended time card on Now showing today and this week with the ratio when a target is set and
  the bare measurement when it is not; and `get_attended_progress` / `set_attended_targets`.
  Totals are summed from `session_spans` and nothing else — never session-open duration,
  prediction rows, or a classifier score. Spans are **clipped to the window** rather than
  counted whole, so an evening past midnight lands its real minutes on each day, and sessions
  predating spans contribute zero rather than an invented total. Minutes round down: 59 seconds
  is not a minute of attendance. Tested for cross-window spans, open spans bounded at now, the
  Monday off-by-a-week case, and legacy sessions with no spans.

  **Local day/week boundaries** use `datetime(col, 'localtime')`, the same convention the
  hourly analytics buckets already use, rather than waiting on **7.16** — which may still
  revisit the representation. DST is not special-cased because SQLite resolves `localtime` per
  timestamp, so a repeated or missing hour is handled by the conversion rather than by
  arithmetic on a fixed offset.

  **Still open:** planned-versus-actual in Review, which the item says should compose with
  **10.11**'s shared time range rather than introduce another hidden one; 10.11 does not exist
  yet. Target *editing* lives on the Now card for now rather than in Settings.

  Count durable active spans, never session-open duration, prediction rows, or classifier
  score. Define local-day/week boundaries after **7.16** and test idle/paused sessions,
  cross-midnight spans, DST, target edits, disabled targets, and legacy sessions with no span
  measurement. Review comparison composes with **10.11** rather than creating another hidden
  date range.

- **2.3 — Model retraining loop.** `L` — **blocked on 13.7.**
  The intended loop is exported CSV + the user's labels → a fresh `model.onnx`, opening the
  door to on-device personalization. The earlier entry assumed an `ml/` trainer was present;
  the 2026-08-05 audit found that directory absent while the UI still requires it. **13.7 must
  first decide whether this is a packaged user feature or developer tooling.**

  **Do not start this as one item — it is at least seven.** The operational half (versioning,
  evaluation gates, rollback, drift, and whether enough labelled data even exists) is broken
  out in **Tier 13**, and **13.5 may rescope 2.3 entirely** if the label corpus turns out too
  small to train on. Also: bundle **5.6** here (it needs both extractors changed together
  plus a retrain), and fix **7.5** first or the corpus stays biased toward
  deliberately-ended sessions.

---

## Tier 3 — Cross-platform breadth & packaging

- **3.0 — DONE 2026-07-30.** `M` — autostart on macOS and Linux. macOS installs a launchd
  `LaunchAgent` plist in `~/Library/LaunchAgents`; Linux writes a systemd **user** unit *and*
  the `graphical-session.target.wants` symlink that `systemctl --user enable` would create —
  writing the unit alone leaves it installed-but-off, which would show a checked toggle with
  nothing starting. Neither backend spawns `launchctl` or `systemctl`.

  Both mechanisms live in their own translation units
  (`src/app/autostart_launchd.cpp`, `src/app/autostart_systemd.cpp`) that **compile and are
  tested on every OS**, with the target directory passed in as an argument. That is a direct
  answer to **11.7**: the tests run against a temp directory and cannot register anything to
  start at login. It is not hypothetical — the first run after the launchd backend landed,
  the *old* `test_autostart.cpp` case (which asserted "no backend off Windows" by calling
  `set_autostart_enabled(true)`) registered the **test binary** as a real login item on the
  dev Mac. A test asserting a no-op stops being harmless the moment the no-op gains an
  implementation.

  Escaping is the part with teeth in both: XML for the plist (`&` in a path makes it
  unparseable, and launchd ignores a malformed agent *silently*), and systemd's `%` specifier
  for the unit (`%h` expands to the home directory). Generated plist output was checked with
  `plutil -lint`. Neither backend sets `KeepAlive`/`Restart=`, so a login item cannot relaunch
  itself when the user quits.

  Not covered: desktops that do not integrate with systemd never reach
  `graphical-session.target`, so the unit is written but never triggered there. The XDG
  `~/.config/autostart/*.desktop` fallback is the follow-up if a Linux user reports it.

- **3.1 — macOS tray + native overlay. DONE 2026-07-28.** `M` — second ADR-0002 v1 release
  blocker cleared. Scope was settled 2026-07-25 by
  [ADR-0002](adr/0002-v1-supports-windows-and-macos.md): macOS v1 ships a **native
  `NSPanel` overlay**, not a notification.

  What landed, in four commits:

  - `src/app/tray_macos.mm` — an `NSStatusItem` tray. The menu rows moved into
    `src/app/tray_common.cpp` as `tray_menu_entries()`, so Windows and macOS translate one
    shared list instead of each owning a copy that can drift.
  - `src/app/mac_ui.mm` — the AppKit shim behind "Show Snapback", kept out of `main.cpp` so
    that translation unit stays plain C++ rather than Objective-C++.
  - `cocoa_origin_y()` in `src/snapback/overlay_common.cpp` — the top-down → Cocoa
    bottom-up placement flip, pure and tested on all three CI hosts.
  - `src/snapback/overlay_macos.mm` — the panel itself, matching
    `src/snapback/overlay_windows.cpp` on geometry, colours, 9s self-dismiss, and
    click-to-dismiss.

  **Verified by running the app on Kassa's Mac**, not just by building it: the card lands
  top-right below the menu bar, a synthesized click dismisses it, it self-dismisses at 9s,
  the tray menu renders both items enabled, and its Quit ends the run loop.

  Both things the stubs warned about were honoured. `ContextTracker`'s `Recovering` state
  still has exactly **one** exit (`dismiss_recovery`), so the panel's content view answers
  `hitTest:` with itself — otherwise the text label would swallow the dismiss click and
  latch the state machine after the first snapback of a session. And
  `show_notification()` still returns `false`: delivery needs a bundle ID and waits on 3.3.
  **Keep returning `false` until real delivery exists**, because callers may start trusting
  the return value to decide whether to fall back.

  Notification delivery was explicitly *not* part of this item. See ADR-0002's correction
  note for why that ordering is the whole reason the overlay was chosen.

- **macOS launch smoke in CI — DONE, HOSTED 2026-08-01.** `S` — fourth ADR-0002
  blocker. PR #40 ran `macos-gui-smoke` successfully on GitHub's macOS runner, alongside 14
  other passing jobs.
  `scripts/gui_smoke_macos.sh`, wired as the `macos-gui-smoke` job. `desktop-app-build`
  proved the macOS binary *links*; nothing proved it *starts*, and both ways it can fail to
  start are invisible at link time (a webview that cannot create its window, and a missing
  frontend bundle, which renders an empty window rather than an error).

  The script reuses `main.cpp`'s existing `SNAPBACK_GUI_SESSION_SMOKE` hook, so it is a real
  round trip — a session started and stopped through `AppState` and SQLite from the UI
  thread — and then requires the run loop to exit on its own, which is the same path the
  tray's Quit item drives. It first passed on Kassa's Mac and is now confirmed in hosted CI.

- **3.2 — Linux tray + overlay.** `M`
  `libappindicator` tray + an overlay window (X11/Wayland caveats noted). Since 3.1 landed,
  Linux is the **only** remaining user of `src/app/tray_stub.cpp` and
  `src/snapback/overlay_stub.cpp`; both now guard on `!_WIN32 && !__APPLE__`. Read
  `src/app/tray_macos.mm` and `src/snapback/overlay_macos.mm` first — they are the worked
  example of replacing these two stubs, including the shared `tray_menu_entries()` model a
  third platform should reuse rather than re-list.

- **3.3 — macOS packaging.** `L` — `.app` bundle + notarization + DMG.

- **3.4 — Linux packaging.** `M` — AppImage and/or Flatpak.

- **3.5 — In-app "check for updates".** `M`
  Fetch a version manifest and offer a download link — no silent install. The lightweight
  variant of the auto-updater deferred in [PACKAGING.md](PACKAGING.md).

---

## Tier 5 — Open findings from the 2026-07-20 engine/storage audit

**Checking each one before fixing it changed the answer twice.** 5.4 and 5.6 turned out to be
deliberate behaviour rather than defects — and 5.6 would have failed the feature-parity
golden test had it been "fixed" unilaterally. Both are now decision items. **An audit
finding is a hypothesis; verify it before writing code.**

Done: 5.1, 5.2, 5.7, 5.8, 5.9 (details in the [Done archive](#done-archive)).

- **5.3 — DONE 2026-08-03. Deleted.** [ADR-0004](adr/0004-verdict-and-opinion.md) chose
  delete over wire-in, and the deciding fact was not the inverted units — it was that
  **the job `should_nag` existed to do is already done elsewhere.** `ContextTracker` requires
  30s of continuous off-task before a snapback fires (`tracker.hpp`), which is the debounce
  confidence gating promised; and there is no risk-driven nag anywhere in the app for the
  gate to sit in front of. Wiring it in would have meant inventing a feature to justify
  dead code.

  `src/engine/confidence.hpp` and `tests/test_confidence.cpp` are gone. The Done archive's
  **2.4 "confidence calibration (gating)" claim is retracted** — see its entry.

- **5.4 — DONE 2026-08-03.** [ADR-0004](adr/0004-verdict-and-opinion.md) made
  `thrash_spikes` a **pure opinion-channel count: `distraction_risk >= 0.7`**, dropping the
  `AND focus_state = 'DISTRACTED'` conjunct. Both call sites changed together (`recap()` and
  its batched copy in `recent_session_summaries`), so the field-by-field parity tests hold
  automatically.

  **The earlier defence of the 0.7 was right and was also the argument for the change.** The
  bar is absolute so Deep mode's higher sensitivity cannot inflate session metrics that feed
  auto-labels — but AND'ing it with `focus_state`, which is mode-derived, smuggled the mode
  straight back in: Recovery rows between 0.7 and 0.85 were never `DISTRACTED` and so were
  never counted. The conjunct defeated the property the constant existed to protect. The
  recap test now seeds exactly that row.

  Consequence recorded honestly: spike counts rise slightly for Recovery sessions, so
  `infer_session_label` shifts a few of them toward Distracted/Pseudo. That is a correction
  in the training corpus, not drift. The Review tile now reads **"Distraction spikes"**; the
  wire name stays `thrash_spikes`.

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

  1. It is long-standing, deliberate behaviour, not an accident.
  2. **The feature-parity golden test pins every key of the feature vector**
     (`tests/test_feature_parity.cpp`). Changing this without updating the golden fails it.

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
     `Top 10 Productivity Fails` produces the overlay **"Return to Top 10 Productivity
     Fails"** — the product's namesake feature telling you to go back to the distraction.

  **Needs a decision first:** this behaviour is long-standing, so changing it is a deliberate
  break with how the app has always worked, not a bug fix. Cheapest fix is to consult `title_is_distracting`, which `app_context.cpp:125`
  already computes and `make_snapshot` ignores.
  *Note: the parser takes no `app_name`, so per-app title conventions are
  currently unimplementable. The decision should settle whether to add it.*

- **4.2 — Fuzz the untrusted boundaries.** `M`
  libFuzzer targets for `title_parser`, the JSON IPC arg parsing, **and the Windows shell
  quoting in `training_deploy.cpp`** — `cmd.exe` metacharacter handling (`^`, `%VAR%`, `&`,
  embedded quotes) is a genuinely different escaping problem from POSIX `sh`, and the same
  `quote()` serves both (`training_deploy.cpp:337`). `%` in particular is not neutralized by
  double-quoting in `cmd.exe`. Low severity (self-injection from a user-entered path), but
  it's the natural third target.

  **Consider instead:** building the process directly (`CreateProcessW` / `posix_spawn`) with
  an argv array removes the entire quoting problem class rather than escaping it correctly.
  *The parser's manual index math is exactly what fuzzing should hammer.*

- **4.3 — Opt-in crash reporting.** `M`
  Windows minidump capture on unhandled exceptions, written locally, opt-in only. Note 8.1
  reduces how often this fires; do 8.1 first.

- **4.4 — Perf regression gate.** `M`
  Profile `engine_tick` allocations (the compute path already defers work — measure it), then
  add a threshold to the benchmark harness so a regression fails CI. **7.12 is its natural
  first benchmark.** *See [benchmarking.md](benchmarking.md).*

  > **Partly advanced 2026-08-01.** The hot-path harness now measures both `health()` and a
  > complete live-read set under adversarial engine contention, with same-host baselines in
  > `benchmarking.md`. The item remains open because CI still checks only that benchmarks run;
  > it does not compare results to a threshold.

- **4.5 — Optional encryption at rest.** `M`
  Optional SQLCipher for the local DB. *(The schema-versioning half of this item was split
  out and promoted to 7.3.)* **Whether this is required at all is decided by 8.5** — don't
  build it before the threat model exists.

- **4.12 — There is no formatter and no static analysis, for either language.** `M`
  Opened 2026-08-04. The repository contains no `.clang-format`, no `.clang-tidy`, no
  `.editorconfig`, and no linter gate for the frontend. Style is currently maintained by
  attention alone across ~40 C++ translation units and a React app.

  **Do the formatter first and expect one enormous diff.** That is the whole cost, and it is
  paid once: every subsequent diff stops carrying whitespace noise, which is what makes small
  behavioural changes reviewable at a glance. Pick the settings to match what the code already
  looks like (100-column, 4-space, attached braces) so the reformat is close to a no-op.

  `clang-tidy` is the more valuable half and the more disruptive one, so scope it
  deliberately: start with `bugprone-*` and `performance-*` on `src/` only, warnings not
  errors, and promote to a gate once the backlog is empty. Turning on everything at once
  produces thousands of findings and teaches everyone to ignore the tool.

  **Not release-blocking**, and worth weighing against 6.6 — a compiler that actually catches
  real portability defects has already proven more valuable here than a style checker would
  have.

- **4.13 — DONE 2026-08-08.** `S` `scripts/check_pin_freshness.py` compares the ONNX
  manifest and the three git FetchContent pins (nlohmann_json, doctest, webview) to each
  project's latest GitHub release. `.github/workflows/pin-freshness.yml` runs weekly (and
  on demand), opens or comments on a `[pin-freshness]` issue when any pin is behind, and
  never edits a digest. `docs-smoke` runs the parser offline so a CMake rename cannot
  empty the watcher silently. CVE/advisory monitoring remains out of scope until 8.5.

  The original finding was:

- **4.13 (original finding) — Nothing watches the ONNX Runtime pin.** `S`
  Opened 2026-08-04, from 8.9's work. Dependabot covers `github-actions` and the frontend
  `npm` tree. It does not cover CMake `FetchContent` (4.6 already records that) and it does
  not cover the ONNX Runtime archives pinned in
  [`third_party/onnxruntime-pins.json`](../third_party/onnxruntime-pins.json).

  8.9 made that pin **trustworthy** — a re-uploaded release asset now fails the digest check
  before extraction. It did not make it **current**. Nothing reports a new upstream release or
  an advisory against 1.20.1, and pinning actively increases staleness by removing the tag
  that used to drift forward.

  A scheduled workflow that queries the ONNX Runtime releases API and opens an issue when the
  pinned version is behind would close the gap; it must not open a PR that edits the digests,
  because a bot computing the hash it is meant to be verifying defeats the point. The human
  step is downloading both archives and hashing them, which
  [dependencies.md](dependencies.md) documents.

  Same argument applies to the three `FetchContent` pins. Consider covering all four at once.

---

## Tier 9 — Ship a v1 (release readiness)

**The gap this tier closes: there is no written definition of "shipped."** Tiers 0–8 are all
"make the thing correct." This is "make the thing releasable to a stranger." Every item was
scoped by walking the lifecycle a real user goes through — install, first run, daily use,
upgrade, failure, uninstall — and asking what's missing at each step. Most of these are
small; the tier is large because nobody has walked that path yet.

- **9.1 — DONE 2026-07-25.** [ADR-0002](adr/0002-v1-supports-windows-and-macos.md) is
  `Accepted`. v1 = Windows + macOS; six release blockers (0.3, 3.1, 3.3, a macOS launch
  smoke, Decision session A, 7.3); Linux desktop, macOS autostart, and macOS toasts are
  fast-follow. The open sub-decision was resolved the same day: **macOS v1 ships a native
  `NSPanel` overlay.**

  **How it resolved is the part worth remembering.** The sub-decision had been framed as
  "expensive overlay vs. cheap notification — and toasts already work," and on that framing
  the notification was winning. Checking the claim before deciding on it showed toasts do
  **not** work on macOS (`Tray::instance().show_notification()` resolves to `NoopTray`'s
  `return false`), and that posting them needs a bundle ID, which puts the "cheap" option
  *behind* the longest-lead-time blocker on the list. The framing had the costs backwards.
  Same lesson as this file's preamble, applied to a decision rather than a task: **check the
  premise before you spend the decision on it.**

  The original finding was:

  There is no release checklist, no scope freeze, no "we ship when X." Without it, every item
  in this file looks equally required and the project never converges. Decide: which OSes at
  v1 (Windows-only is a legitimate answer and makes Tier 3 post-v1)? Which of Tiers 7/8 are
  blockers vs. fast-follows? What is explicitly *not* in v1? Output is a short checklist that
  the rest of this file gets measured against.

- **9.2 — DONE 2026-07-22.** CMake owns the project version, which is compiled into the
  backend and surfaced through the diagnostics payload and card. Runtime diagnostics now
  identify the exact build without adding a second IPC command outside the agreed contract.

  The original finding was:

  **One version number, surfaced everywhere.** `S`
  The version `0.2.0` is written in **two independent places** — `CMakeLists.txt:200`
  (`CPACK_PACKAGE_VERSION`) and `frontend/package.json:4` — with nothing keeping them in
  sync. There is **no `get_version` IPC command**, so the UI cannot display a version and
  `DiagnosticsSnapshot` cannot report one. A bug report today cannot say which build it came
  from, which makes 4.3 (crash reporting) far less useful when it lands. Single-source it in
  CMake, thread it through to the frontend and diagnostics.

- **9.3 — DONE 2026-08-04.** `S` [`CHANGELOG.md`](../CHANGELOG.md) exists, in Keep a Changelog
  form, built from this file's Done archive and the `feat:` history while both were still
  legible. README links it. It carries a **Known gaps** section as well as the usual ones,
  because "what this does not do yet" is the part a reader of a v1 release actually needs.

  **Writing it surfaced something worse than a missing file.** The `v0.2.0` tag points at
  `ba4050f`, which is **not reachable from `master` or any other branch** — same root commit,
  so history was rewritten underneath the tag and left it orphaned. Meanwhile `master` is
  **361 commits ahead** and `CMakeLists.txt` still declares `0.2.0`.

  Two consequences, both recorded in the changelog rather than silently fixed:

  1. There is **no published baseline**. Whatever that tag produced does not correspond to any
     commit that still exists, so nothing can be diffed against it.
  2. 9.11's new `verify-tag` job **would reject that tag**, correctly, on the reachability
     check. That is the gate doing its job on the first real case it met.

  Cutting a release therefore needs a version bump first, then a tag on a green `master`
  commit. Moving or deleting the existing tag is Kassa's call — it rewrites published
  history — so nothing here touches it.

- **9.4 — Walk the upgrade path once, deliberately.** `M`
  Nobody has ever installed version A and then upgraded to version B. Unknowns worth
  resolving before a stranger hits them: does the DB survive through 7.3's migration runner;
  does the HKCU Run key survive a reinstall to a new path, or does autostart silently point
  at a deleted binary; do settings persist; does a running instance get replaced cleanly?

  **The current installer has not earned a successful baseline:**
  `install_windows_package.ps1` passes a wildcard path to `Copy-Item -LiteralPath`, so `*`
  is treated literally instead of expanding the package contents. CI validates the ZIP but
  never installs it. Fix that as part of this item, then install v0.2.0, create recognizable
  data, upgrade to the candidate, and prove the executable, autostart path, settings, and
  `focoflow.db` are discovered and migrated rather than appearing lost under the C++ app's
  current data-directory rules.

- **9.5 — DECIDED AND IMPLEMENTED 2026-08-09; wiring the installer stays open.** `S`
  Decide and implement what uninstall removes. Today it plausibly leaves behind: the
  `focoflow.db` with full window-title history, the HKCU Run key (a startup entry pointing at
  a deleted binary), the log files and rotated backups, and the exported training CSVs. For a
  keystroke-recording app, **leaving the database behind after uninstall is the worst of the
  four** — the user believes they removed it. Ties to 7.6 and 8.5.

  **The decision: uninstall removes all of it, database included.** Not for tidiness — because
  a person who uninstalls an application that recorded their window titles believes they have
  removed what it recorded, and leaving `focoflow.db` makes that belief false without telling
  them. Everything else follows from the same rule: settings, logs and their rotations, every
  export, the model, SQLite's `-wal`/`-shm` companions (which hold recent writes, and so recent
  window titles), every pre-migration backup, and the start-on-login entry.

  **Done:** `src/app/uninstall.hpp` enumerates it and `purge_app_data` removes it, reporting
  per-item what went and what did not through 8.12's `ActivityDeletionResult` rather than a
  parallel shape — the question afterwards is identical. Reachable as `snapback --purge`, which
  exits non-zero on a partial purge so a caller cannot report a clean removal it did not
  achieve. Deliberately **not** `delete_all_activity_data`: that one keeps the database file,
  the settings and the model because the app keeps running: uninstall has no afterwards.
  Two boundaries are pinned by test: files that merely look like ours (`snapback.log.bak`) are
  left alone, and the data directory itself is removed only if empty, so someone who pointed
  `SNAPBACK_DATA_DIR` at a folder of their own keeps what is theirs. An empty data directory
  reports a failure rather than a silent success, because "nowhere to look" is not "nothing to
  remove".

  **Still open:** the Windows uninstaller does not call it yet — `package_windows.ps1` builds
  an IExpress installer, and wiring an uninstall hook to run `snapback --purge` before deleting
  the binary is packaging work that belongs with **3.3**/**3.4**. macOS and Linux have no
  uninstaller at all yet. The command exists and is the single implementation each will use.

- **9.6 — Failure UX: what does the user actually see when it breaks?** `M`
  The backend reports several rich failure states (7.4, 7.10, 8.1), but there is no designed
  response to any of them. Specify what the UI does when: capture permission is revoked
  *mid-session* (macOS lets the user do this at any time); the hook dies; the disk is full so
  writes fail; the DB is locked by another instance; predictions have gone stale. Right now
  most of these render as a dashboard that simply stops updating, which is
  indistinguishable from "you're doing great."

  **Concrete persistence gap:** exceptions escaping the engine persistence phase are logged,
  but no durable failure state reaches `HealthStatus` and no retry policy stops the engine
  from repeating the same failed write. The frontend already has a `persistence-failed`
  event shape. Wire the native state and event, degrade health truthfully, avoid a hot retry
  loop, and test disk-full/locked failures.

- **9.7 — DONE 2026-07-26.** Insights, trends, summary reports, and recent focus now all
  render explicit first-run guidance instead of zero-valued metrics or blank charts. Summary
  export stays disabled until a completed session or prediction exists, and Review-surface
  regression verifies all four empty states together.

- **9.8 — DONE 2026-07-26.** Snapback now acquires a process-lifetime OS lock in its data
  directory before opening SQLite or starting capture. A second launch exits cleanly, real
  lock failures remain errors, and crashes cannot leave a stale logical lock because
  ownership belongs to the native handle. Cross-platform unit tests cover contention,
  moves, release, and unusable paths.

- **9.9 — DONE 2026-07-26.** The diagnostics card now exports a one-click JSON support
  bundle containing health, recent logs, version, and OS/build identity. Both the UI and
  file state exactly what is included, what is excluded, and that logs may expose local
  paths or error details; native and UI regressions pin the privacy boundary.

- **9.11 — DONE 2026-08-04.** `S` `release.yml` gained a `verify-tag` job that
  `windows-package` and `github-release` both depend on. It enforces all three rules: the tag
  must be `vX.Y.Z` **and equal to** CMake's `PROJECT_VERSION`
  ([`scripts/check_release_tag.py`](../scripts/check_release_tag.py)); the tagged commit must
  be an ancestor of `origin/master`; and that commit must already have a completed, successful
  `ci.yml` run. [PACKAGING.md](PACKAGING.md) documents the resulting order.

  **It reads CI's conclusion instead of re-running the matrix.** Copying the 15 jobs into a
  reusable workflow would have satisfied the item and then drifted from the real one — and a
  release gate that tests something *other* than what CI tests is worse than no gate, because
  it looks like proof. Querying the run for that exact SHA cannot drift.

  **The gate fails closed.** A `gh api` error or an unexpected response shape is treated as
  unproven rather than as a pass; five stubbed responses (green, none, two, API error, junk)
  were run against the real step text to confirm each outcome. The tag-version guard was
  likewise checked against six tag shapes plus a `CMakeLists.txt` whose `project()` call no
  longer declares a version — that last one must fail loudly, not pass forever.

  **Not verified live:** the GitHub API query shape was exercised only against stubs, because
  the local `gh` is unauthenticated. It uses the documented `workflows/{file}/runs` endpoint,
  but the first real tag push is what proves it. Push a throwaway tag on a branch before
  relying on this for a real release.

  The original finding was:

  `release.yml` publishes any pushed `v*` tag. The full 15-job CI workflow runs only for
  pushes/PRs targeting `main` or `master`, while release runs a narrower Windows package
  path and never proves that the tag names CMake's `PROJECT_VERSION`. A tag can therefore
  publish an arbitrary commit that never passed the macOS/Linux/sanitizer/ONNX matrix or a
  version whose artifact metadata disagrees with its tag.

- **9.12 — Choose a project license and package dependency notices.** `S` `decision`

  The repository has no `LICENSE`, `NOTICE`, or third-party notice file, while CPack points
  `CPACK_RESOURCE_FILE_LICENSE` at README. That is neither a project license nor an inventory
  of dependency obligations, and a public repository is not automatically open source.

  Kassa must choose the project license. Then add the real license file, audit the licenses
  of bundled/runtime dependencies, generate the required third-party notices, include both
  in every package, and test their presence in extracted release artifacts.

- **9.13 — The `v0.2.0` tag is orphaned, so there is no release baseline.** `S` `decision`
  Found 2026-08-04 while writing [`CHANGELOG.md`](../CHANGELOG.md). The tag points at
  `ba4050f`, which shares this repository's root commit but is **reachable from no branch** —
  history was rewritten underneath it. Meanwhile `master` is **361 commits ahead** and
  `CMakeLists.txt` still declares `0.2.0`.

  Three things follow. Nothing can be diffed against "the last release", because the commit
  that produced it no longer exists on any line of history. The version has not moved in 361
  commits, so tagging today would reuse a number. And 9.11's `verify-tag` job **would reject
  that tag** on the reachability check — correctly; that is the gate meeting its first real
  case.

  **`decision` because the options differ in what they cost, and only Kassa can pick.** Delete
  the tag and start at a fresh version; retag it onto a current `master` commit (this rewrites
  what a published tag means, so anyone who fetched it sees it move); or leave it and treat
  the first real release as the baseline, documenting the gap. The changelog currently states
  the third, as the honest default rather than as a decision made.

  Whatever is chosen, cutting a release needs the version bumped first, then a tag on a
  `master` commit CI has proven green.

- **9.14 — There are four ways to get data out and none to get it back in.** `M`
  Opened 2026-08-05. `export_my_data`, `export_summary_report`, `export_support_bundle`, and
  `export_training_data` all exist. There is **no import, restore, or migrate-to-a-new-machine
  path of any kind.**

  For a cloud-synced app that is a non-feature. For this one it is the gap in the central
  promise: 7.6 and the onboarding wizard say the data is local and yours, and 1.6 says it
  never leaves the machine — which also means **nothing else is holding a copy**. A new
  laptop, a reinstall, or a restored disk image loses the history outright, and history is the
  entire product (trends, streaks, the Tier 13 training corpus).

  Two things make the naive workaround worse than it looks. Copying `focoflow.db` by hand
  works only until the schema versions differ, and 7.3's downgrade guard then **refuses the
  file** — correctly, but with no path forward. And 9.8's process-lifetime lock means a copy
  taken while the app is running can be a torn read of a WAL database.

  Scope it as **replace, not merge**: back up the current database, verify the incoming file
  opens and migrates, then swap it in. Merging two histories is a different and much larger
  problem — UUID session ids will not collide, but retention windows, duplicate detection, and
  conflicting settings all need answers — so state explicitly that it is out of scope rather
  than leaving it implied.

  Use `VACUUM INTO` for the export side: it produces a consistent single-file snapshot of a
  live WAL database, which hand-copying does not. Ties to **7.22** (the same backup
  machinery), **9.4**, and **9.5**.

- **9.15 — Define one coherent desktop instance and window lifecycle.** `M/L`
  Opened 2026-08-05. **9.8** correctly prevents two processes from opening the same database,
  but a second GUI launch only prints “already running” to an invisible stderr stream and
  exits. The owner process also has no close-window handler: when `w.run()` returns the engine
  stops, despite a tray whose only actions are Show and Quit. Double-click can look broken,
  while clicking X can accidentally stop an active always-on app.

  Add a per-user, per-data-directory activation channel owned by the instance lock. A second
  process asks the owner to restore/focus its window, waits for an acknowledgement, and exits
  without opening SQLite, starting capture, or installing another tray. Define close behavior
  explicitly: follow each platform's native convention, offer **Hide/keep running** versus
  **Quit** where needed, and show a one-time explanation the first time a close leaves Snapback
  in the tray.

  Launching while hidden/minimized must surface the existing window within one second and
  restore its previous size, position, and surface. Never hide the only window if tray
  installation failed. Explicit Quit closes the attended span and engine exactly once through
  **7.23/7.25**; stale endpoints after crashes recover without allowing a second database owner.
  Two-process Windows/macOS tests must cover activation, owner-exit races, wrong-user requests,
  repeated close/show, and exactly one engine, capture hook, DB owner, and tray icon. Integrate
  the visible recording state from **2.10**.

- **9.16 — DONE 2026-08-06.** `M` The ownership export is complete, streamed, and states what
  it holds. It keeps the "my data" name because it now deserves it — the **Recent preview**
  fallback the item offered was not needed.

  **The document lied in two independent ways.** It said it contained "every session" while
  the command stopped at 200 sessions and 500 windows within each, with no command able to
  retrieve the rest. And the `truncated` flag was set from the *session* cap alone, so a file
  that dropped the 501st window of an **included** session was reported to the UI as complete.

  **Raising the caps would not have been a fix**, and the item says so: an unbounded
  `list_context_snapshots` materializes the whole history under `storage_mutex_`, which is the
  stall-becomes-dropped-events path 7.12 exists to remove. Rows are now paged — each page read
  under the lock, written, and released before the next — so peak memory is one page rather
  than one history. `page_size` replaces the old caps and is deliberately a *memory* knob, not
  an *answer* knob: the previous arguments changed what the file contained, which is how an
  export that omitted history passed its own tests.

  **Keyset cursors, not OFFSET.** OFFSET re-scans from the start on every page and silently
  skips or repeats rows when the table changes underneath it — the export would corrupt itself
  simply because the engine kept recording during it. Sessions page on
  `(started_at DESC, session_id DESC)`; context rows on `(timestamp ASC, id ASC)`, with `id` in
  the key because 7.16's whole-second timestamps are not unique and a page boundary inside a
  tied group would drop or repeat rows.

  **`truncated` is now derived, not stored** — from per-record-type omission counts — so the
  original defect is unrepresentable rather than merely fixed. A footer manifest states
  sessions, windows, and interruptions, plus an FNV-1a checksum of the body, so a cut-short
  file is distinguishable from a valid empty one. The checksum is documented as an integrity
  check and explicitly **not** a signature.

  The regression seeds **205 sessions and 505 windows** — past both old caps — exports with
  `page_size=7`, and asserts every id appears **exactly once**: a mishandled page boundary
  repeats rows rather than losing them, and a `contains` check would pass either way.

  Deferred as the item directs: progress, cancellation, and atomic temp→final publication to
  **14.6**; the native destination picker to **10.14**; a lossless database snapshot to
  **9.14**. Peak-memory measurement is structural here rather than instrumented — one page is
  bounded by construction — and belongs with **4.4**'s gate. Three new C++ cases and a frontend
  module; suite **394 pass**. The original finding follows.

- **9.16 (original finding) — Make “Export my data” complete, streaming, and honest about
  omissions.** `M`
  Opened 2026-08-05. The readable export says it contains “every session,” but the command
  hard-caps the result at 200 sessions and 500 context rows per session with no cursor or
  continuation. Worse, its IPC `truncated` flag reflects only omitted sessions; a file that
  drops the 501st window from one included session can be reported to the UI as untruncated.
  No available command can retrieve the omitted history.

  Make the default ownership export complete. Stream or cursor through a consistent read
  snapshot in deterministic order with bounded memory; do not “fix” the cap by materializing
  an unlimited archive under `storage_mutex_`. If a fast capped product remains useful, rename
  it **Recent preview** and provide explicit continuation — it cannot keep the “my data” name.
  Include exact exported/omitted counts per record type plus a small manifest/checksum so a
  partial or interrupted file is distinguishable from a valid empty one.

  A fixture above both current limits must prove every session and context row appears exactly
  once, with peak memory below 64 MiB and a stable result while new rows are written. Progress,
  cancellation, and atomic temp→final publication belong to **14.6**; native destination choice
  belongs to **10.14**. This is a human-readable portable archive. **9.14** separately owns a
  database snapshot suitable for lossless restore.

---

## Tier 10 — Frontend & UX

The frontend was inventoried in July and reviewed end-to-end on 2026-08-05: composition,
loading behavior, chart semantics, session controls, Settings hierarchy, privacy copy, and
the CSS token layer. Tests still mock IPC, so **10.1** remains the real-browser boundary.

- **10.1 — Nothing tests the real binary against the real UI.** `L`
  There is **no E2E framework** — no Playwright, no Cypress, nothing in
  `frontend/package.json`. Frontend tests mock `invoke()`; C++ tests run headless.

  Be precise about what *is* covered, because this entry used to overstate the gap.
  `test_ipc_contract` pins command names three ways (the `bind_cmd` list, the frontend's
  `invoke` calls, and `fixtures/ipc_commands.json`), and `test_command_bridge` covers the
  dispatcher itself — arg unwrapping, the error envelope, the escaped-JSON event boundary,
  the validation helpers, and two real handlers round-tripping with camelCase keys.

  **What nothing exercises is the real `webview.bind()` round trip in a running process.**
  Every test above calls the handler layer directly, so a break *between* `bind()` and the
  browser — the injected shim, promise resolution, a webview API change — passes CI. A
  command whose payload shape drifted in a handler *without* a bridge test is the same
  story, and both violate the IPC synchronization contract.

  > *Corrected 2026-07-29:* this entry said `windows-desktop-integration` "is currently
  > skipped (6.3)". It is not — 6.3 removed its `needs:` on 2026-07-22 and it now runs
  > unconditionally. It also said the seam is tested "only by `test_ipc_contract`'s name
  > matching", which ignored `test_command_bridge` entirely.

- **10.2 — DONE 2026-07-25.** Decided in
  [ADR-0003](adr/0003-three-surface-dashboard.md) (`Accepted`) and shipped on
  `fix-macos-app-launch`: `SurfaceNav.tsx` switches between the three surfaces, `App.tsx`
  composes them, and `FocusStateHero.tsx` leads Now with the focus *state*. The
  ~20-cards-on-one-page problem is gone. **The components carry no tests — tracked as
  10.7.**

  The decision it implements was:

  Three surfaces — **Now**
  (session control, live state, pomodoro), **Review** (insights, analytics, summary, focus
  summary, session recap, activity), **Settings** (rules, settings, privacy, permissions, goal
  categories, training, diagnostics). Now leads with the focus *state*, not the score, which
  keeps this work independent of Decision session A. Palette stays; hierarchy, spacing, and
  density change. Implementation is a surface-at-a-time composition change, not a rewrite —
  no longer a `decision`.

  The original finding was:

  `App.tsx` renders 34 card/wizard references. Every feature shipped as "another card,"
  which was right while porting and is now an information-architecture problem: no
  navigation, no hierarchy, no progressive disclosure. A new user's first screen shows
  session control next to ONNX training deployment. Needs a design decision (tabs? routes?
  a settings/advanced split?) before more cards land — and 7.6, 9.6, and 9.7 all want to add
  surfaces.

- **10.7 — DONE 2026-07-26.** `FocusStateHero`, `SurfaceNav`, `VerdictFeedback`, and
  `SignalsCard` now have direct component tests. The suite covers verdict evidence and
  feedback, tab selection/ARIA wiring/keyboard focus, concrete correction labels, and signal
  list rendering including the empty state.

- **10.3 — Accessibility has never been assessed.** `M`
  No audit has been done. Specifically worth checking: keyboard navigation through the card
  grid; focus management when the snapback overlay appears (it steals attention by design —
  does it trap focus?); screen-reader labelling of the score/state tiles; whether the
  distraction states are distinguishable without color; and whether the always-on-top
  overlay respects reduced-motion and OS contrast settings. A focus tool that fights
  assistive tech is a bad look.

- **10.4 — DONE 2026-07-26.** A render probe confirmed that prediction-owned parent state
  re-invoked stable cards on every event. Memo boundaries now isolate the stable app shell
  and non-live cards on all three surfaces. The hero, activity history, and signals remain
  live; the regression test advances parent telemetry and proves unchanged Now, Review, and
  Settings cards do not render again.

- **10.5 — DONE 2026-07-26.** The measured component-suite baseline is 76.86% statements,
  66.72% branches, 74.20% functions, and 77.66% lines. Vitest now enforces rounded-down
  global floors of 76/66/74/77, and the frontend CI job runs pure TypeScript unit tests plus
  the coverage-gated component suite through `npm run test:ci`. The generated HTML report
  remains gitignored.

- **10.6 — No C++ coverage measurement at all.** `M`
  The frontend can measure coverage; the C++ side cannot. Given how many bugs in Tiers 5/7
  were "the tests never exercised the production branch" (`seconds_since_session_start`, 7.1,
  5.3), a coverage report is the cheapest tool for finding the next one. `gcov`/`llvm-cov` on
  the Linux CI job.

- **10.8 — DONE 2026-08-06.** `S` The hourly chart is on a fixed 0–100 axis with references at
  0, 50, and 100, and the app list no longer calls context snapshots "switches".

  **Max-relative scaling was the headline defect and it inverted the chart's message.** Every
  bar was drawn as a fraction of the largest value in the current dataset, so a user whose best
  hour scored 20/100 saw that hour at full height — the picture said "this is your peak" while
  the number beside it said "this is poor", from the same measurement. Bars are now
  `score / 100 × plotHeight`, clamped, so an out-of-range value cannot overflow the plot and
  misreport every other bar by comparison.

  **A missing hour and a measured zero are now different pictures.** They used to render
  identically, which made "we have no idea" and "you were completely distracted" the same
  drawing. A measured zero keeps a visible 2px sliver; an hour with no samples gets a muted
  tick *below* the axis, outside the plot area, so it cannot be read as a value. An hour
  reported with `sampleCount == 0` counts as no data — the count is what says whether anything
  was observed, not the score.

  **Each bar's title now carries what its height cannot**: the score out of 100, how many
  samples it rests on, and what fraction of them were distracted.

  **The app metric was relabelled, not redefined.** `context_app_counts` counts context
  snapshot rows, which the engine writes periodically as well as on a real window change — so
  "switches" credited anyone sitting still in one window with *more* app-hopping the longer
  they stayed put. It now reads "N samples" with a caption saying what that means. A real
  switch/dwell metric stays a separate question; the item was explicit that the SQL count must
  not be renamed by wish.

  Geometry and wording are pure functions in `frontend/src/analyticsChart.ts` with a `tsx`
  test, following `sessionStatus.ts` — the component suite cannot run on this machine
  (**11.11**), so chart arithmetic reachable only through a rendered component would have had
  no local test at all. The cases cover a low-only dataset, a genuine zero, missing hours,
  out-of-range clamping, and the sampled-context label. This is data correctness and does not
  substitute for **10.3**'s accessibility audit. The original finding follows.

- **10.8 (original finding) — Make Review charts and labels tell the truth.** `S`
  Opened 2026-08-05. `AnalyticsCard` divides each hourly focus score by the largest value in
  the current dataset. If the user's best hour scores 20/100, that bar is drawn at full
  height. A missing hour is rendered exactly like a measured zero. The same card labels
  `context_app_counts()` rows as app "switches", although the query counts periodic context
  snapshots as well as actual changes.

  Use a fixed 0–100 axis with visible references; render no-data gaps distinctly from zero;
  and include the bucket's sample count/distraction rate in accessible detail. Either relabel
  the app metric as sampled context or define and query a real switch/dwell metric — do not
  rename the SQL count by wish. Geometry/ARIA tests must cover a low-only dataset, a genuine
  zero, missing hours, and periodic snapshots. This is data correctness, not a substitute for
  the broader accessibility audit in **10.3**.

- **10.9 — DONE 2026-08-11.** `S/M`
  Opened 2026-08-05. Settings currently opens with model training, followed by goal
  categories, diagnostics, raw signals, rules, ordinary settings, privacy, and permissions
  in one card stream. The global header permanently exposes classifier backend, model file,
  and quality state. The product reads like an engineering console before it reads like a
  focus tool.

  Preserve ADR-0003's three top-level surfaces, then group Settings into **General**,
  **Focus**, **Privacy & permissions**, and **Advanced/developer**. Training, raw signals, and
  logs are collapsed by default; one compact health badge opens technical details, while a
  real actionable failure may reveal the relevant section automatically. At the default
  1100×760 window, common settings must be reachable without scrolling through developer
  controls. Add navigation/focus tests and keep deep links for support instructions.

  **Done:** `settingsSections.ts` owns the four groups, the deep-link parser, the failure
  routing, and the badge; `SettingsNav.tsx` is a second-level tablist with the same roving
  tabindex and arrow/Home/End contract as `SurfaceNav`. ADR-0003's three surfaces are
  untouched — this is a level *below* Settings, not a fourth tab. Only the selected group
  mounts, which is what actually satisfies "reachable without scrolling through developer
  controls": the ordinary settings are no longer *below* the console, they are elsewhere.
  Training, logs, and raw signals sit in `<details>` inside Advanced, closed by default.

  **The header lost three permanent engineering fields and gained one badge.** Classifier
  backend, model file, and training quality were on the first screen of a focus tool at all
  times; they are now one line that says whether anything needs attention plus a
  **Technical details** link into the section that holds the detail. That is ordering the
  information rather than hiding it — everything is still one click away.

  **Two things this pass got wrong first, both caught by a test rather than by reading.**
  The badge's two capture labels were written against `summarizePermissions`, whose `blocked`
  collapses a refused OS permission and a dead capture listener into one value — so "Capture
  stopped" was unreachable and every capture failure read as a permission problem. They are
  different problems with different fixes (a settings dialog versus a restart), so the badge
  now reads the two causes separately. And the auto-reveal effect had no latch, so it dragged
  the user back to Privacy on every re-render for as long as capture stayed down, making the
  other three sections unusable exactly when someone might need them. It now fires once per
  run.

  **The reorganisation broke 28 existing assertions, which was the point.** Every Settings
  test had been passing because all eight cards were mounted at once — the same shape of
  failure ADR-0003 fixed at the top level. `renderApp` now takes a section, and each test
  names the group its card lives in.

- **10.10 — Build a complete visual-token and appearance system.** `M`
  Opened 2026-08-05. `styles.css` declares `color-scheme: light`, duplicates semantic colors
  as literals, and references undefined custom properties including `--border`, `--card`, and
  `--text`. The browser silently drops those declarations, so some borders/text depend on
  fallback context rather than an intentional design system.

  Define semantic tokens for canvas, surface, border, text, controls, charts, and each focus/
  error state. Add persisted **System / Light / Dark** appearance with System as the default;
  every state retains text or icon meaning beyond color. Add a guard for undefined custom
  property references, light/dark visual snapshots for the three surfaces and overlay, and
  automated contrast checks. Coordinate with **10.3**, including disabling card-rise and
  other nonessential animation under `prefers-reduced-motion` rather than replaying it on
  every surface switch.

- **10.11 — Give the whole Review surface one shared time range.** `M/L`
  Opened 2026-08-05. Trends describes all retained predictions, Summary chooses 24 hours or
  seven days, Recent Focus is framed as a sample count, and Insights uses its own recent-row
  limit. Placing those cards together implies comparison even though they describe different
  populations.

  Add a Review-level **Today / 7d / 30d / All / custom** range owned by the Review workflow.
  Every card must query and display that exact interval; no card may apply a hidden row cap.
  Loading, empty, error, and stale-result behavior belong to the range as one unit. Add
  workflow tests proving one change invalidates every Review dataset once and an older slow
  response cannot overwrite the newer range. Land after **7.16** defines calendar boundaries
  and **7.12** makes those queries bounded; implement through **14.4**, not another set of
  cross-card callbacks in `App.tsx`.

- **10.12 — Make the Windows snapback overlay monitor- and DPI-aware.** `S/M`
  Opened 2026-08-05. The shared placement helper and its test already support a display whose
  origin is not `(0,0)`, but Windows production always asks `SPI_GETWORKAREA` for the primary
  work area. It then lays out a fixed 420×250-pixel window. On a multi-monitor or mixed-DPI
  desk, a nudge caused on one screen can appear on another, clip, or render physically tiny.

  Target the monitor containing the foreground window, with the cursor monitor as an explicit
  fallback. Query that monitor's work area, scale dimensions/padding/type by its effective DPI,
  and handle `WM_DPICHANGED` while the overlay is visible. Preserve the non-activating focus
  behavior: better placement must not steal the user's keyboard target.

  Tests and a Windows desktop smoke must cover negative coordinates, monitors above/left of the
  primary display, taskbars on every edge, unplugging the target monitor, and 100/125/150/200%
  scale without clipped copy or controls. Coordinate colors/typography with **10.10**, focus and
  zoom behavior with **10.3**, and the future action layout with **2.8**.

- **10.13 — DONE 2026-08-06.** `S/M` The two prediction-row tiles became a real
  continuous-focus **duration**, and the session metric says "sessions" in its own label.

  **The row counts were not a labelling problem, they were a measurement problem.** Predictions
  arrive when input produces a reading, not once per second, so two people doing identical work
  got different "streaks" purely from typing cadence — a case the tests now pin directly: the
  same 60 seconds sampled twice and six times reports 60 both ways, where the row counts differ
  threefold.

  **A stretch breaks on a distraction, on a gap past `kFocusRunGapSecs` (120 s), and at a
  session boundary.** That bound *is* the interpolation policy, stated rather than implied: an
  interval counts at face value up to it and counts as nothing beyond it. Nothing is inferred
  about a gap, because predictions stop entirely while the user is idle or private — a gap is a
  pause in the *user*, not in the data. A backwards clock and an unparseable timestamp both end
  a run rather than being folded in at an invented distance. Attended spans are not joined:
  they would be redundant, since the AFK freeze means no prediction is ever written outside
  one, and the gap rule handles legacy sessions with no spans identically.

  **The parity test earned its keep twice.** Comparing the SQL against the production C++
  implementation over the 12,000-row fixture caught (1) runs walking across concurrent sessions
  with identical timestamps, which is why both sides now break on session change, and (2) the
  SQL crediting the gap *out of* a distracted sample to the focused run that followed — which
  reported every stretch as exactly **twice** its real length. A plausible number, wrong by a
  factor of two, and invisible to any test that only checked it was non-zero.

  **Renamed through the DTO, not over it**, as the item requires: `longest_focus_streak` →
  `longest_focus_secs` in `FocusSummary`, `SummaryReport`, `Storage::PredictionStats`, the JSON
  wire, the frontend types and mappers. `AnalyticsSummary::productive_session_streak` keeps its
  name — it genuinely counts sessions — and its label is now "Sessions in a row" with a caption
  saying "this counts sessions, not time".

  One ordering hazard worth recording: `recent_predictions` returns newest-first, which was
  harmless while the summary counted rows. Measuring between neighbours made the order
  load-bearing, and reversed it reports zero. `focus_summary` now reverses explicitly and the
  header says the input must be chronological.

  Nine new C++ cases and a frontend module; suite **400 pass**. The original finding follows.

- **10.13 (original finding) — Give every “streak” a truthful unit, or remove it.** `S/M`
  Opened 2026-08-05. Three incompatible quantities use nearly the same label. Recent Focus's
  “Focus streak” and Summary's “Best streak” are consecutive non-Distracted **prediction row
  counts**; Analytics's “Focus streak” is consecutive completed **sessions** whose average is
  at least 70. Predictions arrive when input produces a reading, not once per second, so none
  of those row counts is elapsed focus time and two users with identical work can get different
  values from different input cadence.

  Rename the completed-session metric with its unit and definition, for example “Productive
  sessions in a row.” Either remove the two prediction-row tiles or replace them with a true
  continuous-focus duration. Duration must use timestamps intersected with **7.23** attended
  spans, break at idle/private periods and missing-cadence gaps, and state any bounded
  interpolation policy. Never display a row count with time-like “streak” copy.

  Fixtures must hold wall time constant while varying event cadence, insert an idle gap, cross
  a session boundary, and include legacy sessions without attended spans. Accessible labels,
  exports, and API fields need the same unit — this cannot be a frontend rename over a falsely
  named DTO. **10.8** owns chart scaling and other misleading labels; it did not cover these
  streak definitions.

- **10.14 — Use native Open/Save workflows for user-owned documents.** `M`
  Opened 2026-08-05. Support, summary, and personal exports silently choose folders inside the
  app-data directory and then print a path. There is no file-dialog seam. That is tolerable for
  internal training artifacts, but poor desktop behavior for a document the user intends to
  keep, send, or restore on another machine; **9.14** will otherwise have no safe way to select
  an incoming snapshot either.

  Add owned native **Save As** dialogs for personal, summary, and support exports, plus native
  **Open** for the restore package in **9.14**. Cancellation is an ordinary result, not an error.
  Use platform overwrite confirmation, type filters and correct extension handling, then write
  to a sibling temporary file and publish atomically. After success offer Reveal and Copy path.
  The default private app-owned folder remains available for unattended/internal workflows.

  Keep dialog and filesystem authority in native code; do not expose an unrestricted path API
  to the webview, especially across **8.14**'s trust boundary. Adapter tests must cover cancel,
  overwrite refusal, Unicode and long paths, read-only destinations, extension normalization,
  and a window disappearing while the dialog is open; one Windows and macOS desktop smoke must
  exercise the real owner window. Coordinate long exports with **9.16/14.6**.

---

## Tier 11 — Test infrastructure

- **11.1 — DONE 2026-07-31.** `S` `doctest_discover_tests` now registers each case as its own
  CTest entry: **296 entries instead of 1**, so a crash costs one result instead of the run.
  CI's per-test `--timeout` also starts meaning what it says — it used to cover all 295 cases
  at once, making one hung test indistinguishable from a slow suite. Cost measured on this
  Mac: 2.4s as a single binary, 7.2s as 296 processes. That is the price of not being blind
  after a crash, and it buys per-case isolation of global state for free.

  **It immediately found a production bug, which is the part worth keeping.** Splitting the
  suite turned a dismissed flake into a reproducible single failure: *AppState health reports
  a capture hook that stopped unexpectedly* failed about **1 run in 40**. The flaky assertion
  was the honest one. `CaptureThread::record_failure()` set `failed_` and left `running_`
  true until the thread body ended a mutex acquisition and a string assignment later, and
  `AppState::health()` loads the two flags separately — so the diagnostics panel could report
  **"capture failed" and "running: true" in the same report**. Same contradiction shape as
  7.7. Fixed by clearing `running_` before setting `failed_`; 0 failures in 200 runs after.

  Note what had been hiding it. `CaptureThread reports a hook that returns as failed` waits
  for `running()` to go false *before* asserting, so it sidesteps the window rather than
  pinning it. The new `CaptureThread never reports failed and running at the same time` spins
  instead of sleeping so it samples inside the window, and reports 3 contradictions in 200
  attempts with the stores put back the wrong way round — a deterministic catch where the
  old one was a 2.5% coin flip.

  **The general lesson: one CTest entry per binary does not just lose results after a crash,
  it launders per-case flakiness into "the suite is flaky."** One bad case in a 295-case
  process is noise you re-run; one bad case out of 296 entries is a bug report with a name.

  The original finding was:

  6.1 made this concrete: a single SIGSEGV aborted the run and **138 test cases were
  reported as *skipped*.** The whole suite is one CTest entry (`snapback_tests`), so any
  crash blinds us to everything after it. Register test cases as separate CTest entries, or
  shard the binary, so a crash costs one result instead of the run.

- **11.2 — DONE 2026-07-31.** `M` `tests/test_classifier_properties.cpp` asserts eight
  properties over 5,000 generated feature vectors × 3 focus modes — **186,380 assertions**
  covering score ranges, label validity, determinism, thrash monotonicity, and the ONNX
  normalisation seam.

  **It earned its keep on the first run: see 7.18.** The property "guardrails only ever move
  the state toward distraction" failed on 246 assertions, and the cause is a real asymmetry —
  the drift branch exempts `DEEP_FOCUS` but not `DISTRACTED`, so a weakly-distracted row gets
  *upgraded*. That is the thing this item promised ("would have caught 5.2 and 5.3
  mechanically") and it delivered on a defect nobody had filed.

  Two choices in how it is written, both of which are the point rather than incidental:

  - **The seed is fixed** (`0x5EEDC0FFEE1234`). A property test that draws fresh randomness
    each run turns a real defect into an intermittent one and teaches everyone to re-run it —
    exactly the failure mode 11.1 spent this week untangling out of the capture layer. Same
    corpus every run, every host; widening it is a deliberate edit.
  - **The generator is adversarial on purpose.** One draw in eight is negative, enormous, or
    exactly zero. Feature extraction is supposed to keep these sane, but the classifier is a
    separate module and its guarantees must not rest on its caller behaving — a snapshot
    replayed from an older build can hand it anything.

  **The failing property was pinned, not deleted.** Weakening a test until it passes is how a
  finding gets lost. The guaranteed half (the DISTRACTED-forcing conditions) is asserted as a
  property; the divergence became a characterization test set to fail the moment 7.18 was
  settled.

  > **It worked exactly as designed, 2026-08-03.** ADR-0004 made policy demote-only, the
  > characterization test went red on the change, and it is now the assertion *guardrails
  > never raise the state* — the original property, restored. **This is the payoff for
  > pinning rather than weakening:** the finding survived three days as executable code
  > instead of a comment, and the fix could not land without confronting it.

  The original finding was:

  `features.cpp` and `classifier.cpp` are pure functions over a feature vector — the ideal
  target for property testing, and currently covered only by example-based cases. Properties
  worth asserting: `focus_score` always in `[0,100]`; `distraction_risk` always in `[0,1]`;
  `focus_state` always one of the four labels (this one has already been violated — 5.2);
  probabilities sum to 1; and monotonicity where the model claims it (more thrash never
  *decreases* distraction risk). Would have caught 5.2 and 5.3 mechanically.

- **11.3 — ALREADY DONE; entry was stale, corrected 2026-07-31.** `S` No code was written for
  this. `fixtures/feature_parity/golden.json` and
  `TEST_CASE("feature vectors match the checked-in golden fixture")` already exist and do
  exactly what the item asks: replay each scenario, then diff all 31 features **by name at
  their index** against the checked-in file, plus a count assertion. Comparing name-at-index
  is what makes it catch a *reordering* and not merely a value change. It is JSON rather than
  the CSV the item imagined, which changes nothing.

  Verified live rather than assumed, because a golden test that never actually compares is the
  obvious way for this to be quietly worthless: perturbing one value in `golden.json` fails the
  test, and the clean fixture passes 172 assertions.

  **This is the fourth time an item here has described a gap the code had already closed** —
  see 0.3, 7.2, and the note on trusting this file. The instruction at the top is load-bearing:
  *when an item claims something is missing, check whether it is actually missing before
  rebuilding it.* Checking cost two minutes; rebuilding it would have cost an afternoon and
  produced a second, competing golden fixture.

  The original finding was:

  The feature-vector order is declared a contract with the model and the CSV exporter
  and CSV exporter, enforced only by the dual-language parity job. A checked-in golden CSV for a
  fixed input, diffed on every run, makes a reordering fail loudly and locally rather than in
  a cross-language job people may not read.

- **11.4 — PARTLY DONE 2026-07-31.** `M` `src/util/clock.hpp` defines a two-method `Clock`
  (monotonic `steady_ms()` + wall `wall_time()`) with a `SystemClock` for production, and
  `AppState` takes an optional `Clock*` alongside its existing optional `Logger*` — same
  injection idiom, so every existing call site compiles unchanged. `now_rfc3339()` and
  `steady_now_ms()` stopped being static and now read through the seam, which is the point:
  reading the time is something an *instance* does, not something any code can do from
  anywhere. `tests/manual_clock.hpp` holds the fake, deliberately under `tests/` — shipping a
  test clock in `src/` would be a poor answer to 7.14.

  **Two clocks, not one, and that is load-bearing.** Wall time can jump backwards across a DST
  change or an NTP correction; a duration derived from it goes negative and an idle timer
  concludes the user has been away for -1 hour. Only `steady_ms()` is used for elapsed time,
  and the seam makes that case *testable* rather than merely asserted in a comment.

  **What is NOT done: Storage still reads its own clock.** The first draft of the test
  asserted a session's `started_at` carried the injected time and failed with today's real
  date — **sessions are stamped by `Storage`, not by `AppState`.** Storage has six
  `utc_now_rfc3339()` call sites plus two SQL `CURRENT_TIMESTAMP` uses, and injecting there
  means changing how `Storage` is constructed (`open`, `open_memory`, and its move
  semantics), which is a separate change rather than a bigger version of this one. The item
  stays open for that half; the claim made here is scoped to `AppState` and no wider.

  **This unblocks 7.14**, which is the reason to have done it: all four `_for_test` methods
  are `AppState` methods that exist only to pass `now_ms` in by hand.

  The original finding was:

  Time is read directly in at least three places (`state.cpp:69`, `:82`,
  `storage.cpp`'s `CURRENT_TIMESTAMP`). This forces sleep-based tests, blocks testing
  idle/pomodoro/throttle interactions at real durations, and is the direct cause of the
  `_for_test` methods in 7.14. One injected clock seam fixes all of it. **Pairs naturally
  with 7.16** — settle what time *is* here, then settle where it comes from.

- **11.5 — DONE 2026-07-31.** `M` Tracked as 7.11 and closed there when the sixth (large)
  fixture landed; listed here so the testing story is complete in one place. Marked done
  2026-08-04 — as a pure cross-reference it kept reading as open work after its target closed.

- **11.8 — DONE 2026-08-04.** `S` Both failures are fixed and the full suite is **328/328 on
  GCC/MinGW for the first time**. Details below; the original finding follows after them.

  **Failure 2 was a production bug, not a test bug, and it was not confined to
  `training_deploy.cpp`.** `copy_options::overwrite_existing` is ignored by libstdc++ on
  MinGW, so every "replace this file" call site was broken there.
  [`src/util/fs_replace.hpp`](../src/util/fs_replace.hpp) now provides `copy_over()`
  (remove-then-copy, portable), and `swap_file`, `swap_optional_file`, and
  `save_app_settings` all use it.

  **The settings backup had the same bug, written the same day, and its test could not see
  it.** `save_app_settings` passed `overwrite_existing` through the `std::error_code`
  overload, so on MinGW it did not throw — it silently did nothing and left a **stale**
  `settings.json.bak` while reporting success. The 7.19 test saved twice and passed, because
  two saves never reach the overwrite: the first has no `settings.json` to back up and the
  second has no `.bak` to replace. Only a *third* save exercises it. That case is now a test.
  The lesson is about the shape of the gap, not the flag: a test that never reaches the state
  where the operation has to *replace* something proves only that it can *create* something.

  **Failure 1's flake is fixed and measured.** The precondition wait is now bounded by a
  5-second deadline instead of 2,000,000 iterations. Over 30 Debug runs it is **30/30**,
  against the 26/30-failing baseline recorded below. It is still a spin, not a sleep — the
  measurement has to land between `record_failure()`'s two stores — and the clock is read once
  per 1024 iterations so reading it cannot widen the window being sampled.

  **But the invariant half of that test does not detect its own bug on this machine, and
  that is pre-existing.** Reintroducing the 11.1 ordering bug (storing `failed_` before
  clearing `running_`) leaves the case passing — `contradictions` stays 0. That is *not* a
  regression from the deadline change: the **original** iteration-count version was
  re-checked against the same inverted stores and also passed, 10/10. The contradiction
  window is simply not observable on this hardware/toolchain, so the test's own comment —
  "With the stores in the wrong order this fails" — is **unverified here** and rests on the
  MSVC/CI behaviour it was written against. Anyone adding a MinGW job should not read this
  case as protecting the 11.1 invariant on that toolchain.

  The original finding was:

- **11.8 (original finding) — Two tests fail on a toolchain CI does not cover (GCC/MinGW).** `S`
  Found 2026-08-04 while verifying ADR-0004's C++ changes. This machine had no compiler, so a
  portable GCC 14.2 (MinGW-w64 UCRT) was used; CI only ever builds MSVC on Windows and
  libstdc++ on Linux, so this combination had never been exercised. **Both failures are
  pre-existing** — reproduced at `697e77b`, and `src/capture/` and `tests/test_capture_thread.cpp`
  are byte-identical to it. Recorded, not fixed: neither is a defect on a platform we ship.

  1. **`CaptureThread never reports failed and running at the same time` fails almost always
     here — Debug 26/30 runs, Release 30/30.** The failing assertion is `REQUIRE(failed)`
     (`tests/test_capture_thread.cpp:238`), **not** the `contradictions == 0` invariant the
     test exists to protect, so 11.1's ordering fix is intact and this is a defect in the
     test's own precondition wait. It spins a bounded 2,000,000 relaxed loads waiting for the
     hook thread to record failure, and on this machine that window can close before Windows
     schedules the thread.

     **The amplification is the part worth understanding.** The case runs **200 attempts** and
     one missed attempt fails the whole case, so a per-attempt miss rate of only ~1–2% —
     back-solved from the rates above — becomes an ~87–100% case failure. That is why a
     timing margin nobody would call marginal reads as a hard failure, and why the same code
     can be green in CI: the per-attempt miss just has to be a bit rarer there.

     *Two hypotheses were tested and rejected.* It is **not** Debug-vs-Release (both fail),
     and it is **not** background load — the rates above were re-measured with Docker Desktop
     stopped and were identical. An early Debug run that passed was luck, consistent with the
     observed ~4-in-30 pass rate; do not read a single green run as evidence here.

     The fix is to wait on a condition with a real deadline rather than an iteration count.
     Note the comment's "spin, don't sleep" rationale applies to *sampling the contradiction
     window*, not to establishing the precondition, so the two can be separated without
     weakening what the test checks.
  2. **`rollback_model swaps the deployed model and quality metadata` throws
     `filesystem error: cannot copy file: File exists`.** `swap_file`
     (`src/app/training_deploy.cpp:463`) passes `copy_options::overwrite_existing` on every
     copy, which libstdc++ does not honour on MinGW.

  Neither blocks release. **They become blocking the moment anyone adds a MinGW job**, which
  is the only reason this is written down.

- **11.11 — DONE 2026-08-07.** `S` The suite runs here now: **87 of 87**, and `npm run test:ci`
  — the exact script CI runs — passes locally for the first time.

  **The diagnosis in the original finding was wrong, and usefully so.** It blamed jsdom
  ("jsdom is not providing a DOM at all") and proposed pinning Node to state the mismatch. One
  probe test showed jsdom was fine — `window` exists, the document URL is a real origin — and
  printed the actual cause:

  > `ExperimentalWarning: localStorage is not available because --localstorage-file was not
  > provided.`

  **Node 26 ships its own experimental global `localStorage`**, gated behind a flag nobody
  passes. It is on `globalThis` before the jsdom environment installs its window properties,
  so jsdom's implementation never wins. Node 22 — which CI pins — has no such global, which is
  exactly why the same tree was green there and 47 of 87 failed here. This is the file's own
  rule about checking a claim before building on it, applied to a diagnosis rather than to a
  feature.

  `frontend/tests/memoryStorage.ts` installs a spec-shaped in-memory `Storage` **only** when the
  environment did not end up with a working one, and says so in the log when it does. On CI's
  Node 22 nothing runs and jsdom's real implementation is untouched. It has its own `tsx` test,
  because the component suite cannot be what verifies the thing the component suite depends on.
  `.nvmrc` and `engines` state the intended toolchain, and all four jobs now read
  `node-version-file` rather than repeating `22` in four places.

  **Turning it on immediately found seven regressions from this session's own work** — none of
  which CI had seen either, since nothing has been pushed. Three were 7.23's running/paused
  rename still asserted as "active"; three were 9.16's export wording; one was 10.13's duration
  tile. All were the intended new behaviour with a stale assertion, and one of them was
  genuinely sharpened in the fixing: the session-status query now scopes to the Session Control
  card, because the health pill also reads "running" and an unscoped match would have passed on
  the wrong element.

  **The coverage gate needed a decision, not a nudge.** The new pure-logic modules are tested
  by the `tsx` runner, which does not feed V8's aggregate, so they counted as near-uncovered
  and dragged branches to 65.66% against a 66% floor. Lowering the floor would hide
  regressions; excluding them restores what the number claims to measure (77.43/66.90/74.09/78.20).
  So they are excluded **with a guard**: `scripts/check_coverage_exclusions.py` fails unless
  every excluded module has a matching dedicated test file that `npm run test:unit` actually runs.
  Verified by making it fail. Without that, the exclusion list would be one edit away from
  silencing the gate, and the failure would look like an improving coverage number.

  The original finding follows.

- **11.11 (original finding) — The frontend component suite cannot run on the dev machine's
  Node.** `S`
  Found 2026-08-05 while wiring 7.23 through the UI. `npm run test` fails **44 of 87** cases
  with `TypeError: Cannot read properties of undefined (reading 'clear')` on
  `window.localStorage` — jsdom is not providing a DOM at all. `vite.config.ts` correctly sets
  `environment: "jsdom"`, and the lockfile and installed trees agree (vitest 4.1.10, jsdom
  29.1.1), so nothing is out of sync.

  **The difference is Node.** This machine runs **v26.5.1**; CI pins **22**
  (`actions/setup-node` in both workflows). Confirmed pre-existing and unrelated to 7.23 by
  stashing every frontend change and re-running: identical 9/19 files, 44/87 cases.

  The consequence is not "some tests are red" — it is that **the component suite cannot be run
  before pushing**, so 10.5's coverage floor and every component regression are enforced only
  in CI. `npm run typecheck` still works, which is what 7.23's frontend change was verified
  with.

  Pin the toolchain rather than chasing the symptom: add `engines` and an `.nvmrc` naming the
  Node CI uses, so a mismatch is stated instead of discovered as a wall of DOM errors. Then
  decide separately whether to move both to a newer Node.

- **11.9 — DONE 2026-08-08.** `S` The case now samples `failed()` and `running()` from a
  second thread for the whole attempt — the same two separate loads `health()` performs —
  rather than reading `running()` on the thread that just observed the flip. On MinGW UCRT
  GCC 14, reintroducing the inverted stores (failed first, running cleared after the mutex
  write) produced **1157 contradictions across 200 attempts**; restoring the correct order
  is a clean pass. Same-thread post-flip sampling stayed inert on this toolchain, which is
  why the old test could not claim to guard the invariant here.

  The original finding was:

- **11.9 (original finding) — The capture contradiction invariant is unverified outside MSVC.** `S`
  Opened 2026-08-04 by 11.8's fix. `CaptureThread never reports failed and running at the same
  time` exists to catch 11.1's store-ordering bug, and its comment claims "with the stores in
  the wrong order this fails."

  On GCC/MinGW that claim is false. Reintroducing the bug — storing `failed_` before clearing
  `running_` — leaves the case **passing**. This is not a consequence of 11.8's deadline
  change: the original iteration-count version was re-run against the same inverted stores and
  also passed, 10/10. The contradiction window simply is not observable on that toolchain.

  So the test's protective value rests entirely on MSVC/CI behaviour that has never been
  measured either — only assumed, because the bug was originally *found* there. **A test whose
  sensitivity is unknown on the platform it runs on is not yet evidence of anything.**

  Do the experiment: invert the stores on a branch, run that case in Windows CI, and record
  the failure rate. If it does not reliably fail there either, the test needs a real
  observation strategy (sample `running()` from a second thread the moment `failed()` flips,
  rather than from the same loop) or it should stop claiming to guard the invariant.

  Ties to **6.6** — a GCC job makes this the difference between "known-inert here" and a
  silently useless test running on every push.

- **11.10 — DONE 2026-08-08.** `S` `ScratchKey` now sweeps `HKCU\Software\SnapbackTests`
  for `test-<pid>-<n>` leaves whose pid is not a running process, before allocating its own
  leaf. A concurrent case named for a live pid is left alone; a leaf that does not match the
  naming pattern is left alone. Two cases plant a dead-pid orphan and a foreign name and
  check both outcomes.

  The original finding was:

- **11.10 (original finding) — A crashed test process leaves a registry key behind.** `S`
  Opened 2026-08-04 alongside 11.7. `tests/test_autostart_run_key.cpp` creates
  `HKCU\Software\SnapbackTests\test-<pid>-<n>` and deletes it in the fixture destructor. A
  crash, an abort, or a `REQUIRE` that terminates the process skips that destructor, so the
  key survives — and 11.1 runs each case as its own process, so a crash is a per-case event.

  Much smaller than what 11.7 fixed: the residue is inert, under a name production never
  touches, and it cannot register anything to launch at login. But it is the same *shape* of
  defect, and 11.7's own history is two rounds of exactly that shape.

  Sweep stale `test-*` subkeys at fixture construction — the pid in the name makes "left by a
  process that no longer exists" decidable — or delete the `SnapbackTests` root once at
  process start. Prefer the sweep: deleting the root races concurrent cases.

- **11.7 — DONE 2026-08-04.** `S` The Windows half is closed, so nothing in the suite touches
  a real login mechanism on any platform any more.

  The registry mechanism moved to [`src/app/autostart_run_key.cpp`](../src/app/autostart_run_key.cpp)
  with the **key path as an argument** — the same seam the launchd and systemd backends
  already had, which this item named as the fix to copy. `autostart.cpp`'s Windows branch now
  only chooses *which* key, exactly like the macOS and Linux branches choose a directory.
  `tests/test_autostart_run_key.cpp` round-trips against
  `HKCU\Software\SnapbackTests\test-<pid>-<n>` and deletes it, and the real-Run-key case in
  `test_autostart.cpp` is gone, replaced by a read.

  **Suppressing the assertion had been treating the symptom.** The old case stopped asserting
  the write because hardened environments refuse it (~33% of early Windows CI runs). But the
  hazard was never the assertion — it was touching the shared key at all, since a crash
  between the write and the restore leaves the *test binary* registered to launch at login.
  Against a scratch key the write is asserted again, because now a refusal really would be
  our bug.

  **A first attempt still left residue, which is the same class of mistake one level down.**
  `RegCreateKeyEx` creates intermediate keys, so deleting only the leaf left an empty
  `HKCU\Software\Snapback` behind — verified by inspecting the real registry after a run,
  not by reading the code. The fixture now removes the parent too, and the scratch root is
  `SnapbackTests` rather than `Snapback` so a test never creates or deletes a key production
  might one day own. `RegDeleteKey` refuses a key that still has subkeys, so concurrent cases
  cannot destroy each other's.

  Verified by checking `HKCU` before and after a run from a clean state: the Run key is
  untouched and neither scratch key survives. Suite: **332 cases, 332 pass** on GCC/MinGW.

  The original finding was:

- **11.7 (original finding) — The autostart test asserts against the real machine's registry.** `S`
  `tests/test_autostart.cpp:26` does a live round-trip through `HKCU\...\Run` and `REQUIRE`s
  that the write succeeds, so a passing suite depends on ambient machine state rather than on
  our code.

  **Windows is still the open half.** The macOS and Linux backends added by 3.0 on 2026-07-30
  take their target directory as an argument, so `test_autostart_launchd.cpp` and
  `test_autostart_systemd.cpp` run entirely inside a temp directory — that shape is the fix
  this item is asking for, and it can be copied onto the registry backend by injecting the key
  path. **This stopped being theoretical on 2026-07-30:** the first suite run after the launchd
  backend landed left a real `~/Library/LaunchAgents/com.snapback.app.plist` on the dev Mac,
  pointing at `build/snapback_tests`, because the old "no backend off Windows" case called
  `set_autostart_enabled(true)` expecting a no-op. It was caught by the test failing for a
  different reason and removed by hand.

  > **And it happened again the next day, on Linux — fixed 2026-07-31.** The 2026-07-30 fix
  > patched the macOS arm and left the structure intact, so when 3.0's second half gave Linux
  > a systemd backend, Linux kept falling into the `#else` "no backend yet" arm. CI run
  > `30607879815` failed on all four Linux jobs, and the failing assertions say what happened:
  > `CHECK_FALSE(set_autostart_enabled(true))` failed *and* so did the `CHECK_FALSE(
  > autostart_enabled())` after it — **the test installed a real systemd user unit onto the
  > GitHub runner and left it enabled.**
  >
  > **The lesson is about the guard, not the assertion.** A `#else` arm is a claim about
  > "every platform that has no backend *yet*", and it silently shrinks each time one lands —
  > while the single line inside it that mutates the machine keeps running. Patching the arm
  > that broke leaves the next platform to rediscover it, which is precisely what happened
  > twice in two days.
  >
  > The no-op contract is now **driven by the runtime answer**: it early-returns when
  > `autostart_supported()` is true, so `set_autostart_enabled(true)` can only execute where
  > it is defined to do nothing. Adding a fourth backend cannot reintroduce this, with or
  > without anyone remembering this file. Verified locally by forcing the stub backend to
  > compile on macOS: the case runs 3 assertions with the stub and 0 with a real backend.
  >
  > This does not close 11.7 — Windows still round-trips the real registry — but it removes
  > the mechanism by which *every* platform inherited the hazard.

  **Measured flake rate: 2 of the first 6 observed Windows job runs (~33%), alternating
  between the two jobs on identical code.**

  | Run | `ONNX backend / windows` | `C++ headless tests / windows-latest` |
  |-----|--------------------------|----------------------------------------|
  | `30141403795` (PR #27) | ❌ `test_autostart.cpp:26` | ✅ |
  | `30141852252` (master `18dcba0`) | ✅ | ✅ |
  | `30143262631` (PR #28) | ✅ | ❌ `test_autostart.cpp:26` |

  Neither `src/app/autostart.cpp` nor the test changed across those runs, and the failure
  moves between jobs — so it is environment coupling, not a regression. Most likely cause:
  writing `HKCU\...\Run` is a textbook persistence technique, so a hardened runner can refuse
  it.

  **Stopgap applied 2026-07-25:** the write is no longer asserted — if the environment
  refuses it, the test emits a `MESSAGE` and returns. Everything after the write is still
  asserted, so a real round-trip regression still fails wherever the write is permitted. This
  keeps master green; it does **not** close this item.

  Two shapes for the fix:

  | Option | Change | Cost |
  |--------|--------|------|
  | Tolerate | Treat "cannot open the Run key for write" as *environment cannot test this* and skip; keep asserting the round-trip when it can open | ~5 lines, keeps the real-registry coverage where it works |
  | Inject | Parameterize the key path so the test uses a throwaway key and never touches the real `Run` value | Makes autostart testable by seam instead of side effect; slightly wider change |

  **Inject is the better shape** — it removes the shared mutable resource instead of
  tolerating it. *Note: this is Windows-only code, so either fix is CI-verified only from a
  macOS host (see [running.md](running.md)) — write it carefully, because the feedback loop
  is a full CI run.*

---

## Tier 12 — Documentation truth

**Every item here is a doc asserting something false.** This tier exists because that has now
happened often enough to be a category, not an accident. Two root causes recur: docs written
*before* the code (plans that were never reconciled), and docs written *about* code that
later moved.

> **Cleared 2026-07-23.** 12.1–12.5 are all done; only **12.6** remains, and it is a port
> gap the audit *found*, not a doc defect. Three things are worth carrying forward:
>
> 1. **The tier is now partly self-enforcing.** `scripts/check_doc_paths.py` runs in
>    `docs-smoke` and fails the build if any doc names a file that does not exist. That
>    closes the most common failure mode mechanically. It cannot check *claims* — only
>    paths — so the audit habit still matters.
> 2. **Doc audits find code bugs.** 12.2 turned up **8.7**, a silently dead `-UseVite`
>    flow, and 12.1 turned up **12.6**, an entire missing capability. A doc records what
>    the code was *supposed* to do; diffing that against what it does is cheap and finds
>    things tests do not.
> 3. **Every stale claim pointed the same way** — describing the system before a fix
>    landed, never after. Docs rot toward *pessimism* here, which is the dangerous
>    direction: it makes finished work look open and invites rebuilding it.

- **12.1 — DONE 2026-07-23.** Moved to the [Done archive](#done-archive). The map now
  matches the tree, and `scripts/check_doc_paths.py` runs in CI so it cannot drift again.
  The audit found one thing nobody was looking for: **global label hotkeys were never
  built** — see the new item **12.6**.

- **12.2 — DONE 2026-07-23.** Moved to the [Done archive](#done-archive). Ten false claims
  corrected across four files; `PACKAGING.md` was the only one that survived clean. The
  audit surfaced **8.7** (a live bug) and confirmed the value of `check_doc_paths.py` from
  12.1.

  The original finding was:

  the former architecture summary, `testing_strategy.md`, `benchmarking.md`,
  `windows_demo.md`, and
  `PACKAGING.md` have not been verified since they were written. Given the hit rate on
  the former root guidance (six false claims), `ARCHITECTURE.md` (seven), and this file
  (three), assume
  they contain errors until checked. `docs-smoke` in CI only checks that docs exist.

- **12.3 — DONE 2026-07-23.** Moved to the [Done archive](#done-archive).
  [`docs/adr/`](adr/README.md) now holds the template, the index, and the list of the
  fourteen `decision`-tagged items still awaiting one.

- **12.4 — DONE 2026-07-23.** Moved to the [Done archive](#done-archive).
  [`docs/running.md`](running.md) is the per-OS page, and every macOS claim in it was run
  before being written.

- **12.6 — Global label hotkeys were never built, and nothing recorded that.** `M`
  Found 2026-07-23 while reconciling 12.1. The design called for global hotkeys that label
  the current window focused/distracted without leaving the app. **The frontend event and
  notification scaffolding now exists, but no native code registers an OS-global shortcut or
  emits the label-hotkey event.** It stayed invisible because
  `ARCHITECTURE.md`'s module map never listed the capability — the map only covered what
  someone intended to build, so a skipped module left no trace anywhere.

  Filed here because the doc audit is what surfaced it. It needs hand-written per-OS hotkey
  registration, which is presumably why it was skipped. ADR-0002's six-item blocker list does
  not include it, so this is post-v1 unless that accepted scope is explicitly revised.

- **12.7 — DONE 2026-08-08.** `S` ADR-0002 now states inline that development happens on
  Darwin, so the accepted record no longer depends on a gitignored local file. ADR-0001
  describes that same local-only guidance file without citing it as a path. `.gitignore`
  comments why those names stay untracked. `scripts/check_doc_paths.py` dropped the
  exemption that treated the missing file as fine: a markdown link or relative path to it
  now fails even when the file exists on the author's machine. Bare prose that names the
  file is still allowed (the attribution note in this document does exactly that). A
  self-test inside the guard pins the three cases.

  The original finding was:

- **12.7 (original finding) — ADR-0002 linked to a file that exists in no clone.** `S`
  Opened 2026-08-04. ADR-0002 used a relative markdown link to a gitignored local agent
  file as evidence for where development happens. That file is absent from every clone, so
  the link was broken for everyone but Kassa — and `scripts/check_doc_paths.py` carried an
  explicit exemption that kept the guard quiet about it.

  **The exemption was doing real damage, not just hiding a dead link.** Tier 12 exists
  because docs asserted things no reader could check; an ADR citing a file nobody can open
  is that exact failure, inside the document type meant to be the durable record. ADRs are
  append-only, so the fix was not to drop the Darwin-dev claim — it was to make the
  citation resolve by restating the fact inline, then drop the exemption so the guard stops
  normalising an unresolvable path.

---

## Tier 13 — Model lifecycle (breaking down 2.3)

**2.3 was one `L` item that hid at least seven.** The deployment identity, quality gate, and
rollback are complete as 13.1–13.4 in the Done archive. The three unresolved product decisions
below still determine whether, where, and how the retraining loop should operate.

- **13.5 — Is there enough labelled data to train on at all?** `S` `decision`
  Unexamined. Labels come from explicit user submissions plus auto-labels at session end.
  **7.5** unified explicit and shutdown stop, but **7.25** found the broader lifecycle still
  has holes: replacement can omit an auto-label and repeated Stop can add another. Before
  building the loop, first make label production idempotent, then measure: how many labels
  does a typical week produce, and what's the class balance? If the answer is "40 labels,
  90% PRODUCTIVE," personalization is premature and 2.3 should be rescoped to *collecting*
  data well rather than training on it.

- **13.6 — Define what happens when the model and the heuristic disagree.** `S` `decision`
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

- **13.7 — DONE 2026-08-07.** [ADR-0006](adr/0006-trainer-is-developer-tooling.md):
  **training tooling is developer-only.** Consumer Settings keeps Focus Feedback labels and
  does not mention `ml/`, repo paths, or train-from-export. Debug builds (or Release with
  `SNAPBACK_DEV_TRAINING`) still expose the existing Model tooling card. Native train/repo
  commands refuse when the gate is off. **2.3** remains repository tooling until a packaged
  trainer ships and a new ADR revisits option A.

  The original finding was:

- **13.7 (original finding) — Decide the trainer's real product boundary.** `S` `decision` → then `L`
  Opened 2026-08-05. The current consumer UI asks for a Snapback repository path and reports
  readiness only when that directory contains `ml/pipeline_cli.py`; its help text tells the
  user to install `ml/requirements-train.txt`. **This checkout contains no `ml/` directory at
  all**, and a packaged application would not normally include a source checkout. The first
  card in Settings therefore advertises a path that neither this tree nor an installed build
  can satisfy.

  Choose one honest product:

  1. **Packaged on-device training** — restore/replace the pipeline, package its runtime and
     licences, invoke it without a repo path, publish progress/cancellation through **14.6**,
     and test the installed artifact rather than a developer checkout.
  2. **Developer-only model tooling** — keep export/deploy internals behind an explicit dev
     build or Advanced flag, remove the impossible consumer instructions, and make **2.3** a
     repository tooling project rather than an end-user feature.

  Record the choice before more lifecycle work. Acceptance is a packaged smoke in option 1,
  or zero training controls/instructions in a normal release in option 2. The current third
  state — a prominent workflow whose required files do not exist — is not an acceptable
  fallback.

- **13.8 — PARTIAL 2026-08-10.** `S/M` Startup no longer exits on model-recovery failure
  (`recover_model_deployment_for_startup`); health reports `degraded` with preserved paths and
  Diagnostics offers **Retry cleanup** and **Reveal preserved files**, which opens the private
  data directory and still reports its path when the OS refuses. Remaining from this item:
  the adversarial real-webview harness cases named in the original acceptance (coordinate
  with **10.1**).

  The original finding was:

- **13.8 (original finding) — Optional model recovery may degrade, never brick the core app.** `S/M`
  Opened 2026-08-05. Startup runs `recover_model_deployment()` before storage or the webview
  and exits the entire process on any exception. Recovery deliberately throws for a malformed
  marker or staging/cleanup debris it cannot remove — including a committed deployment whose
  valid live model is already in place. ONNX load and inference already fall back safely to
  the heuristic, so cleanup metadata is paradoxically more availability-critical than the
  optional model itself.

  If **13.7** removes consumer deployment, remove this startup path from normal builds and close
  the item that way. Otherwise preserve or quarantine questionable artifacts, start the core
  capture/history app on the heuristic, and expose a durable degraded-model health state with
  **Retry cleanup**, Reveal files, and a rollback action when one is provably safe. Never delete
  the only candidate/previous model merely to make startup green.

  Drive corrupt-marker, unremovable-staging, committed-but-locked-cleanup, invalid-model, and
  clean-retry cases through the actual startup orchestrator. Each must prove storage, session
  capture, Review, and heuristic predictions remain available; diagnostics must name the
  preserved paths without leaking their contents. This complements **13.4** rollback and
  **9.6** runtime failure UX; neither currently covers a pre-window optional-subsystem failure.

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

- **11.6 — DONE 2026-07-31.** `S` `src/util/ranked_mutex.hpp` gives each lock its position in
  the order (`LockRank::State` → `ActivityBoundary` → `Storage`), and `AppState`'s three
  mutexes are now `RankedMutex`. An inverted or equal-rank acquisition reports itself on the
  first single-threaded run through the bad path — no race, no scheduler luck. Every call site
  is `std::lock_guard lock(...)` with a deduced argument, so none of them changed.

  Three things are worth carrying out of it.

  **The check is compiled into release builds, not just debug.** Tracking is a fixed
  eight-slot thread-local array, so acquisition costs no allocation and a handful of
  instructions. Only the *response* varies: abort under `!NDEBUG` so a developer gets the
  stack, log-and-continue in release, because crashing a user's app over a deadlock that has
  not happened is worse than the warning. The main CI test job builds `Release`, so a
  debug-only check would have run in exactly one job.

  **The first draft's own bookkeeping was the bug.** It stored one "innermost rank" that each
  mutex restored on unlock — correct only if locks are released LIFO. `std::unique_lock` lets
  you release an outer lock while an inner one is held, and when a test did, the thread-local
  was left permanently wrong and *every later lock on that thread* reported a violation that
  never happened. Two of the eleven tests failed for that reason and neither had a real
  inversion in it. **A diagnostic that goes wrong after the first mistake is worse than none**
  — it turns one bug into a wall of false reports. The fix is the held-rank array, which is
  correct under any release order and reports a non-LIFO release once, as its own finding.

  **The AppState regression test only covers the methods it calls.** An inversion was planted
  in `upsert_app_rule` to check the guard bites and the test stayed green, because the draft
  called eight methods and that was not one of them. The test now names every `AppState`
  method that touches `storage_mutex_` or `activity_boundary_mutex_`, and with the plant back
  in it fails. Same shape as 7.1 and 5.3: **the suite passed because it never took the
  branch.**

  The original finding was:

  `state.hpp:161` states the invariant: *always acquire `mutex_` before `storage_mutex_`,
  never the reverse.* Nothing enforces it — no wrapper type, no runtime assertion, no test.
  It holds today because a careful author held it, and the codebase now has three mixed-lock
  methods plus every IPC command. TSan catches an *actual* inversion only if a test happens
  to exercise both orders concurrently. A debug-only lock-order assertion is a few lines and
  converts a comment into a guarantee — exactly the class of invariant worth making
  mechanical rather than trusting to review.

- **12.5 — DONE 2026-07-23.** Moved to the [Done archive](#done-archive). `test_local.sh`
  and `run_benchmarks.sh` are real ports (verified by running them on this Mac), and
  `scripts/README.md` says which of the eleven scripts run where.

---

## Tier 14 — Architecture leverage (2026-08-01 and 2026-08-05 deep-module scans)

These are not “large file” complaints. Each item identifies a shallow seam where callers
must understand implementation details. Only work with a concrete acceptance boundary is
kept here; already-deep modules and completed performance work were rejected during the scan.

- **14.1 — Benchmark a separate SQLite query lane.** `M` `performance`

  The immutable live snapshot removed state-lock contention, but every storage-backed UI
  report still takes `storage_mutex_`, the same seam the engine uses to persist. WAL already
  permits concurrent readers and a writer. A deep `ActivityQueries` module owning a read
  connection could hide reporting SQL and let the engine's `Storage` connection remain the
  sole writer.

  **Do not implement from structure alone.** First extend the month-scale benchmark to run
  the largest session-history/analytics/summary reads concurrently with persistence and
  measure writer delay and dropped-event risk. If the result is immaterial, close this item
  with the numbers and keep one connection. If it reproduces contention, introduce the read
  lane, pin snapshot/after-delete semantics, and require the benchmark to show the gain.

- **14.2 — Make one synchronous engine cycle the production test seam.** `M`

  `engine_tick()` owns the real drain → idle/pomodoro → compute → persist → emit sequence,
  while tests and both benchmark targets include `tests/app_state_test_access.hpp` to reach
  three narrower private methods. Deleting that friend seam would force tests back to threads
  and sleeps; its forwarding interface is shallow because it exposes which internals to call.

  Extract a deterministic engine-cycle module whose `step` returns persistence jobs, emitted
  events, and updated live state. The production thread and tests must call the same step;
  persistence and UI dispatch remain adapters outside it. Delete the three private test
  methods and remove the benchmarks' dependency on the `tests/` include path. This is the
  concrete completion path for 7.14 and the remaining testability half of 11.4.

- **14.3 — Make the command registry the authoritative native contract.** `M`

  Command names, argument defaults, validation, result casing, TypeScript DTOs, mappers,
  fixtures, and mocks are parallel hand-maintained descriptions across `commands.hpp`,
  `api.ts`, and `apiMappers.ts`. The contract test uses source-text matching for names, while
  selected command tests manually recreate handler lambdas. That catches some drift but still
  lets a real registered handler's payload shape diverge.

  Introduce a webview-free `CommandRegistry` that owns the real descriptors and handlers;
  make webview binding a thin adapter over it. Every registered handler must be invokable by
  name in native tests, and the frontend contract fixture must be generated from or validated
  against the same manifest. This complements 10.1's real-webview E2E; neither replaces the
  other.

- **14.4 — Move frontend invalidation into workflow modules.** `M`

  `App.tsx` coordinates roughly a dozen feature states and passes 29 values into
  `useAppEffects`; deletion and session actions know which unrelated stores must refresh.
  Tests reproduce that knowledge with large command-switch mocks. Deep Now, Review, and
  Preferences workflow modules should own subscriptions, refresh consequences, and failure
  propagation, exposing smaller action/result interfaces to the surfaces.

  Preserve ADR-0003's Now/Review/Settings placement. Acceptance is workflow-level tests in
  which one public action proves every required invalidation without `App` manually calling
  each refresh function. Schedule this after release blockers; it is locality leverage, not a
  prerequisite to ship.

  **Performance acceptance added 2026-08-05.** The default surface is Now, but mount currently
  fetches health, latest prediction, rules, training status, insights, focus summary,
  analytics, and active session immediately; `useAnalytics` also performs its own duplicate
  mount fetch. An active session polls context history even while Review is hidden. The new
  workflows must make hydration surface-aware: initial Now renders with zero Review/Settings
  data calls, first surface entry fetches each dataset once, re-entry uses cached data until a
  real invalidation, and no timeline query runs while Review is hidden. Deduplicate in-flight
  requests and prevent an older response from overwriting newer state. Prefer a native
  "context snapshot persisted" invalidation to refreshing history on ordinary prediction
  events. Pin command counts in workflow tests.

- **14.5 — Replace the fixed 10 Hz engine poll with deadline-aware, bounded work.** `M`
  `performance`
  Opened 2026-08-05. The engine calls `engine_tick()` and sleeps 100 ms forever, including
  when there is no session and no event. Inside a cycle it drains until the capture queue is
  empty while holding the state lock; under sustained input, idle/Pomodoro work, persistence,
  emissions, stop, and session actions wait behind an unbounded drain.

  Build this with or after **14.2**'s deterministic cycle. Wake on capture arrival, stop/
  lifecycle requests, and the next idle/Pomodoro deadline. Give each cycle a fixed event or
  time budget, preserve event order across batches, and publish queue depth/high-water mark
  plus maximum drain time in diagnostics. A quiet minute should execute only deadline-required
  cycles rather than roughly 600; a continuous-producer test must prove stop and timer events
  cannot starve. Record same-host idle CPU/wakeups and event-to-prediction p95 before/after,
  with instrumentation overhead below 1%. ADR-0005 keeps sessions explicit, so also measure
  and eliminate unnecessary no-session classifier work without breaking **2.7**'s nudge path.

- **14.6 — Move long-running commands behind owned, cancellable jobs.** `L`
  Opened 2026-08-05. Webview bindings run on the UI thread. Training waits in `std::system()`
  for the Python process, and full training/personal exports execute directly inside bound
  handlers. A large export or real training run can freeze the native dispatch path while the
  frontend displays a progress state that cannot actually animate or cancel the work.

  Mark slow commands in **14.3**'s registry and return a job id within 50 ms. Progress,
  completion, structured failure, and cancellation arrive as events; only one training job
  runs at once, while independent exports have an explicit concurrency policy. A deliberately
  slow fake job must leave the UI heartbeat responsive. Cancellation and app shutdown must
  terminate/reap child processes, join workers, and guarantee no callback outlives `AppState`.
  Fast commands remain synchronous. Replace shell execution with an owned process API as part
  of this work, aligning with **4.2**, and do not promise cancellation until the child can
  actually be stopped.

- **14.7 — Move retention and space reclamation out of the launch critical path.** `M`
  `performance`
  Opened 2026-08-05. `main.cpp` blocks on `Storage::open()` before the webview is constructed.
  That open synchronously migrates, prunes every retained runtime table, and runs a full
  blocking `VACUUM` after only 500 deleted rows. At roughly one prediction/feature row per
  second, an ordinary day's expiry clears that threshold; a mature database can periodically
  rewrite itself while a user double-clicks and sees no window.

  Measure first with month- and 90-day fixtures. Keep lock acquisition, schema validation, and
  required migrations on the correctness-critical startup path, but schedule ordinary pruning
  and page reclamation after first paint. Make timestamp predicates indexable after **7.16**,
  add the missing useful global timestamp access path for high-volume feature rows, delete in
  bounded chunks, and choose incremental/freelist-ratio or byte-based reclamation from measured
  file growth rather than row count alone. A session start must be able to yield/cancel
  maintenance before it harms capture persistence.

  Acceptance: no full `VACUUM` runs before the first window; the same-host mature-fixture p95
  for launch-to-visible improves by at least 80% over the captured baseline; expiry leaves no
  out-of-policy rows; engine write latency stays inside its existing benchmark bound; and a
  crash between chunks resumes safely without a second deletion interpretation. Publish last
  maintenance time/result and pending reclaim bytes in diagnostics. Reuse **14.5**'s deadline
  scheduler or **14.6**'s owned jobs rather than starting another unmanaged thread, and align
  the policy with user-configurable retention in **9.10**.

---

## Recurring health checks

Checks to run on a cadence, not one-off tasks. Several are automatable; where so, that's
itself a backlog item below.

### Before every release

- [ ] Open a **pre-existing** `focoflow.db` written by an earlier install and run a
      full session end-to-end. The 7.11 fixture corpus now makes this directly runnable.
- [ ] Kill the process uncleanly mid-session, restart, confirm WAL recovery and that the
      orphaned `ACTIVE` session resumes (`state.cpp:158` claims to handle this — verify it).
- [ ] Run a session on each OS long enough to exceed the ring buffer under load, and confirm
      `capture_events_dropped` reflects reality; 7.4/7.17 expose the signal, this validates
      it under a real desktop workload.
- [ ] Confirm every `invoke(...)` string in `frontend/src/api.ts` resolves in
      `commands.hpp`. `test_ipc_contract` covers the C++ side; confirm it covers the TS side
      too. This contract is easy to drift and a mismatch fails silently at runtime; 14.3 is
      the structural fix.
- [ ] Confirm the release tag equals CMake's version, names a commit reachable from protected
      `master`, and carries the full CI result required by 9.11.
- [ ] Extract every release artifact and verify the project license, dependency notices,
      frontend bundle, executable signature where required, and launchable binary are inside.
- [ ] Feed a window title containing invalid UTF-8, U+2028, quotes, and backslashes through
      the full pipeline. Covers 8.1 and 8.2 in one test.

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

### Candidates for new CI jobs

- [ ] **Schema-drift job:** diff the current `CREATE TABLE` statements against a checked-in
      snapshot and fail on divergence. Guards the 7.3 compatibility promise directly.
- [ ] **Scale job:** seed a month of synthetic usage; assert analytics/summary return correct
      counts inside a time budget. Would have caught 7.1; guards 7.12.
- [ ] **Health-truthfulness job:** force each failure mode (dead hook, over-broad exclusion,
      persistence failure, no session) and assert `HealthStatus` reports something other than
      healthy. Capture/prediction fields are unblocked by 7.4 and 7.10; persistence waits on
      9.6. The point is that health fields must never be literals again.
- [ ] **Stack-size assertion:** `static_assert(sizeof(AppState) < N)`. One line, permanently
      prevents 6.1's class of regression.
- [ ] **Dead-header job:** automate the dead-code sweep above. It's the check that would have
      caught 2.4 for free.

---

## Done archive

Completed work. Kept for history; further detail lives in the git log.

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
