import type { RecordingStatus, SessionRecord } from "./api";

export type SessionStatusView = {
  label: string;
  /** One-line reason; null when the label is enough on its own. */
  reason: string | null;
};

/**
 * Single vocabulary for the live session (ADR-0005). Elapsed and attended stay distinct:
 * pauses stop attended/recording while elapsed keeps running. Both Session Control and the
 * header chrome must read from this model so they cannot contradict each other.
 */
export function sessionStatusView(
  record: SessionRecord | null,
  userIdle: boolean,
  recordingState?: RecordingStatus["state"],
): SessionStatusView {
  // Not "idle": that means the user is away, which is independent of having a session.
  if (!record) return { label: "No session", reason: null };

  // Anything already finished keeps its own status; only a live session can be paused.
  if (record.status !== "ACTIVE") {
    const status = record.status.toLowerCase();
    return { label: status.charAt(0).toUpperCase() + status.slice(1), reason: null };
  }

  const paused = "Not counting toward attended time.";
  if (recordingState === "pausedPrivate") return { label: "Paused (private)", reason: paused };
  if (recordingState === "pausedIdle") return { label: "Paused (away)", reason: paused };
  if (userIdle) return { label: "Paused (no input)", reason: paused };
  return { label: "Running", reason: null };
}

/** Label-only helper for callers that have not moved to the reason yet. */
export function sessionStatusLabel(
  record: SessionRecord | null,
  userIdle: boolean,
  recordingState?: RecordingStatus["state"],
): string {
  return sessionStatusView(record, userIdle, recordingState).label;
}
