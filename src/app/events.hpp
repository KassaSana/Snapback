// Host->frontend event names. Roadmap 11.13.
//
// Commands have had a registry, a fixture, and a three-way contract test since 7.12; events
// had none of that, and four `listen(...)` calls in `frontend/src/api.ts` accumulated for
// names no native code ever emitted. Nothing could notice, because the emit sites were string
// literals scattered across three translation units and the only list of them was whatever a
// reader could grep.
//
// This header is that list. Every emit site spells its name from a constant here, `kAll` is
// the set the IPC contract test compares against `fixtures/ipc_commands.json`, and the test
// also refuses a raw string literal at an emit call site -- otherwise a new event could be
// emitted without ever appearing in `kAll`, which is the hole the constants alone would leave.
//
// Adding an event: add the constant, add it to `kAll`, add it to the fixture's `events.emitted`
// list, and add the `listen(...)` on the frontend. The contract test names whichever of those
// four you forgot.
#pragma once

#include <array>

namespace snapback::events {

// The engine tick's alert and state stream (`AppState::engine_tick` -> the emit hook).
inline constexpr const char* kIdle = "idle";
inline constexpr const char* kPrediction = "prediction";
inline constexpr const char* kSnapback = "snapback";
inline constexpr const char* kPomodoro = "pomodoro";
inline constexpr const char* kHyperfocus = "hyperfocus";
inline constexpr const char* kUntrackedWork = "untracked_work";

// Out-of-band pushes: a long-running command reporting progress, and the native side
// changing the answer to "am I being recorded?" (`AppState::emit_event`).
inline constexpr const char* kTrainingProgress = "training-progress";
inline constexpr const char* kRecordingStatus = "recording-status";

// Raised on the UI thread by `main.cpp` when a native alert is clicked, rather than by the
// engine, so it is not on the hook path above.
inline constexpr const char* kAlertAction = "alert_action";

// Every name this binary may emit. The IPC contract test pins the fixture to exactly this.
inline constexpr std::array<const char*, 9> kAll = {
    kIdle,      kPrediction,        kSnapback,          kPomodoro,     kHyperfocus,
    kUntrackedWork, kTrainingProgress, kRecordingStatus, kAlertAction,
};

}  // namespace snapback::events
