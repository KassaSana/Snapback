// Host->frontend event names. Every emit site uses a constant from here, and `kAll` is what the
// IPC contract test compares against fixtures/ipc_commands.json (which also rejects raw string
// literals at emit sites).
//
// Adding an event: add the constant, add it to `kAll`, add it to the fixture's
// `events.emitted`, and add the frontend `listen(...)`.
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
