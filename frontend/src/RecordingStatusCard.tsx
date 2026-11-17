import { memo } from "react";

import type { RecordingStatus } from "./api";
import { snoozeRemainingLabel } from "./alertDelivery";
import type { SessionStatusView } from "./sessionStatus";

// The header's "am I being recorded?" chip, with pause and resume beside it. The state comes
// from the backend, so this and the tray can't disagree.
type RecordingStatusCardProps = {
  status: RecordingStatus;
  onPause: (minutes: number) => void | Promise<void>;
  onResume: () => void | Promise<void>;
  /** Ends an alert snooze started from the tray. */
  onResumeAlerts: () => void | Promise<void>;
  /** The last refresh failed, so `status` is the last known state rather than the current one. */
  unconfirmed?: boolean;
  /** While a session runs, the chip uses the session's status wording. */
  sessionStatus?: SessionStatusView | null;
};

const RECORDING_STATE_LABELS: Record<RecordingStatus["state"], string> = {
  recording: "Recording",
  pausedIdle: "Paused (away)",
  pausedPrivate: "Paused (private)",
  noSession: "Not recording",
  blocked: "Blocked",
};

const formatRemaining = (ms: number): string => {
  const totalMinutes = Math.ceil(ms / 60000);
  if (totalMinutes >= 60) {
    const hours = Math.floor(totalMinutes / 60);
    return `${hours}h ${totalMinutes % 60}m left`;
  }
  return `${totalMinutes}m left`;
};

const PAUSE_CHOICES = [15, 30, 60];

export const RecordingStatusCard = memo(function RecordingStatusCard({
  status,
  onPause,
  onResume,
  onResumeAlerts,
  unconfirmed = false,
  sessionStatus = null,
}: RecordingStatusCardProps) {
  const paused = status.state === "pausedPrivate";
  const snoozed = status.alertSnoozeRemainingMs > 0;
  const canPause = status.state === "recording";
  const label = sessionStatus?.label ?? RECORDING_STATE_LABELS[status.state];
  const reason =
    sessionStatus?.reason ??
    (status.state === "pausedPrivate" || status.state === "pausedIdle"
      ? "Not counting toward attended time."
      : null);

  return (
    <section className="recording-status-header" aria-labelledby="recording-status-heading">
      <h2 id="recording-status-heading">Session status</h2>
      <span className={`status-value${status.state === "blocked" ? " status-alert" : ""}`}>
        {label}
      </span>
      {reason ? <p className="meta-sub">{reason}</p> : null}
      {unconfirmed ? (
        <p className="meta-sub status-alert">
          Couldn&apos;t reach the app. Showing the last known state.
        </p>
      ) : null}
      {paused && status.privatePauseRemainingMs > 0 ? (
        <p className="meta-sub">
          {formatRemaining(status.privatePauseRemainingMs)}, then recording resumes.
        </p>
      ) : null}
      {snoozed ? (
        <p className="meta-sub">
          Alerts snoozed, {snoozeRemainingLabel(status.alertSnoozeRemainingMs)}. Recording
          continues.{" "}
          <button type="button" className="link-button" onClick={() => void onResumeAlerts()}>
            Resume alerts
          </button>
        </p>
      ) : null}
      {paused ? (
        <button className="link-button" onClick={() => void onResume()}>
          Resume recording
        </button>
      ) : canPause ? (
        <div className="button-row">
          <button className="link-button" onClick={() => void onPause(0)}>
            Pause recording until I resume
          </button>
          <details className="recording-pause-menu">
            <summary>Pause recording for…</summary>
            {PAUSE_CHOICES.map((minutes) => (
              <button
                key={minutes}
                type="button"
                className="link-button"
                onClick={() => void onPause(minutes)}
              >
                Pause recording {minutes}m
              </button>
            ))}
          </details>
        </div>
      ) : null}
    </section>
  );
});
