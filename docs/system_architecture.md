# Snapback (C++) — System Architecture & Design

**Status:** Living document · **Audience:** Engineering leadership, senior ICs, reviewers
**Scope:** The single-binary C++/webview rewrite of Snapback (v0.2 parity target)
**Author role:** Principal Systems Architect
**Source of truth:** the Rust/Tauri original at `../FocoFlow-1/src-tauri/`; this document
describes the C++ port's realized design, not a greenfield proposal.

---

## 1. Executive Summary & System Boundaries

### 1.1 What the system does
Snapback is a **local-first focus telemetry and context-recovery agent**. It observes
low-level user input (keyboard/mouse) and the active window, derives a rolling feature
vector, classifies the user's focus state (`DISTRACTED` → `PSEUDO_PRODUCTIVE` →
`PRODUCTIVE` → `DEEP_FOCUS`), and — its namesake capability — detects the
*return-from-distraction edge* and hands the user back "where you left off." All
telemetry, sessions, labels, and context snapshots persist to a local SQLite database
(`focoflow.db`). A reused React dashboard renders live state and drives session control.

This C++ edition replaces the Rust/Tauri core of the original while **reusing the React
frontend unchanged**. The rewrite's thesis: everything Tauri gave us for free (window,
system webview, IPC glue, managed state) is now assembled by hand, and that assembly —
not the ML — is where the engineering risk concentrates.

### 1.2 In scope (system responsibilities)
- Global input capture (per-OS hooks) and active-window resolution.
- Deterministic feature extraction over 30 s / 5 min rolling windows (31-feature vector).
- Focus classification via a heuristic backend, with an optional ONNX backend.
- Context-recovery state tracking and snapback payload generation.
- Durable persistence: sessions, predictions, feature snapshots, labels, context
  snapshots, app rules.
- A command/IPC bridge exposing a fixed contract to the webview frontend.

### 1.3 Explicitly out of scope (boundaries)
- **No network plane.** The system is offline by construction; there is no server, no
  sync, no telemetry egress. This is a privacy boundary, not an oversight.
- **No model training in-process.** Training/deployment is delegated to an external
  Python pipeline; the app exports CSVs, records the configured repo path, and shells out
  to `ml.pipeline_cli` when requested.
- **No multi-user / multi-tenant semantics.** One local user, one database file.
- **Frontend rendering & UX logic** belong to the reused React app; the C++ core owns
  only the data + event contract beneath it.

### 1.4 Quality attributes prioritized
1. **Behavioral parity** with the Rust original (numbers, states, wire format) —
   guarded by feature-parity fixtures.
2. **Determinism & testability** of the pure core (types → storage → engine) — headless,
   no OS or UI required.
3. **Capture-path safety** under a real producer/consumer thread split.
4. **Teaching clarity** — every deviation from Rust's guarantees is named and re-upheld.

---

## 2. Architectural Concepts & Core Topology

### 2.1 The pipeline
The system is a **staged data pipeline** with one hot producer thread and one consumer
thread, fronted by a UI-thread event loop:

```
 OS input hook (producer thread)
        │  push (lock-free)
        ▼
 SPSC RingBuffer<CaptureEvent, 65536>          ← capture/ring_buffer.hpp
        │  drain (consumer thread, ~10 Hz tick)
        ▼
 FeatureExtractor  → FeatureVector (31 dims)   ← engine/features.hpp
        │
        ▼
 Classifier (heuristic | ONNX)  → PredictionScores   ← engine/classifier.hpp
        │  (focus_state fed back to the tracker as on-task context)
        ├─► ContextTracker.observe_window_change / maybe_checkpoint_snapshot
        │      → ContextSnapshotDto?  (timeline)  +  SnapbackPayload?  (return edge)
        │      ← snapback/tracker.hpp
        │
        ▼
 Storage (SQLite: predictions, feature_snapshots, context_snapshots)  ← storage/storage.hpp
        │
        ▼
 AppState.emit_hook  ──dispatch──►  webview UI thread  ──eval──►  window.__snapback.emit
                                                                      │
                                                            React listeners (api.ts)
```

### 2.2 The four layers the frontend cares about

| Layer | Owner | Responsibility | Key type |
|---|---|---|---|
| **Storage** | `Storage` | Durable CRUD + migrations over `focoflow.db`; owns the `sqlite3*` handle | `src/storage/storage.hpp` |
| **State engine** | `AppState` | Orchestrates capture → features → classifier → tracker → storage on a periodic tick; holds `latest_prediction`, session, app-rule cache; guards all shared state with one `std::mutex` | `src/app/state.hpp` |
| **Command bridge** | `register_commands` | Binds 22 named commands to `AppState` methods; parses args, serializes results, converts exceptions to an error envelope | `src/app/commands.hpp` |
| **IPC layer** | `kIpcShim` + emit | A hand-written JS shim that satisfies `@tauri-apps/api` v2 (`window.__TAURI_INTERNALS__`) atop webview's binding protocol; a host→frontend event bus | `src/app/ipc_shim.hpp` |

**Topological invariant — the three-way name contract:** every command name must match
across (a) the frontend's `invoke(...)` calls (`../FocoFlow-1/frontend/src/api.ts`), (b)
`register_commands`, and (c) the Rust `generate_handler![...]` list. A mismatch fails
silently at runtime (the UI call rejects), so this contract is treated as sacred and is
the first thing a reviewer checks.

### 2.3 Ownership & lifetime topology
`AppState` is the composition root for the running system: it owns `Storage`,
`CaptureThread`, `FeatureExtractor`, `Classifier`, and `ContextTracker` by value. This
makes lifetimes trivially correct (destruction is deterministic and ordered) but makes
`AppState` a **large object** (see §4.3) that must live on the heap.

---

## 3. Design Patterns & OOP Decisions

We deliberately map each Rust idiom to an explicit C++ pattern. The value is not pattern
purity; it is that the pattern names the guarantee Rust used to give us for free.

### 3.1 State pattern — `ContextTracker`
The context-recovery machine is a finite-state automaton with three explicit states —
`Focused → Distracted → Recovering` — ported faithfully from Rust's `ContextTracker`
(`snapback/tracker.cpp`). Transitions are driven by window changes plus `is_on_task`
gating (app-rule + goal-alignment + focus-state feedback) and a minimum-distraction
threshold; it emits both the context-timeline snapshots and the return-from-distraction
`SnapbackPayload`. **Why:** the domain *is* a state machine; naming the states keeps the
"emit exactly once, on the edge" semantics auditable. **History:** an earlier, simpler
`SnapbackTracker` keyed off the classifier's `focus_state`; it was retired once the full
`ContextTracker` landed, so there is now a single tracker for both snapshots and snapback.

### 3.2 Strategy pattern — `Classifier` backend
`Classifier::predict` selects between a heuristic backend and an ONNX backend at runtime
(`engine/classifier.cpp`), reporting the active one via `backend()`.
**Why:** inference is a swappable policy; the heuristic must remain a correct fallback
when no model is present — exactly the Rust policy. **Flexibility:** a third backend
(e.g., a remote or quantized model) drops in without disturbing the tick loop.

### 3.3 Observer pattern — the emit hook + event bus
`AppState` exposes an `EmitHook` (`std::function<void(const char*, const std::string&)>`)
set by `main.cpp`. On each tick that produces a new prediction or snapback, the tick
notifies the observer *after releasing the lock*. The JS side (`kIpcShim`) is a second
Observer layer: `listen()` registrations are fanned out by `window.__snapback.emit`.
**Why:** decouples the core from the presentation transport; the core has no knowledge of
webview. **Flexibility:** the same hook can drive a native overlay (Phase 8) or a test
double.

### 3.4 Command pattern — the IPC bridge
Each frontend request is a named Command bound to a handler lambda over `AppState`
(`register_commands`). A single `bind_cmd` wrapper standardizes argument unwrapping,
result serialization, and error handling.
**Why:** it reifies "a request the UI can make" as a first-class, uniformly-handled unit,
replacing Tauri's codegen with one explicit, greppable list. **Flexibility:** cross-
cutting concerns (validation, logging, auth) attach in one place.

### 3.5 RAII — resource guarantees (the load-bearing idiom)
Rust's `Drop` becomes C++ destructors. Two instances matter most:
- `Storage` owns `sqlite3*` and closes it in `~Storage`; move-only, copy-deleted.
- `Stmt` (`storage.cpp`) wraps `sqlite3_stmt*`, guaranteeing `sqlite3_finalize` on every
  path — a missing finalize is a leak C++ will not catch for you.
**Why:** deterministic resource release without a GC or borrow checker; exception-safe by
construction.

### 3.6 Creational — Singleton (scoped) and Factory-ish opens
`OnnxModel::instance()` is a lazy singleton (the model is a process-global resource).
`Storage::open` / `open_memory` are static factory functions returning
`std::optional<Storage>` — Rust's `Result` mapped to "value or nothing," logging before
returning `nullopt`.

### 3.7 What we intentionally did *not* abstract
No generic plugin framework, no dependency-injection container, no event-bus library. The
system is small and single-purpose; premature abstraction would obscure the parity mapping
that is the project's whole point.

---

## 4. Data Structures & Memory-Management Trade-offs

### 4.1 The SPSC ring buffer — `RingBuffer<CaptureEvent, 65536>`
The capture hot path uses a **lock-free single-producer/single-consumer ring buffer**
backed by an inline `std::array<CaptureEvent, 1<<16>` with two atomics (head/tail) and
acquire/release ordering (`capture/ring_buffer.hpp`).
- **Time:** O(1) push/pop, wait-free on the producer.
- **Space:** fixed ~5 MB (65,536 × ~80 B/event), pre-allocated. No per-event allocation
  on the hot path — critical, because the producer runs inside the OS input queue and
  must not block or allocate.
- **Cache locality:** contiguous storage → sequential, prefetch-friendly access.
- **Back-pressure policy:** on overflow the producer *drops and counts*
  (`capture_events_dropped`), never blocks. Bounded memory beats unbounded latency for an
  input hook.
- **Trade-off:** fixed capacity means burst tolerance is capped (~1.3 s at 50k events/s).
  Sizing mirrors the Rust note.

### 4.2 The feature vector — fixed 31-dimension contract
`FeatureVector` is a fixed-order array of 31 doubles (`kFeatureCount`). **The order is a
wire contract** with the ONNX model input and the CSV exporter; reordering silently
corrupts both. Fixed-size, no indirection, trivially copyable — ideal for the tight
extract→classify loop.

### 4.3 `AppState` is large — and must be heap-allocated
Because `AppState` contains `CaptureThread` (hence the ~5 MB ring buffer) **by value**, an
`AppState` instance is multi-megabyte. MSVC reserves a function's entire stack frame on
entry, so a stack-local `AppState` in `main()` overflows the default 1 MB stack *before the
first statement executes* (observed as `0xC00000FD`). **Resolution:** `main` and the tests
allocate it via `std::make_unique<AppState>` (heap). This is the canonical example of a
guarantee Rust's `.manage()` gave us for free that we now uphold by hand.

### 4.4 SQLite storage structures
Persistence uses SQLite's B-tree tables with targeted composite indexes
(`idx_predictions_session_ts`, `idx_feature_snapshots_session_ts`) to keep the common
"latest N for a session, newest first" queries index-covered. Prepared statements are
reused via the `Stmt` RAII wrapper. Recap aggregates (`AVG`, `SUM/COUNT`) are pushed into
SQL rather than materialized in C++ — the database is the right engine for set math.

### 4.5 Small-value ergonomics
`std::optional<T>` models Rust's `Option`/nullable returns (`latest_prediction`,
`active_session`); `std::vector<T>` for row sets. Move semantics (`std::move`) transfer
ownership of `Storage` and drained snapback payloads without copies.

---

## 5. Concurrency & IPC Engineering

### 5.1 Thread model
Three threads of interest:
1. **OS hook thread (producer):** enriches each event with the active window and pushes to
   the ring buffer. Must stay fast and allocation-free.
2. **Engine thread (consumer):** wakes ~10 Hz (`engine_tick`), drains the ring buffer,
   runs the pipeline, persists, and gates predictions to ~1 Hz.
3. **UI thread:** owns the webview and its event loop; the only thread allowed to call
   `webview.eval`.

### 5.2 Synchronization strategy
- **Capture handoff:** lock-free SPSC. Correctness rests entirely on the two atomics and
  acquire/release ordering. Rust's `Send`/`Sync` + borrow checker made this safe by
  construction; here it is upheld manually and is the code to be most paranoid about.
- **Shared app state:** a single `std::mutex mutex_` guards all of `AppState`'s mutable
  members. Every public method locks; the tick locks once per wake.
- **Lock discipline (critical):** `engine_tick` performs *all* shared-state work under the
  lock, then **snapshots what to emit and releases the lock before invoking the emit
  hook.** The hook is never called while holding `mutex_` (CLAUDE.md rule: never hold the
  lock across a callback). This prevents coupling lock-hold time to webview latency and
  avoids re-entrancy deadlocks.

### 5.3 Cross-thread UI marshaling
`webview.eval` is **not thread-safe** and must run on the UI thread. The engine thread
therefore cannot emit directly. The emit hook (defined in `main.cpp`) wraps the call in
`w.dispatch([...]{ emit(w, …); })`, which enqueues onto the UI thread. Event/payload are
captured **by value** because the tick's strings do not outlive the hook call. On
shutdown the hook is cleared (`set_emit_hook(nullptr)`) before `stop_engine()` joins the
thread, so we never emit into a torn-down webview.

### 5.4 The IPC mechanism and its overhead
The frontend speaks Tauri v2's `@tauri-apps/api`, whose entire surface reduces to
`window.__TAURI_INTERNALS__.invoke(cmd, args)` and `transformCallback`. `kIpcShim`
implements exactly those, forwarding real commands to webview's `window[cmd](args)` binds
and modeling `listen`/`emit` on top.
- **Transport:** string-serialized JSON across the native↔JS boundary (webview's bind
  delivers arguments as a JSON *array* string; results return as JSON).
- **Overhead:** each call incurs a JSON encode/parse on both sides plus a webview IPC
  round trip. This is acceptable because command frequency is low (user actions + ~1 Hz
  event pushes), not a hot path. We would not route high-frequency data this way.
- **Error semantics:** a synchronous bind can only *resolve*, never *reject*. We adopt an
  envelope: handlers that fail return `{"__snapback_error": "…"}`, and the shim's `invoke`
  rejects the Promise on that key — faithfully reproducing Rust's `Result::Err` → frontend
  `.catch`.

### 5.5 Failure modes considered
- **Ring overflow:** bounded, counted, surfaced in `HealthStatus`.
- **Torn-down webview during shutdown:** hook cleared before join.
- **Lock contention:** single mutex is a known serialization point (see §7).
- **Bad frontend input:** validated at the bridge (length caps, required fields) exactly
  as `commands.rs` did.

---

## 6. Deep-Dive Trade-offs Matrix

| Decision | Alternative(s) | Pro (chosen) | Con (chosen) | Engineering justification |
|---|---|---|---|---|
| **Single-binary C++ + `webview/webview`** | Keep Tauri/Rust; or Electron | Full control; SQLite/ONNX are first-class C/C++; small footprint | Reassemble window/IPC/tray by hand | Project mandate: own and defend every line; backends are *easier* in C++ |
| **Lock-free SPSC ring buffer** | Mutex-guarded queue; channel lib | Wait-free producer; no alloc in hook; cache-friendly | Manual memory-ordering correctness; fixed capacity | The producer runs in the OS input queue — blocking/allocating there is unacceptable |
| **Fixed 65,536-slot inline array** | Dynamic/growable buffer | O(1), bounded memory, no reallocation jitter | ~5 MB always resident; forces heap `AppState` | Predictable latency > peak memory economy for a background agent |
| **Single `std::mutex` over `AppState`** | Fine-grained locks; lock-free state | Simple, obviously correct, easy to review | One serialization point | State mutation is infrequent (≤10 Hz); correctness and clarity outrank throughput here |
| **Emit after unlocking (snapshot pattern)** | Emit inside the lock | No lock coupled to webview; no re-entrancy | Slightly more tick code | Prevents deadlocks and unbounded lock-hold; matches the "never lock across callbacks" rule |
| **JSON-string IPC via shim** | Custom binary IPC; native bindings gen | Reuses the React app *unchanged*; tiny shim | Encode/parse overhead; stringly-typed | Command volume is low; reuse of the frontend is a hard requirement |
| **Heuristic default, ONNX optional (Strategy)** | ONNX-only; heuristic-only | Always-correct fallback; ships without a model | Two code paths to keep in parity | Mirrors Rust policy; the app must work with zero model artifacts |
| **Exceptions → error envelope** | Return codes; `std::expected` everywhere | Clean handler bodies; one catch site | Exceptions across the bind boundary need care | Reproduces Rust's `Result::Err` UX with minimal handler noise |
| **ContextTracker-gated snapshots** | Persist every window change inline | Matches the Rust on-task gate and 30 s focused checkpoint; reduces noisy timeline rows | Slightly more state in the tick loop | The timeline should be a recovery aid, not a complete browser history |
| **`std::optional<Storage>` from `open`** | Throw on open failure; global error | Explicit, callee-checked failure | Caller must handle `nullopt` | Rust `Result` mapped faithfully; startup failure must be loud, not silent |

---

## 7. Critique & Architectural Debt

We are shipping deliberate trade-offs. Naming them is the point.

### 7.1 Known scaling limits
- **Single-mutex contention.** All `AppState` access serializes on one lock. At the
  current ≤10 Hz tick and human-paced commands this is invisible; under a hypothetical
  high-frequency command load or a much faster tick it becomes the bottleneck. *Mitigation
  path:* split read-mostly state (latest prediction) behind an atomic/seqlock, or shard
  the lock.
- **Fixed ring-buffer capacity.** ~1.3 s of burst tolerance. Sustained input above the
  drain rate drops events (counted, not silent). *Mitigation path:* adaptive drain
  batching or a second consumer — but SPSC simplicity is worth keeping until proven
  insufficient.
- **~5 MB resident `AppState`.** Forces heap allocation and is wasteful if capture is
  disabled. *Mitigation path:* make the ring buffer a `unique_ptr`-owned heap block, or
  size it per-config.
- **String-JSON IPC.** Fine for control-plane traffic; it would not carry a high-rate data
  stream. If live charts ever need sub-second, high-volume series, introduce a batched or
  binary channel rather than per-sample `emit`.

### 7.2 Parity debt (intentional, tracked)
- **Cross-language fixture CI.** C++ replays `fixtures/feature_parity/scenarios.json` and
  `classifier_scenarios.json`; optional Rust dual-check remains a future CI checkout step.
- **Unicode validation.** Command length limits now count UTF-8 scalars (Rust `.chars()` parity).
- **Signing / installer parity.** Unsigned ZIP + IExpress + `-SignCertificate` hook documented in
  [`docs/PACKAGING.md`](PACKAGING.md). Auto-update deferred for v1.

### 7.3 Feature debt (phased, not accidental)
- **macOS/Linux capture depth.** Native taps are implemented (`CGEventTap` on macOS, evdev on
  Linux) with active-window polling fallback when permissions/devices are unavailable.
- **Frontend assets** are bundled beside `snapback.exe` for the Windows demo/release path;
  `SNAPBACK_FRONTEND_URL` remains a dev override for Vite.

### 7.4 Test-surface risk
The pure core (types, storage, engine, tracker, app-state) is covered headless. The two
places tests cannot fully reach are (a) global input injection and (b) deep live
webview interaction. The current automated smoke launches the Windows webview and verifies
that the main Snapback window appears; click-through GUI automation is the next step.

### 7.5 Overall assessment
The architecture is appropriately small for its domain: a staged pipeline, one lock-free
handoff, one coarse lock, and a thin reified command/IPC layer. The highest-value
engineering rigor is concentrated where it belongs — the capture handoff and the
cross-thread emit discipline — and the accepted debts are bounded, documented, and mapped
to phases. The primary architectural risk is not any single component but **drift from the
Rust source of truth**; the feature-parity fixtures are the control that keeps that risk
managed.
