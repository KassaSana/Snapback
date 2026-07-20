# PORT_HISTORY.md — how Snapback was ported from Rust to C++

> **This port is complete.** The phase-by-phase playbook below drove the Rust→C++ rewrite
> from an empty scaffold to an app that runs end-to-end (capture → engine → SQLite → webview
> IPC → reused React UI). It's kept as a **teaching record** — the ordering rationale and the
> `Learn:` / C++-vs-Rust notes are the reason the project exists. For what's left to build,
> see [ROADMAP.md](ROADMAP.md); this file is history, not a live TODO.

The Rust source at `../FocoFlow-1/src-tauri/src/` was the spec. For every phase, the
"Rust reference" line names the file that was read and ported.

> **The work loop each phase followed:** write the code → write/adjust the doctest →
> explain it to Kassa (bold takeaway + bullets) → suggest a one-line commit message.
> Never run git.

---

## Phase 0 — Environment & toolchain (no app code yet)

**Goal:** prove the toolchain compiles a trivial C++20 program with all deps wired.

**Steps**
1. Install: Visual Studio Build Tools (MSVC + C++ workload), CMake, Git. Confirm:
   `cmake --version` (≥ 3.20), `cl` reachable from a Developer prompt.
2. Vendored deps that FetchContent can't get:
   - **SQLite:** download the amalgamation, drop `sqlite3.c` + `sqlite3.h` into
     `third_party/sqlite/`.
   - **ONNX Runtime (optional, defer):** extract a release into
     `third_party/onnxruntime/`; leave `SNAPBACK_ONNX=OFF` for now.
3. Configure + build the existing scaffold:
   ```powershell
   cmake -S . -B build
   cmake --build build --config Release
   ```
   Expect stub link errors until Phase 1 fills bodies — that's fine. What must work:
   CMake configures, FetchContent pulls `nlohmann_json` + `doctest`, and the test
   target builds. `webview` is intentionally deferred until `SNAPBACK_BUILD_APP=ON`
   in Phase 6.
4. Build + run the tests target to prove doctest works:
   ```powershell
   cmake --build build --target snapback_tests --config Release
   ctest --test-dir build -C Release
   ```

**Done when:** `snapback_tests` builds and the title-parser test passes.

**Learn:** how CMake `FetchContent` replaces `Cargo.toml` dependency resolution;
why C++ needs an explicit build system where Cargo was implicit.

---

## Phase 1 — Types & the data model

**Rust reference:** `types.rs`.

**Goal:** `types.hpp` complete, plus JSON (de)serialization for every struct that
crosses the IPC boundary.

**Steps**
1. Finish `src/types.hpp` (mostly done in the scaffold): confirm every struct and
   enum from `types.rs` is present with matching field names/values.
2. Add `nlohmann/json` mappings. For each wire struct use `NLOHMANN_DEFINE_TYPE_*`
   with **camelCase** field names (Rust uses `#[serde(rename_all="camelCase")]`).
   Enums that serialize as strings (`FocusMode` = lowercase, `EventType` /
   `FocusLabel` = SCREAMING_SNAKE_CASE) need custom `to_json`/`from_json`.
3. Port `FocusMode`/`FocusLabel`/`AppRuleKind` parse+as_str helpers verbatim,
   including the fallback semantics (unknown `FocusMode` → Normal; unknown
   `AppRuleKind` → *absent*, not a default).

**Tests:** round-trip each enum through parse→string; serialize a `PredictionRecord`
and assert the JSON keys are camelCase. (Port `types.rs`'s `#[cfg(test)]` cases.)

**Done when:** types round-trip through JSON with the exact keys the frontend expects.

**Learn:** serde derives → hand-written `to_json/from_json`; how Rust's exhaustive
`match` becomes a `switch` you must remember to keep exhaustive (no compiler nag).

---

## Phase 2 — Storage (SQLite)

**Rust reference:** `storage/mod.rs`.

**Goal:** `Storage` opens `focoflow.db`, migrates the schema, and does session +
prediction + label CRUD. This is a self-contained subsystem you can test headless.

**Steps**
1. Wire `sqlite3.c` into the build (already in CMakeLists as the `sqlite3` target).
2. `Storage::open`: `sqlite3_open` on `app_data_dir / "focoflow.db"`, then `migrate()`.
   Return `std::optional<Storage>` (`nullopt` on failure) — this is Rust's `Result`
   mapped to optional; log the error first like the Rust setup() does.
3. `migrate()`: port every `CREATE TABLE IF NOT EXISTS` from `storage/mod.rs`
   **column-for-column**. The `feature_snapshots` columns must match
   `FEATURE_EXPORT_COLUMNS` order.
4. CRUD: `create_session`, `end_session`, `active_session`, `insert_prediction`,
   `insert_feature_snapshot`, `insert_label`, `recap`. Use prepared statements
   (`sqlite3_prepare_v2` / `bind` / `step` / `finalize`) — wrap in a small RAII
   `Stmt` helper so you never leak a statement.
5. `export_training_csv`: port the SELECT + CSV writing to produce `features.csv` +
   `labels.csv` byte-identical to the Rust exporter (the `ml/` trainer consumes them).

**Tests:** open an in-memory DB (`":memory:"`), create a session, insert a
prediction, read it back; assert `recap()` math matches a known fixture.

**Done when:** a throwaway `main` can create a session and round-trip a prediction.

**Learn:** RAII for `sqlite3*` and `sqlite3_stmt*` (destructors replace Rust's
`Drop`); why a missing `finalize` is a leak C++ won't catch for you.

---

## Phase 3 — Engine: features & classifier (pure math, no I/O)

**Rust reference:** `engine/features.rs`, `engine/classifier.rs`,
`engine/app_context.rs`, `engine/goal_alignment.rs`, `engine/focus_modes.rs`.

**Goal:** given a stream of `CaptureEvent`, produce a `FeatureVector`, then a
`PredictionScores` from the heuristic backend. No OS, no DB — 100% testable.

**Steps**
1. `FeatureExtractor::ingest/extract`: port the rolling 30s / 5min window math from
   `features.rs`. Keep the 31-feature order fixed (it's a contract).
2. `classify_app_context`: port the full keyword tables from `app_context.rs`.
3. `Classifier::predict_heuristic`: port the real thresholds from `classifier.rs`
   (the scaffold has placeholder numbers — replace them).
4. `goal_alignment` + `focus_modes` helpers: port verbatim.

**Tests:** this is the highest-value test surface. Port `../FocoFlow-1`'s
feature-parity fixtures (`fixtures/feature_parity/scenarios.json`): feed the same
event sequences, assert the C++ feature vector matches Rust within a tolerance.
That parity check is your proof the port is faithful.

**Done when:** feature-parity fixtures pass; heuristic classifier reproduces Rust
focus states on the fixtures.

**Learn:** Rust slice iterators / `.windows()` → manual index math; floating-point
determinism across languages (why you assert within an epsilon).

---

## Phase 4 — Capture (the hard, OS-specific part)

**Rust reference:** `capture/mod.rs`, `capture/thread.rs`,
`capture/active_window.rs`, `capture/permissions.rs` (Rust got these from `rdev` +
`active-win-pos-rs`; you hand-write them).

**Goal:** real keyboard/mouse events flowing from an OS hook, through the SPSC ring
buffer, drained by a consumer — with drop-counting.

**Steps (Windows first, it's this machine)**
1. `input_hook_windows.cpp`: finish the low-level hooks. Enrich each event with the
   active window (call `query_active_window()`), compute `mouse_speed`, and fill
   timestamps. **Keep the callback fast** — it runs inside the global input queue.
2. `active_window.cpp` (Windows): finish `OpenProcess` + `GetModuleBaseNameW`, and
   UTF-16→UTF-8 conversion so strings match Rust's `String`.
3. `permissions_*.cpp`: port the probe logic. On Windows it's mostly trivially
   available; the real work is macOS (Accessibility + Input Monitoring).
4. `CaptureThread`: already wired to push→ring buffer and count drops. Verify the
   producer/consumer split under a stress test.
5. Defer macOS (`CGEventTap` + `CFRunLoop`) and Linux (XRecord/evdev) to their own
   sub-phases; stub them so the build stays green cross-platform.

**Tests:** unit-test the ring buffer hard (fill it, assert a drop is counted; SPSC
correctness). The hooks themselves you verify by running and watching event counts —
note this is where you *manually verify*, since tests can't inject global input.

**Done when:** running the app (even without UI) logs real key/mouse events and a
stall/health check reports capture running.

**Learn:** the biggest C++ vs Rust gap. Rust's `Send`/`Sync` + the borrow checker
made the producer/consumer handoff safe by construction; here you uphold it manually
with atomics + memory ordering. This is the phase to be paranoid.

---

## Phase 5 — App state & the engine tick loop

**Rust reference:** `state.rs`.

**Goal:** tie capture → features → classifier → snapback tracker → storage together
on a periodic tick, holding `latest_prediction` and session state.

**Steps**
1. `AppState::start_engine`: start `CaptureThread`, spawn the engine thread.
2. `engine_tick` (~1/sec, match Rust): drain events into `FeatureExtractor`, extract,
   classify, feed the `SnapbackTracker`, persist prediction + feature snapshot,
   update `latest_prediction_`, and (Phase 6) emit to the frontend.
3. Session commands (`start_session`/`stop_session`/`submit_label`/`health`): guard
   shared state with `mutex_`. This is Tauri's managed-state + Mutex pattern in std.
4. Port `SnapbackTracker::observe` transitions from `snapback/tracker.rs`.

**Tests:** drive the tick loop with a synthetic event stream (no OS), assert a
session's `recap()` and that a distraction→return produces a `SnapbackPayload`.

**Done when:** a headless run produces predictions and snapback payloads from
synthetic input, persisted to SQLite.

**Learn:** `Arc<Mutex<T>>` + Tauri `.manage()` → a shared object + `std::mutex`;
lock discipline (hold the lock as briefly as possible; never across a callback).

---

## Phase 6 — Webview UI + IPC bridge (the Tauri replacement)

**Rust reference:** `commands.rs`, `events.rs`, `lib.rs` (the `generate_handler!`
list + setup closure).

**Goal:** the reused React app runs in a system webview and talks to the C++ core.

**Steps**
1. `main.cpp`: create the `webview::webview`, size/title it, `register_commands`,
   navigate to the Vite dev server (`http://localhost:5173`).
2. Inject the IPC shim via `webview.init(...)`: define `window.__TAURI__`-compatible
   `invoke(name, args)` that calls the bound C++ functions, plus
   `window.__snapback.emit` for host→frontend events. See `frontend/README.md`.
3. Fill in every `w.bind(...)` handler in `commands.hpp`: parse the JSON arg string,
   call `AppState`, serialize the result. **Names must match** the frontend +
   the Rust handler list exactly.
4. `events::emit_or_log` → the `emit()` helper: on each tick, push the new
   `PredictionRecord` (and snapback payloads) to the frontend.
5. Dev workflow: run `../FocoFlow-1/frontend`'s Vite server, then launch the C++ app.

**Tests:** command handlers are thin — unit-test the `AppState` methods they call
(done in Phase 5). Verify the wired UI by **running it** and watching the dashboard
update (this is the `verify` step, not a unit test).

**Done when:** the React dashboard shows live focus state driven by your real input,
sessions start/stop from the UI, and labels persist.

**Learn:** Tauri's auto-generated `invoke` glue vs. hand-registering `bind`s; why the
three-way name contract (frontend ↔ bridge ↔ core) is the thing that breaks silently.

---

## Phase 7 — ONNX inference (optional backend)

**Rust reference:** `engine/onnx_model.rs` (behind `--features onnx`).

**Goal:** load `model.onnx` and run the 31-feature vector through ONNX Runtime,
falling back to the heuristic when absent — exactly the Rust policy.

**Steps**
1. Build with `-DSNAPBACK_ONNX=ON`, point CMake at `third_party/onnxruntime/`.
2. `OnnxModel::init`: build `Ort::Env` + `Ort::Session` from the model path; cache
   input/output names.
3. `OnnxModel::run`: make an `Ort::Value` tensor `[1, 32]` from `FeatureVector`,
   `session.Run`, map outputs → `PredictionScores`.
4. `Classifier` already picks ONNX when `loaded()`, else heuristic — verify the
   fallback.

**Tests:** run a tiny known model on a fixed feature vector, assert the output
matches the Rust ONNX path (port a fixture from `../FocoFlow-1`).

**Done when:** with a model present, `get_health` reports `backend: "onnx"` and
predictions come from the model; without one, it says `heuristic`.

**Learn:** why the C++ ONNX API is *nicer* than Rust's `ort` crate — first-class,
no wrapper crate; tensor lifetime management with `Ort::Value`.

---

## Phase 8 — Snapback overlay, tray, permissions polish

**Rust reference:** `snapback/overlay.rs`, `tray.rs`, `capture/permissions.rs`.

**Goal:** the namesake overlay window + system tray + a real permissions flow.

**Steps**
1. `Overlay`: simplest is a second borderless `webview` loading the reused
   `snapback.html`; nicer is a native always-on-top layered window. Show on the
   tracker's return-from-distraction edge; auto-dismiss.
2. Tray: per-OS (Windows `Shell_NotifyIcon`, macOS `NSStatusItem`, Linux
   `libappindicator`). No free abstraction — this is real per-OS work.
3. Permissions UX: the `refresh_permissions` command + the macOS Accessibility /
   Input Monitoring prompts.

**Done when:** returning from a distraction pops the "here's where you left off"
card; the tray menu works; macOS permission flow matches the Rust README steps.

**Learn:** how much Tauri's tray/window abstractions were doing for free.

---

## Phase 9 — Packaging, CI, parity sign-off

**Rust reference:** `../FocoFlow-1/.github/workflows/`, `docs/DEPLOYMENT.md`.

**Goal:** a shippable binary + a CI that guards the port.

**Steps**
1. Installer: WiX/NSIS (Windows), an app bundle + DMG (macOS). Tauri did this;
   you script it.
2. Embed the built frontend `dist/` in the binary (custom scheme / data URLs).
3. CI: build + `ctest` on Windows/macOS/Linux; run the **feature-parity fixtures**
   against both Rust and C++ so drift is caught automatically.
4. Auto-update: decide whether to port it (Tauri's updater) or drop it for v1.

**Done when:** a signed installer produces a working app on a clean machine, and CI
is green including parity.

**Learn:** everything in the "What you lose" section of ARCHITECTURE.md, made real.

---

## Ordering rationale (why this sequence)

- **Pure/testable first** (types → storage → engine): builds confidence and a test
  net before touching anything OS-specific or async.
- **Capture in the middle:** the riskiest code, tackled once you have a stable core
  to plug it into.
- **UI after the core works:** the frontend is reused, so it's integration, not
  invention — do it once there's something real to show.
- **ONNX + polish + packaging last:** optional or non-load-bearing until the app
  actually works end-to-end.

## Definition of "done" for the whole rewrite

The C++ app reaches feature parity with `../FocoFlow-1` v0.2: live focus states, the
snapback overlay, focus modes, one-tap labels, session recap, SQLite persistence,
optional ONNX — and the **feature-parity fixtures pass against both codebases**.
