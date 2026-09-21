import { memo } from "react";

import type { RecordingStatus } from "./api";
import { snoozeRemainingLabel } from "./alertDelivery";

// Roadmap 2.10. For software that reads window titles, "am I recording right now?" should
// never require navigation — so this states the answer plainly and offers the pause beside it.
//
// The five states come from the backend already decided. Deriving them here from health plus
// settings is exactly what the item forbids: the tray would then compute the same question
// separately, and the two could disagree.
//
// The header variant is the same answer in the chrome (ADR-0003's Now cockpit): a full card
// on idle Now was four Pause buttons under "nothing is being recorded".
type RecordingStatusCardProps = {
  status: RecordingStatus;
  onPause: (minutes: number) => void | Promise<void>;
  onResume: () => void | Promise<void>;
  /** Roadmap 2.16. Ends an alert snooze started from the tray. */
  onResumeAlerts: () => void | Promise<void>;
  /**
   * The last attempt to confirm this with the app failed, so `status` is the last answer we
   * had rather than the current one. Said out loud: a state this surface cannot vouch for is
   * still shown, but not as a fact.
   */
  unconfirmed?: boolean;
  /** Compact chrome for the app header. Same commands, no card chrome. */
  variant?: "card" | "header";
};

export const RECORDING_STATE_LABELS: Record<RecordingStatus["state"], string> = {
  recording: "Recording",
  pausedIdle: "Paused for idle",
  pausedPrivate: "Paused privately",
  noSession: "Not recording",
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
  unconfirmed = false,
  variant = "card",
}: RecordingStatusCardProps) {
  const paused = status.state === "pausedPrivate";
  const snoozed = status.alertSnoozeRemainingMs > 0;
  const canPause = status.state === "recording";
  const header = variant === "header";

  const remaining =
    paused && status.privatePauseRemainingMs > 0 ? (
      <p className="meta-sub">
        {formatRemaining(status.privatePauseRemainingMs)} — recording resumes on its own.
      </p>
    ) : null;

  const unconfirmedLine = unconfirmed ? (
    <p className="meta-sub status-alert">
      Could not confirm with the app — showing the last known state.
    </p>
  ) : null;

  const snoozeLine = snoozed ? (
    <p className="meta-sub">
      Alerts snoozed — {snoozeRemainingLabel(status.alertSnoozeRemainingMs)}. Recording
      continues.{" "}
      <button type="button" className="link-button" onClick={() => void onResumeAlerts()}>
        Resume alerts
      </button>
    </p>
  ) : null;

  const actions = paused ? (
    <button className={header ? "link-button" : "primary-button"} onClick={() => void onResume()}>
      Resume recording
    </button>
  ) : canPause ? (
    <div className="button-row">
      <button
        className={header ? "link-button" : "secondary-button"}
        onClick={() => void onPause(0)}
      >
        Pause until I resume
      </button>
      {header ? (
        <details className="recording-pause-menu">
          <summary>Pause for…</summary>
          {PAUSE_CHOICES.map((minutes) => (
            <button
              key={minutes}
              type="button"
              className="link-button"
              onClick={() => void onPause(minutes)}
            >
              Pause {minutes}m
            </button>
          ))}
        </details>
      ) : (
        <div className="pause-choices">
          <span className="meta-label" id="recording-pause-choices-label">
            Pause for a while
          </span>
          <div className="button-row" role="group" aria-labelledby="recording-pause-choices-label">
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
        </div>
      )}
    </div>
  ) : null;

  if (header) {
    return (
      <section className="recording-status-header" aria-labelledby="recording-status-heading">
        <h2 id="recording-status-heading">Recording status</h2>
        <span className={`status-value${status.state === "blocked" ? " status-alert" : ""}`}>
          {RECORDING_STATE_LABELS[status.state]}
        </span>
        {unconfirmedLine}
        {remaining}
        {snoozeLine}
        {actions}
      </section>
    );
  }

  return (
    <section className="card recording-status-card">
      <div className="card-header">
        <h2>Recording status</h2>
        <span className="pill">{RECORDING_STATE_LABELS[status.state]}</span>
      </div>

      <p className="helper-text">{DETAIL[status.state]}</p>
      {unconfirmedLine}
      {remaining}
      {snoozeLine}
      {actions}
    </section>
  );
});
