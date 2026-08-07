import { memo } from "react";

import type { AutostartStatus } from "./api";
import {
  formatIdleThreshold,
  idleThresholdHelperText,
  IDLE_THRESHOLD_CHOICES,
} from "./idleThreshold";

type SettingsCardProps = {
  busy: boolean;
  error: string | null;
  onAutostartChange: (enabled: boolean) => void;
  status: AutostartStatus | null;
  /** Roadmap 7.23. Seconds of no input before attended time pauses. */
  idleThresholdSecs: number;
  idleThresholdBusy: boolean;
  idleThresholdError: string | null;
  onIdleThresholdChange: (seconds: number) => void;
};

export const SettingsCard = memo(function SettingsCard({
  busy,
  error,
  onAutostartChange,
  status,
  idleThresholdSecs,
  idleThresholdBusy,
  idleThresholdError,
  onIdleThresholdChange,
}: SettingsCardProps) {
  const supported = status?.supported ?? false;
  // A saved value outside the offered choices is still the truth about this install, so it is
  // shown as its own option rather than silently snapping the dropdown to a value the user
  // never picked.
  const choices = IDLE_THRESHOLD_CHOICES.includes(
    idleThresholdSecs as (typeof IDLE_THRESHOLD_CHOICES)[number],
  )
    ? [...IDLE_THRESHOLD_CHOICES]
    : [...IDLE_THRESHOLD_CHOICES, idleThresholdSecs].sort((a, b) => a - b);

  return (
    <section className="card config-card">
      <div className="card-header">
        <h2>Settings</h2>
        <span className="pill">{supported ? "available" : "platform default"}</span>
      </div>
      <label className={`toggle-row${!supported ? " toggle-row-disabled" : ""}`}>
        <span>
          <strong>Start on login</strong>
          <span className="helper-text">Launch Snapback when you sign in.</span>
        </span>
        <input
          type="checkbox"
          checked={status?.enabled ?? false}
          disabled={!supported || busy || status === null}
          onChange={(event) => void onAutostartChange(event.target.checked)}
        />
      </label>
      {error ? <p className="helper-text alert">{error}</p> : null}
      <label className="toggle-row">
        <span>
          <strong>Count me as away after</strong>
          <span className="helper-text">{idleThresholdHelperText(idleThresholdSecs)}</span>
        </span>
        <select
          aria-label="Count me as away after"
          value={idleThresholdSecs}
          disabled={idleThresholdBusy}
          onChange={(event) => void onIdleThresholdChange(Number(event.target.value))}
        >
          {choices.map((seconds) => (
            <option key={seconds} value={seconds}>
              {formatIdleThreshold(seconds)}
            </option>
          ))}
        </select>
      </label>
      {idleThresholdError ? (
        <p className="helper-text alert">{idleThresholdError}</p>
      ) : null}
      {!supported ? (
        // Roadmap 3.0 added the macOS launchd backend, so this line named the wrong platforms
        // the moment that landed. It is driven by `supported` from the native side rather than
        // by a hardcoded OS check, so the only thing to keep true is the sentence itself.
        <p className="helper-text">Start-on-login is available on Windows and macOS.</p>
      ) : null}
    </section>
  );
});
