// How much CPU this process has burned, for the idle-cost figure.
//
// ROADMAP 14.11 asks for "idle CPU and wakeups per second with no session". Wakeups are a
// counter the engine keeps; CPU time is not ours to count, so it is asked of the OS. Both
// halves are needed to say anything useful: the engine wakes a fixed ten times a second
// whether or not anything is happening (`kEngineTickIntervalMs`), so the wakeup count alone
// is a constant. What it costs to wake that often is the number worth publishing.
//
// User + kernel time, summed, for the whole process rather than one thread: the engine tick,
// the capture hook thread and whatever the webview does are all part of what a user's battery
// pays for, and attributing it per thread would invite optimizing the wrong one.
#pragma once

#include <cstdint>

namespace snapback {

// Milliseconds of CPU consumed since the process started, or 0 where the platform call
// fails. Zero is deliberately not an error signal -- a diagnostic that threw, or that forced
// every caller to unwrap an optional, would be worse than a figure that reads as "unknown"
// and is obviously wrong beside a non-zero wall time.
std::uint64_t process_cpu_ms();

}  // namespace snapback
