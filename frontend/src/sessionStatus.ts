import type { RecordingStatus, SessionRecord } from "./api";

/**
 * The label shown for the current session (ADR-0005): running or paused. A paused session
 * accrues no attended time, so the difference matters. `userIdle` is the engine's idle signal.
 */
export function sessionStatusLabel(
  record: SessionRecord | null,
  userIdle: boolean,
  recordingState?: RecordingStatus["state"],
): string {
  // Not "idle": that means the user is away, which is independent of having a session.
  if (!record) return "no session";

  // Anything already finished keeps its own status; only a live session can be paused.
  if (record.status !== "ACTIVE") return record.status.toLowerCase();

  if (recordingState === "pausedPrivate") return "recording paused — private";
  if (recordingState === "pausedIdle") return "recording paused — idle";
  return userIdle ? "paused" : "running";
}
