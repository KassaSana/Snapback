import { memo } from "react";

import type { AutostartStatus } from "./api";
import type { AppearanceMode } from "./appearance";
import {
  ALERT_CHANNEL_LABELS,
  ALERT_CHANNELS,
  ALERT_EVENT_LABELS,
  ALERT_EVENTS,
  formatTimeOfDay,
  parseTimeOfDay,
  quietHoursSummary,
  routingSummary,
  type AlertChannel,
  type AlertDeliverySettings,
  type AlertEventKey,
} from "./alertDelivery";
import {
  formatIdleThreshold,
  idleThresholdHelperText,
  IDLE_THRESHOLD_CHOICES,
} from "./idleThreshold";

type SettingsCardProps = {
  appearanceMode: AppearanceMode;
  onAppearanceChange: (mode: AppearanceMode) => void;
  busy: boolean;
  error: string | null;
  onAutostartChange: (enabled: boolean) => void;
  status: AutostartStatus | null;
  /** Roadmap 7.23. Seconds of no input before attended time pauses. */
  idleThresholdSecs: number;
  idleThresholdBusy: boolean;
  idleThresholdError: string | null;
  onIdleThresholdChange: (seconds: number) => void;
  /** Roadmap 2.16. When and how an interruption may reach the user. */
  alerts: AlertDeliverySettings;
  alertsBusy: boolean;
  alertsError: string | null;
  onAlertsChange: (alerts: AlertDeliverySettings) => void;
};

export const SettingsCard = memo(function SettingsCard({
  appearanceMode,
  onAppearanceChange,
  busy,
  error,
  onAutostartChange,
  status,
  idleThresholdSecs,
  idleThresholdBusy,
  idleThresholdError,
  onIdleThresholdChange,
  alerts,
  alertsBusy,
  alertsError,
  onAlertsChange,
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
      <label className="toggle-row">
        <span>
          <strong>Appearance</strong>
          <span className="helper-text">Match the system, or choose light or dark.</span>
        </span>
        <select
          aria-label="Appearance"
          value={appearanceMode}
          onChange={(event) => onAppearanceChange(event.target.value as AppearanceMode)}
        >
          <option value="system">System</option>
          <option value="light">Light</option>
          <option value="dark">Dark</option>
        </select>
      </label>
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
      {/*
        Roadmap 2.16. Delivery lives under Settings > General, not Privacy. Putting "silence
        alerts" beside "stop recording" is exactly the confusion the item forbids: one changes
        whether you are interrupted, the other changes whether anything is recorded at all.
      */}
      <h3 className="settings-subheading">Interruptions</h3>
      {ALERT_EVENTS.map((event: AlertEventKey) => (
        <div className="toggle-row" key={event}>
          <span>
            <strong>{ALERT_EVENT_LABELS[event]}</strong>
            <span className="helper-text">{routingSummary(alerts[event])}</span>
          </span>
          <span className="channel-choices">
            {ALERT_CHANNELS.map((channel: AlertChannel) => (
              <label key={channel} className="channel-choice">
                <input
                  type="checkbox"
                  checked={alerts[event].includes(channel)}
                  disabled={alertsBusy}
                  onChange={(changed) => {
                    const next = changed.target.checked
                      ? [...alerts[event], channel]
                      : alerts[event].filter((c) => c !== channel);
                    onAlertsChange({ ...alerts, [event]: next });
                  }}
                />
                <span>{ALERT_CHANNEL_LABELS[channel]}</span>
              </label>
            ))}
          </span>
        </div>
      ))}
      <label className="toggle-row">
        <span>
          <strong>Quiet hours</strong>
          <span className="helper-text">{quietHoursSummary(alerts)}</span>
        </span>
        <input
          type="checkbox"
          checked={alerts.quietHoursEnabled}
          disabled={alertsBusy}
          onChange={(event) =>
            onAlertsChange({ ...alerts, quietHoursEnabled: event.target.checked })
          }
        />
      </label>
      {alerts.quietHoursEnabled ? (
        <div className="toggle-row">
          <span>
            <strong>Silent between</strong>
            <span className="helper-text">Local time. A range may cross midnight.</span>
          </span>
          <span className="quiet-hours-range">
            <input
              type="time"
              aria-label="Quiet hours start"
              value={formatTimeOfDay(alerts.quietHoursStartMin)}
              disabled={alertsBusy}
              onChange={(event) => {
                const minute = parseTimeOfDay(event.target.value);
                if (minute !== null) onAlertsChange({ ...alerts, quietHoursStartMin: minute });
              }}
            />
            <span>to</span>
            <input
              type="time"
              aria-label="Quiet hours end"
              value={formatTimeOfDay(alerts.quietHoursEndMin)}
              disabled={alertsBusy}
              onChange={(event) => {
                const minute = parseTimeOfDay(event.target.value);
                if (minute !== null) onAlertsChange({ ...alerts, quietHoursEndMin: minute });
              }}
            />
          </span>
        </div>
      ) : null}
      <label className="toggle-row">
        <span>
          <strong>Hide details on the lock screen</strong>
          <span className="helper-text">
            System notifications say only that something happened. What you were working on
            stays out of notification history.
          </span>
        </span>
        <input
          type="checkbox"
          checked={alerts.preview === "generic"}
          disabled={alertsBusy}
          onChange={(event) =>
            onAlertsChange({ ...alerts, preview: event.target.checked ? "generic" : "detailed" })
          }
        />
      </label>
      {alertsError ? <p className="helper-text alert">{alertsError}</p> : null}
      {!supported ? (
        // Roadmap 3.0 added the macOS launchd backend, so this line named the wrong platforms
        // the moment that landed. It is driven by `supported` from the native side rather than
        // by a hardcoded OS check, so the only thing to keep true is the sentence itself.
        <p className="helper-text">Start-on-login is available on Windows and macOS.</p>
      ) : null}
    </section>
  );
});
