import { memo } from "react";

import type { RecordingStatus } from "./api";
import { snoozeRemainingLabel } from "./alertDelivery";

// Roadmap 2.10. For software that reads window titles, "am I recording right now?" should
// never require navigation — so this states the answer plainly and offers the pause beside it.
//
// The five states come from the backend already decided. Deriving them here from health plus
// settings is exactly what the item forbids: the tray would then compute the same question
// separately, and the two could disagree.
type RecordingStatusCardProps = {
  status: RecordingStatus;
  onPause: (minutes: number) => void | Promise<void>;
  onResume: () => void | Promise<void>;
  /** Roadmap 2.16. Ends an alert snooze started from the tray. */
  onResumeAlerts: () => void | Promise<void>;
};

const LABELS: Record<RecordingStatus["state"], string> = {
  recording: "Recording",
  pausedIdle: "Paused for idle",
  pausedPrivate: "Paused privately",
  noSession: "No session",
  blocked: "Blocked",
};

const DETAIL: Record<RecordingStatus["state"], string> = {
  recording: "Window titles are being captured for the running session.",
  pausedIdle: "You are away, so nothing is being captured or counted.",
  pausedPrivate: "Nothing is captured while private mode is on.",
  noSession: "Nothing is being recorded until you start a session.",
  blocked: "Capture cannot run. Check permissions in Settings.",
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
}: RecordingStatusCardProps) {
  const paused = status.state === "pausedPrivate";
  const snoozed = status.alertSnoozeRemainingMs > 0;

  return (
    <section className="card recording-status-card">
      <div className="card-header">
        <h2>Recording status</h2>
        <span className="pill">{LABELS[status.state]}</span>
      </div>

      <p className="helper-text">{DETAIL[status.state]}</p>

      {paused && status.privatePauseRemainingMs > 0 ? (
        <p className="meta-sub">
          {formatRemaining(status.privatePauseRemainingMs)} — recording resumes on its own.
        </p>
      ) : null}

      {/*
        Roadmap 2.16. Stated as its own line, under a status that still says Recording. A
        snooze silences interventions; it does not stop capture, and the one place a user
        checks "am I being recorded?" must not blur the two. The countdown lives here rather
        than in the tray menu because this is a number that changes.
      */}
      {snoozed ? (
        <p className="meta-sub">
          Alerts snoozed — {snoozeRemainingLabel(status.alertSnoozeRemainingMs)}. Recording
          continues.{" "}
          <button type="button" className="link-button" onClick={() => void onResumeAlerts()}>
            Resume alerts
          </button>
        </p>
      ) : null}

      {paused ? (
        <button className="primary-button" onClick={() => void onResume()}>
          Resume recording
        </button>
      ) : (
        <div className="button-row">
          <button className="secondary-button" onClick={() => void onPause(0)}>
            Pause until I resume
          </button>
          {PAUSE_CHOICES.map((minutes) => (
            <button
              key={minutes}
              className="secondary-button"
              onClick={() => void onPause(minutes)}
            >
              Pause {minutes}m
            </button>
          ))}
        </div>
      )}
    </section>
  );
});
