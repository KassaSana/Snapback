# Astra review — findings and reconciliation

This is the preserved source review for later roadmap triage. It contains Astra’s
C++/systems audit, frontend/product audit, and final reconciliation. The synthesis
comes first; detailed reasoning, reproduction ideas, test gaps, learning points,
and original audit instructions follow.

## How coding agents should use this review

- **Status:** reference material, not an accepted implementation plan or a second
  backlog. [ROADMAP.md](ROADMAP.md) remains the sole live backlog. The twelve
  proposals below retain the review’s suggested scope and order; they have not
  been turned into tickets by this editorial pass.
- **Evidence:** all three passes were read-only source inspections. The reviewer
  did not run builds/tests or launch the app. “Confirmed,” “verified,” and
  “current” describe the code inspected then, not runtime reproduction or a fresh
  audit of today’s checkout. Performance impact and rendered UI behavior remain
  unmeasured unless explicitly stated otherwise.
- **Precedence:** use the final reconciliation when the review passes overlap.
  Before implementation, check the current code, roadmap, and accepted
  [ADRs](adr/README.md). Review recommendations do not override those decisions.
- **Provenance:** the imported transcript supplied times of day, but no audit date
  or commit SHA. Do not infer either from this file’s reorganization date
  (2026-09-19). Source filenames and function names are retained as search clues;
  copied line numbers were removed because they are not durable citations.
- **Scope of this edit:** organization and formatting only. Findings, severity,
  caveats, reproduction ideas, acceptance criteria, rejected proposals, and ML
  evaluation conditions are preserved. Copied application controls, timestamps,
  usage-limit messages, and broken icon markup were removed.

### Reading order

1. [Final reconciliation](#reconciliation): priorities, evidence gates, deferrals,
   and twelve proposed implementation slices.
2. [Proposal-to-evidence map](#evidence-map): jump from a proposed slice to its
   detailed source finding. C1–C6, T1–T3, and P1 are original audit labels, not tickets.
3. [C++ / systems audit](#systems-audit): failure modes, reproductions, smallest
   repairs, test gaps, learning points, and known-good invariants.
4. [Frontend / product audit](#frontend-audit): bugs, workflow ownership, product
   simplification, accessibility, and qualified visual observations.
5. [Original audit briefs](#audit-briefs): the scope and constraints that produced
   these conclusions, plus the systems pass’s interim observations.

### Later ticket conversion

For each retained proposal, recheck the failure against current code and its
existing roadmap owner before creating or reopening work. Carry forward the
source finding, concrete failure/invariant, smallest repair, dependencies,
acceptance criteria, and required validation. Record whether evidence is source
inspection, reproduction, or measurement. Preserve decision/ADR gates and the
review’s explicit deferrals. Proposal numbers are local navigation labels only;
assign any real tracking identifiers in the roadmap when triage happens.

<a id="evidence-map"></a>

## Proposal-to-evidence map

| Proposed slice | Supporting detail | Existing roadmap owner named by the review | Dependency |
| --- | --- | --- | --- |
| [1. Attendance recovery](#slice-1) | [C1](#systems-c1) | 9.6; related 7.23/P0-05 | None |
| [2. Wake context](#slice-2) | [C2](#systems-c2) | AUD-02 remainder | None |
| [3. Recording/privacy state](#slice-3) | [Recording status](#frontend-1) | 2.10/2.16/14.4 | None |
| [4. Session switching and drafts](#slice-4) | [Switching](#frontend-2), [draft mode](#frontend-3) | 2.11/14.4 | None |
| [5. Review interval provenance](#slice-5) | [Stale results](#frontend-4), [mixed scopes](#frontend-6) | 10.11; AUD-08 | None |
| [6. Windows mouse speed](#slice-6) | [C4](#systems-c4) | 7.27 | None |
| [7. Shutdown drain](#slice-7) | [T2](#systems-t2) | 14.5 follow-up | None |
| [8. Export/delete exclusion](#slice-8) | [T3](#systems-t3) | 8.12/9.16/14.6 | None |
| [9. Export episode completeness](#slice-9) | [C6](#systems-c6) | Reopen 9.16 | Proposal 8 |
| [10. Model-file exclusion](#slice-10) | [T1](#systems-t1) | 14.6/Tier 13 | None |
| [11. Atomic model rollback](#slice-11) | [C3](#systems-c3) | Tier 13 rollback | Proposal 10 |
| [12. Prediction provenance](#slice-12) | [C5](#systems-c5) | Tier 13 follow-up | None |

This map covers only the twelve proposed slices. Foreground-probe cancellation,
[P1’s bounded-memory export finding](#systems-p1), accessibility, product work,
release constraints, and conditional ML evaluation remain in the full review;
their absence from this shortlist does not discard them.

<a id="reconciliation"></a>

## Final reconciliation

**The next roadmap should prioritize correctness and truthful UI state. No model replacement, retraining project, queue rewrite, or broad architectural refactor is justified by the evidence reviewed.**

I reconciled **ROADMAP.md**, the historical audit, and the completed **“Audit Snapback C++ implementation”** and **“Audit Snapback frontend product”** findings against current implementation and tests.

This was read-only: no files changed, builds/tests run, or application launched. “Verified” below means the failure follows from the current code; runtime frequency and performance impact remain unmeasured.

### 1. Immediate correctness work

| Retained finding | Current-code evidence | Roadmap disposition |
| --- | --- | --- |
| Attendance can remain wrong after a failed transaction                  | update_idle_unlocked advances session_attended_; engine_tick removes the pending transition before committing. Failure leaves no transition to retry.             | Extend **9.6**, referencing **7.23/P0-05**. Their stopped-session protection does not solve commit failure. |
| Waking in another application leaves stale classifier context           | Events drain while idle_ is still true. Feature ingestion skips foreground changes; synthesized IdleEnd does not replace initialized app/title context.           | Narrow **AUD-02/Phase 2** to this remaining defect. Idle synthesis itself exists.                           |
| Recording/privacy indicators disagree with native state                 | Recording refresh follows session changes and direct actions, but lacks timed-expiry reconciliation. Privacy settings have separate state.                         | Reopen the relevant acceptance of **2.10/2.16**, implemented through a narrow **14.4** workflow repair.     |
| Session switching leaves incorrect or unusable UI state                 | Successful stop response is discarded before replacement start; switching is never reset after success. Draft mode changes call the native live/default setter.    | One bounded session-workflow ticket under **2.11/14.4**.                                                    |
| Review labels can describe a different interval from the displayed data | Range changes immediately, while previous datasets remain through loading and failure. Live context/prediction cards also sit beneath an “exact interval” promise. | Complete **10.11**, rather than add another reporting feature.                                              |
| Personal export falsely claims completeness                             | Episodes are limited to 10,000 per session, with no corresponding omission accounting.                                                                             | Reopen **9.16**.                                                                                            |
| Failed ONNX inference retains ONNX provenance                           | predict falls back, but model_id() still returns the loaded model identity.                                                                                        | Add a narrow Tier 13 correctness follow-up; not ML research.                                                |

The primary evidence is in **state.cpp**, **features.cpp**, **classifier.cpp**, **useSession.ts**, **useRecordingStatus.ts**, and **useReviewWorkflow.ts**.

### 2. C++/concurrency work

Keep these as specific invariants, not general “thread-safety improvements.”

| Work | Required invariant / failure mode | Existing owner |
| --- | --- | --- |
| Serialize model-file operations                         | Promotion, recovery, rollback, and loading must not observe or modify a live transaction’s intermediate files. Worker-side training currently overlaps synchronous recovery commands. | **14.6 + Tier 13**               |
| Make rollback failure-atomic                            | Model and quality metadata must remain a matching pair after failure or restart. Current rollback swaps them separately outside the deployment journal.                               | Tier 13 rollback follow-up       |
| Correct shutdown draining                               | Queue emptiness means completion only after the producer is quiescent. Healthy draining must not stop at 64 ticks when time-limited slices can consume only 128 events each.          | **14.5** correctness follow-up   |
| Exclude personal export from activity deletion          | Deletion cannot erase files/pages beneath an accepted export operation. personal_export_active exists but deletion does not check it.                                                 | **8.12/9.16/14.6**, one ticket   |
| Bound foreground-probe cancellation                     | A stuck child process must not indefinitely block capture or its shutdown join. POSIX run_command uses blocking pipe reads/waits.                                                     | **7.27/AUD-10**, including macOS |
| Bound Windows mouse-speed conversion                    | Same-timestamp movement must produce a finite, representable value before conversion; subtract coordinates after widening.                                                            | **7.27**                         |

Evidence: **command_handlers.cpp**, **training_deploy.cpp**, **active_window.cpp**, and **input_hook_windows.cpp**.

**Retain the existing SPSC queue and ranked lock order.** The reviewed **ring buffer** has the expected publication and slot-reuse handoffs. Neither audit established a reason to replace it or weaken its memory ordering.

### 3. Measured performance work

There are four defensible candidates. Only the first has an already-established asymptotic defect; none has a fresh measured impact from this reconciliation.

| Candidate | Measurement required before optimization | Acceptance |
| --- | --- | --- |
| **9.16: bounded-memory export**                                 | Measure exporter-owned peak memory at fixed page size with 1× and 10× output. body += chunk currently retains the whole archive. | Incremental checksum produces identical results; working memory remains bounded by pages/chunks rather than output size. |
| **14.1 + 4.4: reporting contention**                            | Run mature-history reads concurrently with production-like persistence; record writer p95/p99 delay, queue growth, and drops.    | Introduce a query lane only if measured contention crosses a declared budget. Close with numbers if immaterial.          |
| **14.5/FWD-08: quiet-engine wakeups**                           | Record quiet CPU/wakeups and active event-to-prediction latency on the same host/configuration.                                  | Reduce unnecessary wakeups without regressing timer deadlines, capture latency, or deliberate no-session previews.       |
| **14.7: startup pruning/reclamation**                           | Measure launch-to-visible on month/90-day fixtures, separating migration, pruning, and VACUUM cost.                              | Optimize the demonstrated launch bottleneck; preserve retention and migration correctness.                               |

For frontend fetching, **14.4 already supplies a measurable target**: zero context-history requests when no visible consumer needs them. The teaching card is a real consumer, so “Review hidden” alone is insufficient.

The **benchmark harness** needs representative workloads before supporting further optimization claims: its producer constructs string-bearing events, and its consumer classifies every event, unlike production’s throttled path. Existing **baselines** are useful historical evidence, not measurements of today’s proposed changes.

Defer allocation pooling, title-copy elimination, additional SQLite connections, rendering rewrites, and virtualization until those measurements identify a worthwhile target.

### 4. Simplification/refactoring work

Approve only changes directly needed to repair an established problem:

- **14.4:** Give recording/privacy and session transitions coherent ownership. The maintenance problem is partial updates leaving contradictory state.
- **Tier 13:** Share ownership of model-file transactions. The correctness problem is recovery treating an active deployment as abandoned.
- **9.6:** Separate desired attendance from committed attendance. The correctness problem is in-memory state surviving database rollback.
- **14.2/11.4:** Add only the deterministic seams needed for transaction-failure and shutdown-interleaving tests. A complete engine-cycle extraction is not a prerequisite.

Defer **14.8’s full AppState decomposition**, **AUD-20/21 file splits**, generic storage interfaces, and singleton replacement as standalone projects. Line count and “cleaner ownership” are insufficient acceptance criteria.

**14.3’s native registry already exists.** Remaining argument/result-contract coverage belongs to that item; rebuilding the registry or introducing wholesale code generation is unnecessary.

### 5. UI/product work

Keep three distinct classes:

| Class | Retained work | Disposition |
| --- | --- | --- |
| Correctness/accessibility         | Recording state, session workflow, truthful Review intervals; permission-modal focus containment/restoration and scrolling; Settings controls inside their named panel | Existing **2.10/2.11/10.11/10.3**                                      |
| Existing capability made usable   | Consumer rollback in Diagnostics, after native recovery is safe; selected-session recap/context/episodes                                                               | **13.8/P0-08** and **2.9/2.15**                                        |
| Optional polish                   | Recap before optional feedback; consolidate repeat-goal controls; remove redundant pills/confirmation cards                                                            | Small follow-ups to **2.11/2.14/10.10**, outside the correctness queue |

The modal and panel issues are visible in **PermissionWizard.tsx** and **App.tsx**. Contrast and visual responsiveness still require rendered verification.

For **2.18**, immediately make global-rule consequences and persistent dismissal wording honest. Keep actual goal-scoped rules as a separate feature dependent on **7.28**; do not silently change existing global rules.

Merge **FWD-02’s digest** with the existing session-explorer work rather than building a second dashboard. A selected session is the smaller useful first slice. No wholesale redesign is justified.

### 6. ML work, if justified

**No model-changing work is justified now. That is an absence of evidence for improvement, not proof that the heuristic is accurate.**

The repository establishes:

- A 31-feature contract and deterministic feature/classifier fixtures.
- Heuristic scoring with shared context/policy treatment on the ONNX path.
- Optional ONNX inference, disabled by default, with invalid-output fallback.
- Export, developer-only process invocation, quality gating, deployment, and recovery machinery.
- No in-tree ml/pipeline_cli.py, representative labelled evaluation corpus, or reproducible real-world quality comparison found in the inspected sources.

The tests prove implementation properties and mechanics. They do not prove that focus labels correspond to users’ actual work. In particular, the existing ONNX failure test directly exercises output rejection; it does not verify persisted fallback provenance.

The **quality gate** accepts an accuracy scalar of at least **0.60**, and no lower than the previous same-named metric. It does not establish a shared evaluation dataset, independent labels, class coverage, leakage-free splits, or improvement over the heuristic. Exported labels include automatically generated labels alongside human submissions.

Therefore:

- **Keep now:** wake-context repair, correct prediction provenance, deployment exclusion, atomic rollback.
- **Merge and defer:** **13.5 + FWD-07 + 13.6** into one conditional evaluation milestone.
- **Defer:** **2.3/FWD-03**, personalization, new model architectures, threshold tuning, and consumer training UI.

Before any model change, that evaluation milestone must produce:

1. Independently adjudicated examples of consequential current errors, distinguished from capture/context bugs and user-policy overrides.
2. Explicit label scope and conflict handling; automatic labels cannot serve as independent ground truth.
3. Session/time-separated evaluation data with feature-contract and dataset identities.
4. Heuristic, majority baseline, and candidate results on the same held-out cases: confusion matrix, per-class precision/recall, macro-F1, and false interruption rate.
5. A predeclared minimum improvement and non-regression limits, plus runtime cost measurements.
6. Chronological replay that respects focus_momentum feedback, rather than blindly reusing baseline-generated features for every candidate.

The constant is_pseudo_productive, empty-app counting, and fixed-window stretch semantics are real contract questions. They do **not** independently justify retraining or removing columns. Preserve compatibility until a specific semantic change and its evaluation are agreed.

### 7. Delete or defer these proposals

| Proposal | Reconciled disposition |
| --- | --- |
| Reimplement P0-01…P0-08                                                                                                       | Remove from active work. Preserve **P0-09** as an outstanding release verification. |
| “Idle events are never synthesized”                                                                                           | Retire that premise; retain only the wake-context defect.                           |
| Rebuild bounded draining, async training, subprocess ownership, command registry, lazy Review hydration, or appearance tokens | Already present. Track only remaining acceptance gaps.                              |
| Treat **2.3** as awaiting the **13.7** decision                                                                               | Stale: ADR-0006 settled developer-only training.                                    |
| Mandatory Phase 3 personalization                                                                                             | Replace with the evaluation gate above; no automatic commitment to a trainer.       |
| SPSC rewrite, MPMC conversion, weaker atomics                                                                                 | Delete from the proposed queue. No established requirement.                         |
| AppState/file-size refactors, const cleanup, generic abstraction layers                                                       | Defer unless attached to a concrete maintenance failure.                            |
| “Stop classifying without a session”                                                                                          | Reject: live preview is intentional and supports untracked-work nudges.             |
| Rebuild quiet hours                                                                                                           | Already implemented; any additional notification budget needs separate evidence.    |
| Broad new CI/coverage projects                                                                                                | Defer; add targeted regression cases to existing suites.                            |
| Digest, recovery expansion, updater, platform breadth                                                                         | Keep under existing feature/release items, outside the immediate repair queue.      |

Signing, macOS packaging, upgrade/install checks, and the release soak remain existing release constraints. This reconciliation neither resolves them nor creates a second release backlog.

<a id="proposed-slices"></a>

### Twelve proposed implementation slices

These are proposed slices of existing roadmap items, not new permanent IDs. Independent tickets are ordered by practical priority; dependencies are explicit.

<a id="slice-1"></a>

#### Proposal 1: Recover attendance transitions after failed persistence — correctness, M

**Owner:** 9.6; related 7.23/P0-05. **Depends:** none.

Retain session/epoch-tagged transitions until successful commit, preserving their original boundaries. Acceptance: inject BEGIN/write/COMMIT failures for idle-close and wake-open; recovery converges without new input, delayed retries do not move timestamps, intervening transitions are not lost, and stop/replace/delete cannot resurrect attendance. A failed transaction must not permanently consume its pending snapback notification.

<a id="slice-2"></a>

#### Proposal 2: Resynchronize foreground context on wake — correctness, S/M

**Owner:** AUD-02 remainder. **Depends:** none.

Acceptance: IDE → idle → browser focus/input yields browser app/title/flags and browser rules on the first eligible prediction; waking input is counted once; exactly one IdleEnd resets the break clock; privacy exclusions still hold.

<a id="slice-3"></a>

#### Proposal 3: Unify recording/privacy refresh behavior — frontend correctness, M

**Owner:** 2.10/2.16/14.4. **Depends:** none.

Acceptance: Settings, header, and native state agree after pause/resume, timed expiry, idle/wake, and tray-originated changes. Expiry triggers reconciliation without another user action. Out-of-order responses cannot restore stale state; failed refresh is represented honestly.

<a id="slice-4"></a>

#### Proposal 4: Repair session switching and isolate its draft — frontend correctness, M

**Owner:** 2.11/14.4. **Depends:** none.

Acceptance: successful stop is reflected even if replacement start fails; successful switch → Stop → Start works without remounting; stale live cards clear; draft mode/goal edits issue no live/default mode mutation; cancelling the draft leaves the current session unchanged. Preserve explicit default-mode editing separately.

<a id="slice-5"></a>

#### Proposal 5: Make Review interval provenance truthful — reporting correctness, M

**Owner:** 10.11, with AUD-08 cap presentation. **Depends:** none.

Acceptance: every retained result carries its loaded interval; loading/failure never relabels old data as new; Retry works; older responses cannot overwrite newer results. Calendar attendance and rolling totals have explicit scopes, live cards are clearly outside the selected interval, and existing truncation metadata is displayed.

<a id="slice-6"></a>

#### Proposal 6: Make Windows mouse-speed conversion bounded — C++ correctness, S

**Owner:** 7.27. **Depends:** none.

Acceptance: pure translation tests cover equal timestamps, large displacement, coordinate extremes, and ordinary movement. Widen before subtraction and validate/clamp before narrowing. Existing event-category behavior remains unchanged.

<a id="slice-7"></a>

#### Proposal 7: Drain shutdown only after producer quiescence — concurrency correctness, M

**Owner:** 14.5 follow-up. **Depends:** none.

Acceptance: a barrier-controlled final publication is consumed; a full ring drains even when every slice hits the time budget; healthy progress is not cut off by a fixed tick count. Repeated failure/no-progress has a bounded, explicit termination path; producer, consumer, and maintenance threads join.

<a id="slice-8"></a>

#### Proposal 8: Make personal export and activity deletion mutually exclusive — concurrency correctness, S

**Owner:** 8.12/9.16/14.6. **Depends:** none.

Acceptance: reserve exclusion before export is queued; deletion during queued/running export is refused consistently; completion, exception, cancellation, and shutdown release ownership. Real registry/bridge tests cover both operation orderings and returned-path validity.

<a id="slice-9"></a>

#### Proposal 9: Remove silent episode loss from personal export — data correctness, S/M

**Owner:** reopen 9.16. **Depends:** 8.

Add stable episode pagination. Acceptance: export a session with 10,001+ episodes, including equal timestamps, with no omissions or duplicates; counts match rendered rows; failure never reports successful completeness. Keep incremental-hash optimization as the separately measured performance slice.

<a id="slice-10"></a>

#### Proposal 10: Give model-file operations one exclusion policy — concurrency correctness, M

**Owner:** 14.6/Tier 13. **Depends:** none.

Acceptance: pause promotion before marker publication and at later transaction boundaries; reload, cleanup, and rollback cannot touch its files. Conflicting commands return a clear busy result rather than blocking the UI indefinitely. Ownership releases on success, failure, cancellation, and shutdown; lock ordering remains valid.

<a id="slice-11"></a>

#### Proposal 11: Make model rollback a recoverable pair transaction — correctness, M

**Owner:** Tier 13 rollback. **Depends:** 10.

Reuse staged/journaled transaction mechanics. Acceptance: failure or interruption at each model/metadata boundary recovers a matching pair, preserves the available rollback target, and handles optional metadata. Successful rollback remains reversible. Include Windows locked-file cases.

<a id="slice-12"></a>

#### Proposal 12: Persist the backend that produced each prediction — correctness, S

**Owner:** Tier 13 provenance follow-up. **Depends:** none.

Acceptance: actual failed ONNX inference followed by heuristic fallback persists heuristic identity; successful ONNX output persists its model identity; backend/degraded status agrees. Exercise the complete prediction-to-storage path, not only accept_output.

For implementation completion, each ticket needs its targeted regression tests plus the repository’s required native/frontend checks as applicable. Concurrency and ONNX changes should use the existing deep-check coverage. None needs a new framework, broad rewrite, or retraining project.

---

<a id="systems-audit"></a>

## C++ / systems audit

The highest-value findings are **attendance state surviving failed transactions, stale classifier context after idle, and concurrent model deployment/recovery**. The SPSC queue itself does not need a rewrite.

This was a read-only source audit. I inspected architecture, roadmap, relevant ADRs, tests, and benchmarks. I did not modify files or run builds/tests; “confirmed” below means established from the code, not reproduced experimentally.

### Critical correctness issues

No P0 defect was established. These are the substantive correctness findings, ranked within this category.

<a id="systems-c1"></a>

#### C1 — P1: Failed persistence can permanently desynchronize attendance. Confirmed; extends existing roadmap 9.6.

Files/functions: **AppState::update_idle_unlocked**, **AppState::engine_tick**, **Storage::Transaction**.

- **Failure:** session_attended_ changes before persistence, and the pending decision is removed with std::exchange. If BEGIN, a write, or COMMIT fails, SQLite rolls back but that in-memory state remains advanced. Subsequent ticks see no difference to reconcile. A failed idle-close keeps counting absence as attendance; a failed wake-open loses attended time. The 500 ms busy timeout reduces frequency without repairing this.
- **Reproduce:** Start a session, advance the injected clock into idle, and make that tick’s transaction fail. Release the failure and tick again without input. The database still has an open span although the application considers attendance closed.
- **Smallest repair:** Retain a session-tagged pending transition until commit succeeds. Keep desired attendance separate from committed attendance, with an absolute transition timestamp so retries do not shift the boundary. Apply the same acknowledgement discipline to one-shot emissions consumed before persistence.
- **Test gap:** The **exception-containment test** throws from the emit hook; it proves thread survival, not transaction recovery.
- **Learn:** Exception containment is weaker than failure atomicity. A database transaction cannot roll back your C++ state.

<a id="systems-c2"></a>

#### C2 — P1: Waking in another application can leave classification attached to the previous application indefinitely. Confirmed; new failure mode.

Files/functions: **AppState::engine_tick**, **compute_event**, **FeatureExtractor::update_current_app**.

- **Failure:** The tick drains events before updating idle state. While idle, compute_event skips feature ingestion—including foreground changes. The synthesized IdleEnd does not update an already-initialized extractor’s current application. Later key/mouse events also do not update that context. Consequently, browser activity can retain IDE flags, title, goal alignment, and rule evaluation until another foreground-change event arrives.
- **Reproduce:** Establish Cursor context; become idle; queue a browser focus change and browser input; tick to wake; then send more browser keys. Inspect extracted app_name, window_title, and is_ide.
- **Smallest repair:** Preserve the latest permitted foreground context independently of input-count ingestion, and synchronize it into the extractor on wake. Process waking input after applying the wake transition.
- **Test gap:** The **existing wake test** uses Cursor before and after the break and checks only idle/break features.
- **Learn:** Dropping edge events can permanently invalidate cached state. Resumption needs reconciliation with current state.

<a id="systems-c3"></a>

#### C3 — P2: Rollback is not failure-atomic across model and quality metadata. Confirmed.

Files/functions: **rollback_model**, **swap_file / swap_optional_file**, **copy_over**.

- **Failure:** Rollback swaps model files first, then metadata, using remove-and-copy operations without the deployment transaction journal. A failure or crash between them leaves mismatched model/quality pairs. Failure after removing a destination can leave it absent; startup recovery does not interpret .rollback-temp.
- **Reproduce:** On Windows, prevent access to the quality file while permitting model access, then request rollback. Alternatively, interrupt immediately after the model swap. Restart and compare model identity with its quality metadata.
- **Smallest repair:** Reuse the staged, journaled pair-promotion mechanism for rollback.
- **Test gap:** The **rollback test** verifies successful reversible swaps only.
- **Learn:** Atomicity applies to the logical aggregate—the model and metadata together—not merely each file.

<a id="systems-c4"></a>

#### C4 — P2: Windows mouse-speed calculation can perform an undefined floating-to-integer conversion. Confirmed conditional defect.

File/function: **mouse_proc**.

- **Failure:** Equal GetTickCount64 timestamps produce a synthetic dt = 1e-6. A 5,000-pixel displacement then calculates 5,000,000,000 pixels/second, outside uint32_t. Converting that floating value is undefined behavior, not guaranteed unsigned wraparound. [C++ conversion rule](https://eel.is/c%2B%2Bdraft/conv.fpint).
- **Reproduce:** Exercise two same-timestamp samples separated by 5,000 pixels in a pure translation test, with float-cast-overflow instrumentation. Cursor warps across large desktops are a plausible live trigger; I did not reproduce one.
- **Smallest repair:** Use an appropriate high-resolution duration and explicitly bound the finite result before conversion. Convert coordinate operands before subtraction as well.
- **Test gap:** The **Windows translation test** checks message categories, not kinematics.
- **Learn:** Range validation must precede narrowing conversions; inventing a tiny denominator can amplify ordinary input into overflow.

<a id="systems-c5"></a>

#### C5 — P2: Heuristic fallback predictions retain the loaded ONNX model’s identity. Confirmed; optional backend only.

Files/functions: **Classifier::predict and model_id**, **AppState::compute_event**.

- **Failure:** Failed inference correctly falls back to the heuristic, and backend() reports that. But model_id() still returns the loaded ONNX identity. Persisted predictions therefore attribute heuristic output to a model that failed to produce it.
- **Reproduce:** Load a model that initializes successfully but returns unusable output; classify and persist a prediction. Compare the effective backend with the row’s model_id.
- **Smallest repair:** Select identity from the backend that actually produced the result. Returning provenance with the scores would make that association explicit.
- **Test gap:** **ONNX fallback tests** check backend/degraded status, not persisted identity.
- **Learn:** Configuration identity and execution provenance are different facts.

<a id="systems-c6"></a>

#### C6 — P2: Personal exports silently omit episodes while declaring completeness. Confirmed; reopen 9.16.

Files/functions: **AppState::export_personal_data**, **Storage::list_snapback_episodes**, **PersonalArchiveExport::truncated**.

- **Failure:** Episodes are capped at 10,000 per session. No omitted-episode count exists, so the footer can still say “Nothing was left out.”
- **Reproduce:** Seed 10,001 episodes in one session and export. Expect 10,000 exported, truncated() == false.
- **Smallest repair:** Page episodes with a stable cursor; alternatively, report the actual omission explicitly.
- **Test gap:** The **completeness fixture** crosses session/window caps, not the episode cap.
- **Learn:** Completeness requires accounting for every record type; a derived flag is only as complete as its inputs.

### Concurrency/threading issues

<a id="systems-t1"></a>

#### T1 — P1: Model recovery can run against an active deployment. Confirmed filesystem race.

Files/functions: **command registration**, **sync_trained_model_to_app_dir**, **recover_model_deployment_impl**, **reload_classifier_model**.

- **Failure:** Training promotes files on the worker. Reload, rollback, and cleanup remain synchronous UI commands and do not consult training_active. Recovery can delete the active worker’s staging files or restore its backups as though it had crashed. AppState’s mutex does not protect these worker-side filesystem operations.
- **Reproduce:** Pause promotion after staging but before marker publication; invoke reload or cleanup; resume promotion. Recovery deletes staging and the promotion subsequently fails. Repeat at post-marker boundaries.
- **Smallest repair:** Make all deployment/recovery/rollback/load operations share one exclusion mechanism. The smallest production fix is rejecting conflicting commands while training owns the files.
- **Test gap:** **Deployment recovery tests** simulate interrupted transactions serially; they do not run recovery beside a live transaction.
- **Learn:** Crash recovery assumes exclusive ownership. Moving work to another thread changes that assumption even without a C++ data race.

<a id="systems-t2"></a>

#### T2 — P2: Shutdown can leave successfully captured events undrained. Confirmed.

Files/functions: **start_engine_impl**, **stop_engine**.

Two independent paths exist:

- engine_running_ becomes false before the producer is joined. The consumer can observe an empty ring and exit while a final callback is still about to publish.
- The 64-tick shutdown cap assumes count-bounded slices. Time-bounded slices can consume only 128 events; 64 such slices drain 8,192 events, well below the ring’s 65,535 usable slots.

**Reproduce:** Hold a final producer publication until the consumer has observed stop and an empty ring. Separately, fill the ring and force the time budget to expire at each 128-event checkpoint.

**Smallest repair:** Quiesce/join the producer before allowing empty-queue termination. Limit consecutive failed/no-progress attempts rather than healthy shutdown ticks.

The **shutdown test** waits for a small burst to finish before stopping, missing both cases. **Learn:** Queue emptiness proves completion only after producers are quiescent; liveness bounds must account for every batching limit.

<a id="systems-t3"></a>

#### T3 — P2: Personal export lacks the deletion gate used by the other activity exports. Confirmed.

Files/functions: **delete_all_activity_data handler**, **export_my_data registration**.

- **Failure:** personal_export_active exists but deletion never checks it. Deletion can therefore interleave with page reads and output writes. On POSIX, unlinking an open export can leave the exporter reporting success for a nonexistent path; Windows can instead report partial deletion because the file is open.
- **Reproduce:** Pause export after opening its output and reading a page; invoke deletion; resume and inspect both command results and the returned path.
- **Smallest repair:** Include the personal-export gate in the same deletion exclusion policy.
- **Test gap:** Registry tests check async markings, not this operation pair.
- **Learn:** Per-command single-flight gates do not automatically protect shared resources across different commands.

**Known platform liveness gap:** **active_window.cpp::run_command** uses blocking popen reads and pclose without cancellation. macOS performs these probes on its event-loop thread; Linux performs them inside capture processing. A stuck child can block capture and CaptureThread::stop()’s join. The unbounded wait is confirmed; no live hang was reproduced. A deliberately blocked probe measures it directly. Add a deadline/cancellation boundary, preferably reusing the owned subprocess machinery. This belongs under **7.27/AUD-10**, with explicit macOS shutdown acceptance, rather than a duplicate “capture is slow” ticket. The concept is **transitive cancellation**: stopping the owner must reach its blocking dependencies.

The synchronization mechanisms that held up:

- **RingBuffer** publishes initialized slots with release/acquire and protects slot reuse with the reverse release/acquire handoff.
- Shared immutable capture context has a coherent ownership transfer.
- Reviewed AppState paths respect state → activity-boundary → storage lock ordering.
- Concurrent capture lifecycle control remains the **already-known AUD-17 constraint**, not a newly demonstrated production race.

### Performance opportunities

<a id="systems-p1"></a>

#### P1 — P3: The personal export is not bounded-memory streaming. Confirmed structural cost; magnitude unmeasured.

Files/functions: **export_personal_data**, **archive_checksum**.

Every emitted chunk is appended to body, which retains the entire document until the final checksum pass. Peak memory is therefore **O(total output bytes), not O(page size)**, and hashing scans the complete retained body afterward.

Measure peak allocations while increasing export size tenfold at fixed page size. The smallest repair is incremental hashing during emission. Existing export tests validate contents and paging, not peak memory. **Concept:** Streaming must hold across every stage, including checksums. This contradicts the completed **9.16** claim and belongs there.

I would **not** approve other performance rewrites from this audit alone:

- **Producer benchmarks** do not model the complete current shared-context → materialization path.
- **Consumer benchmarking** extracts/classifies every event; production throttles classification.
- SQLite benchmarks measure isolated writes, not mature reporting competing with persistence.
- Training export already has a separate read connection and a **WAL snapshot test**.

Per-event title materialization and feature scratch allocations are measurable candidates, not established bottlenecks. Existing **4.4/14.1** are the right place to establish their value.

### Complexity/refactoring opportunities

Only small changes tied to the defects above are justified:

- Separate **desired** from **committed** attendance.
- Give the model-file transaction one owner shared by promotion, recovery, and rollback.
- Give foreground context an explicit resynchronization path.
- Hash exports incrementally.

The larger deterministic-cycle and AppState decomposition work is already **14.2/14.8**. I would not add another “split the class,” generic storage interface, queue replacement, or weaker-memory-ordering project.

### Missing tests/invariants

The highest-value additions are:

1. Failed attendance close/open must converge after persistence recovers.
2. Wake in a different app must update classifier context and enforce that app’s rules.
3. Promotion must exclude recovery/reload/rollback at every transaction boundary.
4. Rollback failure must preserve a matching model/metadata pair.
5. Shutdown must drain final publications and time-budget-limited batches.
6. Same-timestamp mouse samples must produce finite, representable speed.
7. Fallback predictions must persist heuristic provenance.
8. Export/delete must be mutually consistent; export completeness must include episodes; memory must remain bounded at fixed page size.

These target gaps in otherwise substantial coverage—not an absence of tests.

### Existing roadmap items that already cover findings

| Existing item | Audit disposition |
| --- | --- |
| **9.6**, September 17 busy-timeout follow-up | Persistence failures already known; C1 adds the persistent attendance inconsistency.                                                                     |
| **Phase 2 / AUD-02**                         | IdleEnd synthesis now exists. Do not repeat the old “never produced” finding; C2 is the remaining context-resumption defect.                             |
| **7.27 / AUD-09 / AUD-10**                   | Windows polling gaps, macOS zero mouse speed, Linux event mapping/probing/calendar issues remain existing work. Add C4 and cancellation acceptance here. |
| **9.16**                                     | Reopen its completeness and bounded-memory claims for C6/P1.                                                                                             |
| **14.5**                                     | Count/time drain bounds already landed. T2 is a shutdown regression gap, not missing bounded draining.                                                   |
| **14.6**                                     | Owned async commands exist. T1/T3 expose incomplete resource-exclusion policies.                                                                         |
| **4.4 / 14.1 / 14.7**                        | Performance gates, reporting contention measurement, and startup prune/VACUUM remain known work.                                                         |
| **7.29, 14.2, 14.8, AUD-17**                 | Platform lifecycle signals, deterministic cycle, decomposition, and single-controller capture assumptions are not new findings.                          |

### Genuinely new candidate backlog items

In engineering-value order:

1. Reconcile classifier foreground context across idle/resume.
2. Serialize live model deployment with recovery, reload, and rollback.
3. Make model rollback failure-atomic.
4. Guarantee shutdown draining after producer quiescence, including time-budget slices.
5. Bound Windows mouse-speed conversion.
6. Preserve actual inference provenance on fallback.
7. Include personal export in activity-deletion exclusion.

Attach the attendance recovery, export completeness, and streaming-memory findings to the existing items identified above rather than creating duplicate backlog entries.

---

<a id="frontend-audit"></a>

## Frontend / product audit

Snapback’s biggest opportunity is **making its existing workflows agree with native reality, then reducing competing controls**. The three-surface structure is sound; a wholesale redesign would discard useful work.

I read the roadmap and architecture documentation before tracing the frontend and IPC. This was a source-level audit: no files modified, builds or tests run, or application launched. Visual and responsiveness observations below are therefore qualified, not claims from rendered testing.

The existing foundation deserves preservation: Now/Review/Settings, grouped Settings, semantic appearance tokens, memoized stable cards, and lazy Review hydration are implemented. The bridge also has centralized registration, capability checks, error envelopes, and owned asynchronous execution. The roadmap’s earlier “no real bridge tests” description is superseded by its September acceptance-test updates.

### 1. Actual bugs

<a id="frontend-1"></a>

#### Recording status can remain wrong after native state changes.
**useRecordingStatus.ts**, **useAppEffects.ts**, **usePrivacy.ts**

The comments promise polling, but recording status refreshes on session identity/status changes and explicit actions—not periodically. Idle events update userIdle without refreshing recording status. Settings’ private-mode toggle updates a separate privacy object; header pause/resume actions update only recording status.

Consequences include:

- A timed pause expires natively while the header still says “Paused privately.”
- Settings and the header disagree about private mode.
- Idle and tray-originated changes leave the header stale.

Use one authoritative recording/privacy workflow, refreshed on native transitions and deadline expiry. This is a correctness follow-up to **2.10/2.16**, with ownership work under **14.4**, rather than another status widget.

<a id="frontend-2"></a>

#### Session switching has both failure-path and ordinary-use defects.
**useSession.ts**, **SessionControlCard.tsx**

handleSwitchSession discards the successful stop response. If starting the replacement fails, React retains the old ACTIVE record even though native storage stopped it.

Separately, the card never resets its switching flag after a successful switch. If the user subsequently stops that session, the start form remains in switching mode, but switchable requires an active session: its submit button is disabled until the component remounts.

Apply the stopped record immediately, and explicitly finish/reset the switch interaction. These are new concrete regression cases for **2.11**, not reasons to add a native switching feature.

<a id="frontend-3"></a>

#### Editing a replacement session changes the current session’s classification mode.
**SessionControlCard.tsx**, **useSession.ts**, **state.cpp**

Selecting a mode—or applying a goal suggestion—calls set_focus_mode. Native set_focus_mode changes both the saved default and live focus_mode_. Therefore preparing a replacement session already changes the current session’s policy, even if the user chooses “Keep this session.” The displayed running mode still comes from sessionRecord.focusMode. Failed mode writes are silently ignored.

Separate draft mode from persisted/default/live mode. Commit the draft through Start. Track this alongside **2.11/14.4**.

<a id="frontend-4"></a>

#### Review can label old results with a newly selected interval.
**useReviewWorkflow.ts**, **App.tsx**

Changing range immediately changes card labels while previous datasets remain visible. If a request fails, the old data persists indefinitely under the new label. Request generations prevent older responses overwriting newer ones, but do not solve this presentation mismatch.

Keep the loaded interval attached to the loaded data; show an explicitly stale result or replace it with a loading/error state. Provide Retry. This directly completes **10.11**’s existing stale-result acceptance criteria.

<a id="frontend-5"></a>

#### The permission modal declares behavior it does not implement.
**PermissionWizard.tsx**, **styles.css**, **App.tsx**

The wizard has aria-modal="true" but no initial focus, focus containment, background inertness, or focus restoration. Its centered fixed backdrop also lacks a bounded scrolling treatment for short windows or enlarged text.

Settings has another structural issue: its named tabpanel contains only the heading and introduction; the actual settings controls are siblings outside it.

Fix these within **10.3**, with short-window/zoom verification shared with **10.10**. Existing arrow-key tab navigation is useful and should remain.

### 2. UX/product problems

<a id="frontend-6"></a>

#### Review still mixes historical analysis with a live debugging feed.
**ReviewRangeBar.tsx**, **ActivityCards.tsx**, **SummaryCard.tsx**

The range bar promises “Every card below uses this exact interval,” but:

- Recent Predictions shows the latest eight live/history records.
- Context Timeline shows up to twenty snapshots for the current or last session.
- Attendance uses calendar periods for some presets while other totals use rolling windows.

The Summary caveat helps, but cannot make the page-level promise true. Move Recent Predictions to Advanced, and put context under a selected session. Complete interval alignment under **10.11**, with session selection under **2.9**. This removes clutter while improving interpretation.

<a id="frontend-7"></a>

#### Session completion asks for feedback before delivering the result.
**SessionReviewCards.tsx**, **SessionReflectionCard.tsx**

The render order is Check-in, Reflection, then Recap. Users encounter five rating choices and two text fields before the outcome they stopped to inspect. Saving a reflection leaves another whole confirmation card.

Lead with the recap; place optional reflection and rating in one disclosure below it. Show the existing automatic rating if asking users to retain or override it. This is simplification of completed **2.14**, not a missing journaling feature.

<a id="frontend-8"></a>

#### Starting repeat work has too many parallel mechanisms.
**SessionControlCard.tsx**

The same goals appear through autocomplete, Recent goals chips, Start last session, and pinned presets. Presets additionally expose reorder and remove buttons inline.

Keep the useful one-click repeat action and searchable goal field. Consolidate recent/pinned choices and move preset management behind an edit affordance. Preserve **2.11**’s capabilities while reducing the number of decisions on the start screen.

<a id="frontend-9"></a>

#### Contextual teaching looks session-specific but creates global rules.
**WorkAppTeachCard.tsx**, **ActivityCards.tsx**

“This session,” “The work,” and “Not the work” imply a contextual judgment. Their actions create global substring rules. “I’ll do this later” also persists completion, suppressing the teaching card rather than postponing it.

Finish the existing scope/Undo work in **2.18**; meanwhile make scope explicit and correct the dismissal wording. Avoid expanding this teaching UI until its consequences match its language.

### 3. Frontend architecture problems

<a id="frontend-10"></a>

#### Workflow ownership remains distributed despite the hook count.
**App.tsx**, **useSession.ts**, **useAppEffects.ts**

App still assembles cross-feature invalidation, while useSession separately stores session ID, record, goal draft, mode, recap, and prompt flags. The switch and privacy bugs illustrate the practical cost: one action updates only some representations.

Continue **14.4** incrementally: give Now/session and privacy workflows ownership of their transitions, derive identity/status from the authoritative record, and keep editable drafts separate. Merely splitting JSX into more files would not fix this.

<a id="frontend-11"></a>

#### There is evidenced unnecessary query work, but no measured rendering bottleneck here.
**useAppEffects.ts**, **useLiveData.ts**

Active sessions query context history every thirty seconds regardless of surface, and prediction events can request additional throttled refreshes. This is already **14.4**’s unfinished acceptance criterion.

Make fetching consumer-aware: Review needs history, and the visible work-app teaching card also consumes context. Stop history reads when neither needs them, or supply a smaller current-context value. Because storage reads share the native storage lane, avoiding unnecessary work has a concrete rationale; I found no basis for a rendering rewrite or speculative virtualization.

### 4. Purely visual polish

**SessionReviewCards.tsx**, **PomodoroCard.tsx**, **styles.css**

The remaining “generated collection of features” impression comes from repeated component framing: card, heading, pill, explanatory paragraph, button row—even for minor or optional interactions.

Reduce low-information pills such as “summary” and “end of session,” replace permanent saved-state cards with inline confirmation, and demote Pomodoro’s Skip/Restart controls below its primary action. Keep the existing palette and typography.

Responsive breakpoints and reduced-motion rules exist. However, chart detail relies on SVG titles without keyboard-focusable marks or an equivalent data view. That belongs to **10.3**, while rendered contrast and all-surface visual checks remain **10.10**—neither should be declared complete from source inspection.

### 5. Genuinely useful missing features

<a id="frontend-12"></a>

#### Consumer-accessible model recovery.
**App.tsx**, **DiagnosticsCard.tsx**, **command_handlers.cpp**

Native rollback is deliberately ungated, but its only frontend action lives in the developer-only training card. Diagnostics exposes cleanup and file reveal, not rollback. Surface recovery when available without exposing training configuration. This completes the intent of **P0-08/13.8**.

<a id="frontend-13"></a>

#### A small session detail view using existing stored evidence.
**storage.hpp**, **ActivityCards.tsx**, **InsightsCard.tsx**

Persisted snapback episodes already have a storage query, but no corresponding frontend API call. A selected session should show its recap, reflection, context, and interruptions together. This would answer “what happened?” more effectively than another aggregate chart.

This is already **2.9/2.15/FWD-02**. Start with session detail rather than adding a separate digest dashboard.

### The ten highest-value improvements, in order

1. Synchronize recording/private state with native transitions and timed expiry.
2. Repair session switching, including its ordinary Stop-after-switch path.
3. Separate session drafts from live/default focus mode.
4. Make Review results retain their actual interval through loading and failure.
5. Remove the live prediction feed from Review; place context under a selected session.
6. Put the recap first and collapse optional end-of-session input.
7. Complete permission-modal focus behavior and Settings panel semantics.
8. Make contextual rule scope explicit and reversible under **2.18**.
9. Expose existing model recovery to ordinary installations.
10. Consolidate repeat-goal controls and remove low-information card chrome.

---

<a id="audit-briefs"></a>

## Original audit briefs and process context

The following are historical instructions to the original reviewer, preserved to
explain scope. They are not new instructions to the agent reading this document.

### C++ / systems brief

> Perform a read-only senior-level audit of Snapback's C++ implementation.
>
> Do not modify files, update the roadmap, or commit anything.
>
> First understand the architecture and read the existing ROADMAP.md so you do not report already-known work as a new finding.
>
> Focus on:
>
> - correctness bugs and undefined behavior
> - ownership/lifetime problems
> - threading and synchronization
> - races, deadlocks, lock ordering, atomic usage, memory ordering
> - lock-free SPSC buffer correctness
> - unnecessary copying/allocation on hot paths
> - latency and throughput bottlenecks
> - SQLite concurrency and transaction behavior
> - capture → feature extraction → classifier → persistence pipeline
> - state synchronization between threads
> - platform-specific Windows/macOS/Linux behavior
> - error handling and failure recovery
> - C++ design complexity that can be simplified
> - abstractions that are genuinely harmful or unnecessary
> - tests that pass while failing to validate important behavior
>
> Do not suggest refactors merely for style.
>
> For every significant finding:
>
> 1. cite the exact files/functions involved
> 2. explain the concrete failure mode or cost
> 3. distinguish confirmed defect from suspicion
> 4. explain how to reproduce or measure it
> 5. suggest the smallest reasonable repair
> 6. state what C++/systems concept I should learn from it
>
> Also inspect existing benchmarks/tests before claiming something is slow or unsafe.
>
> At the end, produce:
>
> - Critical correctness issues
> - Concurrency/threading issues
> - Performance opportunities
> - Complexity/refactoring opportunities
> - Missing tests/invariants
> - Existing roadmap items that already cover findings
> - genuinely new candidate backlog items
>
> Rank only by severity/engineering value, not by how interesting the change is.
>
> Do not implement anything.

### Frontend / product brief

> Perform a read-only audit of Snapback's user-facing product, frontend architecture, and native/frontend boundary.
>
> Do not modify anything.
>
> Read the existing roadmap and architecture documentation first.
>
> Inspect:
>
> - React/TypeScript frontend structure
> - webview/native bridge and IPC
> - information architecture
> - duplicated or overly complicated UI state
> - visual hierarchy and consistency
> - screens/components that look generated or over-designed
> - unnecessary text, cards, controls, sections, or dashboard clutter
> - workflows that take too many actions
> - inconsistencies between frontend state and native state
> - features that exist in the backend but are poorly surfaced
> - product features that are incomplete or misleading
> - accessibility and responsiveness
> - frontend performance where there is evidence of a real problem
>
> Separate:
>
> 1. actual bugs
> 2. UX/product problems
> 3. frontend architecture problems
> 4. purely visual polish
> 5. genuinely useful missing features
>
> Do not propose a wholesale redesign.
>
> Prefer simplification and removal over adding more UI.
>
> For each significant recommendation, show the relevant files/components and explain why the change improves the product.
>
> Compare findings against ROADMAP.md so you do not duplicate existing tasks.
>
> Finish with the 5–10 highest-value improvements that would make Snapback feel like a deliberately engineered product rather than an AI-generated collection of features.
>
> Do not implement anything.

### Final reconciliation brief

> Read the existing ROADMAP.md and the findings from the previous C++/systems and product/frontend audits.
>
> Perform a final read-only reconciliation.
>
> Also inspect the classifier, feature extraction, ONNX path, training/deployment/evaluation infrastructure, and related tests sufficiently to determine whether any ML work is actually justified.
>
> Your job is NOT to find more work.
>
> Your job is to reduce everything into a trustworthy implementation roadmap.
>
> For every proposed item:
>
> - verify it against the current code
> - check whether ROADMAP.md already covers it
> - merge duplicates
> - remove speculative refactors
> - reject optimizations without evidence or a measurable hypothesis
> - separate bug fixes from performance work, refactors, features, UI polish, and ML/research
>
> For performance work, require a benchmark or measurable target before optimization.
>
> For concurrency work, require a concrete invariant/failure mode.
>
> For refactors, require a specific maintenance/correctness problem rather than "cleaner code."
>
> For ML changes, require evidence that the heuristic/current model is inadequate and define how improvement would be evaluated before changing the model.
>
> Return:
>
> 1. immediate correctness work
> 2. C++/concurrency work
> 3. measured performance work
> 4. simplification/refactoring work
> 5. UI/product work
> 6. ML work, if actually justified
> 7. items that should be deleted/deferred
>
> Then propose the next 8–12 implementation-ready tickets in dependency order with acceptance criteria.
>
> Do not modify ROADMAP.md yet.

### Systems pass — interim observations

These observations preceded the completed systems audit; use the completed
findings and final reconciliation for disposition.

I’ll map the architecture and known roadmap work first, then trace the C++ pipeline, synchronization, and failure paths. I’ll check tests and benchmarks against each finding and keep the audit read-only.

I’m using the low-level concurrency C++ skill to check ownership, memory ordering, and shutdown behavior. The architecture uses a single capture producer and engine consumer, immutable UI snapshots, and a shared storage mutex; I’ll verify those contracts in the code.

The ring’s acquire/release ordering looks correct under the documented SPSC contract, and a concurrent FIFO stress test covers it. I’m checking the surrounding handoffs next: session changes and failed SQLite transactions can invalidate work after capture has already been consumed.

I found a concrete recovery gap: the tick marks attendance as changed before the SQLite write commits. If that write fails, the database rolls back but the in-memory flag does not, so later ticks may never retry it. I’m also checking an idle-wake context issue and an export path that retains more data than its streaming comments suggest.
