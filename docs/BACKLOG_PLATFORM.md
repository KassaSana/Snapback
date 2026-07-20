# BACKLOG_PLATFORM.md — platform, capture, and shipping backlog

**Staging doc, not the source of truth.** [ROADMAP.md](ROADMAP.md) is still the live backlog.
This file exists because two agents are working the repo concurrently and `ROADMAP.md` is
owned by the other lane right now. Everything here is scoped to the **platform lane**:
`src/capture/`, `src/snapback/` (overlay + title parsing), `src/app/tray*`,
`src/app/autostart.*`, `src/main.cpp`, and the CMake app target / CI.

**Fold this into ROADMAP.md** once the concurrent feature work lands, then delete it.

Item format matches ROADMAP.md: effort is `S` (a sitting), `M` (a few sittings), `L` (a
mini-project). Every claim below cites the code it came from so nothing has to be re-derived.

Found during a full platform audit on **2026-07-20**. Baseline at audit time: 132/132
tests green, clean build.

---

## Tier P0 — The desktop app does not build outside Windows

This tier is first because it invalidates a status claim in `CLAUDE.md`, and because P0.2
is the reason every other item here went unnoticed.

- **P0.1 — Provide no-op `Overlay` / `Tray` fallbacks.** `S` — ✅ **done** (`c0cfc3f`)

  Added `src/snapback/overlay_stub.cpp` + `src/app/tray_stub.cpp` behind
  `#if !defined(_WIN32)`, wired into the app target's `else()` branch. Verified: both
  compile clean and `nm` now reports `T snapback::Overlay::instance()` /
  `T snapback::Tray::instance()`.

  **Not yet verified: a full `snapback` link on macOS.** The build gets past the stubs and
  fails later in `state.cpp` on the concurrent goal-alignment work
  (`default_goal_categories` undeclared). Re-run the app build once that lands to confirm
  end-to-end, and note the two `.o` files above only prove the symbols exist, not that
  nothing else is missing.

  `Overlay::instance()` and `Tray::instance()` are defined in exactly two translation
  units — `src/snapback/overlay_windows.cpp:145` and `src/app/tray_windows.cpp:139` — and
  `CMakeLists.txt:172` adds both **only under `if(WIN32)`**. `src/main.cpp` calls them
  unconditionally at lines 122, 140, 162, 163, 180, and 189.

  So with `-DSNAPBACK_BUILD_APP=ON` on macOS or Linux, the link fails on undefined
  symbols. Verified: `nm` over every built static library finds **zero** definitions of
  either symbol.

  Both headers already promise the fix and it was never written:
  > `overlay.hpp:37` — *"instance() returns the per-platform implementation (a no-op where
  > unimplemented, so the build stays green cross-platform)."*
  >
  > `tray.hpp:25` — same claim.

  Add the documented no-op implementations behind `#if !defined(_WIN32)` so the app links
  everywhere, then real implementations land as P2.2 / P2.3.

- **P0.2 — Build the app target in CI.** `S` — ✅ **done** (`c0cfc3f`)

  Added the `desktop-app-build` job: macOS + Linux matrix, `SNAPBACK_BUILD_APP=ON`,
  builds the `snapback` target and never launches it. Linux installs
  `libgtk-3-dev` + `libwebkit2gtk-4.1-dev` (webview prefers `webkit2gtk-4.1`, falls back
  to `4.0`). **Unverified until the first CI run** — the Linux webview/webkit pairing could
  not be exercised locally on macOS.

  `grep -rn SNAPBACK_BUILD_APP .github/` returns **nothing**. The option defaults to `OFF`
  (`CMakeLists.txt:39`), so the three-OS CI matrix builds only the static libs and the
  headless tests. **The real desktop binary has never been built by CI on any OS.**

  That is why P0.1 could sit undetected: no automated build ever links `main.cpp`. Add
  `-DSNAPBACK_BUILD_APP=ON` to at least one job per OS. Expect to fix P0.1 first or CI
  goes red immediately — which is the point.

  *C++/Rust delta: `cargo build` had one target graph and built the app every time. Our
  opt-in CMake option silently carved the app out of CI's definition of "green."*

---

## Tier P1 — macOS native capture is written, wired, and broken

`ROADMAP.md` 0.3 describes native macOS capture as unstarted ("replace the polling
fallback with a real `CGEventTap`"), and `CLAUDE.md`'s table says macOS is "Polling only."

**Both are stale.** `src/capture/input_hook_macos.mm` is a complete `CGEventTap` +
`CFRunLoop` backend, wired at `CMakeLists.txt:113`. It does not survive contact with real
input. (It was missed by earlier audits because it is the repo's only `.mm` file and
`*.cpp`/`*.hpp` sweeps skip it.)

Correct 0.3's framing when folding this back: the work is **fix and test**, not **build**.
That likely moves it from `L` to `M`.

**P1.1–P1.5 are all fixed (`cc8bf15`, `0bc8242`) — but nobody has watched this run.** The
fixes compile and the headless suite is green under TSan, yet no test exercises a live
`CGEventTap`, and reproducing the original failure needs a real desktop session with
Accessibility granted. **Before marking ROADMAP 0.3 done, someone should run the app on
macOS and confirm keystrokes reach the engine and keep reaching it under sustained mouse
movement** — the old code died within seconds of the first mouse move. Until then this tier
is "fixed in code, unverified in the world."

- **P1.1 — Re-arm the event tap after a timeout.** `S` — ✅ **done** (`cc8bf15`)

  The disable branch now calls `CGEventTapEnable(self->tap_, true)` for both
  `kCGEventTapDisabledByTimeout` and `kCGEventTapDisabledByUserInput`.

  `input_hook_macos.mm:92` recognizes `kCGEventTapDisabledByTimeout` and then just
  `return event;`. macOS **disables the tap** when it sends that message; the only way
  back is `CGEventTapEnable(tap_, true)`, which is never called. Once it fires, macOS
  capture is dead for the remaining process lifetime, with no log line and no health-status
  change — `HealthStatus::capture_running` still reads `true` because the thread is alive.

- **P1.2 — Get `query_active_window()` out of the hook callback.** `M` — ✅ **done** (`cc8bf15`)

  The callback now reads a cached app/title. `run()` refreshes it every 500ms on the hook
  thread, matching the polling fallback's cadence. No lock needed: the tap source is
  attached to the hook thread's run loop, so the callback is delivered on that same thread.
  **This does not remove the `osascript` fork — it caps it at ~2/sec instead of ~100/sec.**
  P3.3 (native APIs) is the actual fix.

  `input_hook_macos.mm:101` calls `query_active_window()` **per event**. On macOS that is
  `active_window.cpp:74` — a `popen` that shells out to **`osascript`**. So every keystroke
  and every mouse-move forks a process and runs AppleScript inside the tap callback, at
  mouse-move rates (~100/sec).

  This is what makes P1.1 fire near-instantly rather than rarely, and it violates the
  standing rule in `CLAUDE.md`: *"keep the hook callback allocation-free."* The Windows
  hook honors it; this path does not.

  Fix: cache the foreground window, refresh it on a timer off the hot path, and have the
  callback read the cache.

- **P1.3 — `stop()` stops the wrong run loop.** `S` — ✅ **done** (`cc8bf15`)

  `run()` publishes its own `CFRunLoopRef` (retained) into an atomic; `stop()` reads that
  and stops it instead of `CFRunLoopGetCurrent()`.

  `input_hook_macos.mm:85` calls `CFRunLoopStop(CFRunLoopGetCurrent())`, but `stop()` runs
  on the **caller's** thread (`CaptureThread::stop()`), not the hook thread. Under the
  webview that is the app's main run loop. It appears to work only because `run()` polls
  with a 0.25s `CFRunLoopRunInMode` timeout (`:68`), so the `running_` flag check exits
  within 250ms regardless. Capture the hook thread's `CFRunLoopRef` in `run()` and stop
  that.

- **P1.4 — First tests for the capture layer.** `M` — ✅ **done** (`0bc8242`)

  `tests/test_capture_thread.cpp`: 5 tests over FIFO ordering, drop counting at the ring's
  `kCapacity - 1` boundary, the double-start guard, stop-without-start, and restart. Needed
  a seam — `CaptureThread::start()` now takes an optional `InputHook*` (defaults to the
  platform singleton) so a `ScriptedHook` can drive the producer side headlessly. Verified
  under TSan: 142/142 clean.

  **Still uncovered:** the OS backends themselves (`input_hook_macos.mm`, `_linux`,
  `_windows`) and `query_active_window`. P1.1–P1.3 are argued from CGEventTap semantics
  and remain unverified against a live tap — see the caveat below.

  `grep -rln "InputHook\|CaptureThread\|query_active_window" tests/` returns **nothing**.
  The layer CLAUDE.md calls out as *"where bugs will hide"* has zero coverage — which is
  exactly how P1.1–P1.3 shipped. Start with what is testable headlessly: `CaptureThread`
  push/drain/drop-counting against a fake `InputHook`, and the tap's event-type mapping.

- **P1.5 — Guard `CaptureThread::start()` against double-start.** `S` — ✅ **done** (`0bc8242`)

  `running_.exchange(true)` gates the thread assignment; `stop()` now stops the hook it
  actually started (not the singleton), so an injected fake is told to return and the join
  cannot hang.

  `capture_thread.cpp:5` assigns to `hook_thread_` with no check. A second `start()` without
  `stop()` assigns over a joinable `std::thread` → `std::terminate`. Currently unreachable
  only because `AppState::start_engine`'s CAS (`state.cpp:124`) happens to gate it — the
  safety lives in the caller, not the class. Fold into P1.4.

---

## Tier P2 — Cross-platform parity

- **P2.1 — Autostart on macOS and Linux.** `M`

  `autostart.cpp:77-81` is a hard-coded `return false` for every non-Windows platform, and
  `autostart_supported()` returns `false`, so the UI correctly greys out — the feature is
  simply absent. Needs a launchd `LaunchAgent` plist (`~/Library/LaunchAgents/`) on macOS
  and a systemd **user** unit on Linux. `ROADMAP.md` 1.3 notes these as follow-ups; this is
  the concrete scope.

- **P2.2 — macOS tray + native overlay.** `M` — *same as ROADMAP 3.1, unblocked by P0.1*

- **P2.3 — Linux tray + overlay.** `M` — *same as ROADMAP 3.2, unblocked by P0.1*

---

## Tier P3 — Correctness and quality in this lane

- **P3.1 — `title_parser` is a stub and mislabels non-editor windows.** `M`

  `title_parser.cpp:9` says so outright: *"This is a faithful port sketch; extend to match
  title_parser.rs."* It splits on `" — "` / `" - "` and calls segment one the file with no
  check that it looks like a filename. So `"Some Article - Google Chrome"` yields
  `file_hint = "Some Article"`, and `tracker.cpp:104` turns that into the summary
  **"Editing Some Article"** — and `tracker.cpp:136` into the snapback **"Return to Some
  Article."** A YouTube video reads as a file you were editing, in the product's namesake
  feature. No test catches it because the parser trivially "succeeds."

  Needs a real extension check plus per-app title conventions.

  **On the Rust source of truth:** it is missing *locally* — `../Snapback/src-tauri/` does
  not exist on this machine, which CLAUDE.md assumes it does. But it is not lost: the
  `feature-parity` CI job checks out `KassaSana/Snapback` at ref **`main-fresh`**, which
  still carries the Rust/Tauri layout (`.github/workflows/ci.yml:162-169`). Clone that ref
  before doing P3.1 so `title_parser.rs` can be ported faithfully rather than guessed at.

- **P3.2 — Fuzz `title_parser`.** `M` — *same as ROADMAP 4.2*

  Extra motivation from P3.1: the manual index math at `title_parser.cpp:18-22` is exactly
  the unchecked-arithmetic pattern Rust bounds-checked for free.

- **P3.3 — `active_window` shells out on every call.** `M`

  `active_window.cpp` drives a subprocess per query on both non-Windows platforms —
  `osascript` on macOS (`:74`), `xdotool` on Linux (`:87`). Beyond the P1.2 hot-path
  problem, it makes capture depend on external binaries at runtime and gives the user a
  visible process-spawn storm. Replace with native APIs: `CGWindowListCopyWindowInfo` /
  `NSWorkspace` on macOS, X11/`_NET_ACTIVE_WINDOW` on Linux.

---

## Handoff notes (other lane — do not fix from this lane)

- **Privacy exclusions skip normalization on load.** `state.cpp:398` matches with
  `app.find(lower_copy(exclusion))`. Exclusions are normalized on write
  (`normalize_privacy_exclusions`) but `from_json` in `types.cpp` does not normalize on
  read, so an empty string in `settings.json` makes `find("")` return `0` and **suppress
  every event** — a silent, total capture blackout with no user-visible cause. Belongs to
  whoever owns the privacy feature (ROADMAP 1.6).
