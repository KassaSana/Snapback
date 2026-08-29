import { memo } from "react";

import { summarizePermissions } from "./healthHints";
import { settingsHealthBadge, type SettingsSection } from "./settingsSections";
import { RecordingStatusCard } from "./RecordingStatusCard";
import type { RecordingStatus } from "./api";

type AppHeaderProps = {
  activeWindowAvailable: boolean;
  captureFailed: boolean;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  healthStatus: string;
  /** Roadmap 10.9. The one model fact that is a failure rather than configuration. */
  modelDeploymentDegraded: boolean;
  permissionCaptureAvailable: boolean;
  permissionMessage: string | null;
  permissionSteps: string[];
  /** Opens the Settings section holding the technical details behind the badge. */
  onOpenTechnicalDetails: (section: SettingsSection) => void;
  recordingStatus: RecordingStatus;
  onPauseRecording: (minutes: number) => void | Promise<void>;
  onResumeRecording: () => void | Promise<void>;
  onResumeAlerts: () => void | Promise<void>;
};

export const AppHeader = memo(function AppHeader({
  activeWindowAvailable,
  captureFailed,
  captureProbeConfirmed,
  captureRunning,
  healthStatus,
  modelDeploymentDegraded,
  permissionCaptureAvailable,
  permissionMessage,
  permissionSteps,
  onOpenTechnicalDetails,
  recordingStatus,
  onPauseRecording,
  onResumeRecording,
  onResumeAlerts,
}: AppHeaderProps) {
  const permissionHealth = summarizePermissions({
    captureAvailable: permissionCaptureAvailable,
    captureFailed,
    captureProbeConfirmed,
    captureRunning,
    activeWindowAvailable,
    message: permissionMessage ?? "",
    setupSteps: permissionSteps,
  });

  // Roadmap 10.9. Classifier backend, model file, and training quality used to sit here
  // permanently — three engineering fields on the first screen of a focus tool. They are now
  // one badge that says whether anything needs attention, and a link to the section where the
  // detail lives. That is ordering the information, not hiding it.
  //
  // `permissionHealth.label` is deliberately *not* the input here. It collapses to "blocked"
  // for a failed capture listener as well as a refused OS permission, and those are different
  // problems with different fixes — one is a settings dialog, the other is a restart. Reading
  // the two causes separately is what keeps both badge labels reachable. The `checking` guard
  // stops a not-yet-loaded health payload from being reported as a refusal.
  const badge = settingsHealthBadge({
    permissionBlocked: !permissionCaptureAvailable && healthStatus !== "checking",
    captureFailed,
    modelFailed: modelDeploymentDegraded,
  });

  const degraded = badge.warning;

  return (
    <header className="app-header">
      <div>
        <p className="eyebrow">Snapback</p>
        <h1>What are you working on?</h1>
        <p className="subtitle">Name a goal and start.</p>
      </div>
      <div className="status-stack">
        <RecordingStatusCard
          variant="header"
          status={recordingStatus}
          onPause={onPauseRecording}
          onResume={onResumeRecording}
          onResumeAlerts={onResumeAlerts}
        />
        {degraded ? (
          <>
            <div className="status-pill">
              <span className="status-label">App</span>
              <span className={`status-value${healthStatus === "online" ? "" : " status-alert"}`}>
                {healthStatus}
              </span>
            </div>
            <div className="status-pill">
              <span className="status-label">Capture</span>
              <span className={`status-value${captureFailed ? " status-alert" : ""}`}>
                {captureFailed ? "failed" : captureRunning ? "running" : "idle"}
              </span>
            </div>
            <div className="status-pill status-pill-stack">
              <span className="status-label">Permissions</span>
              <span
                className={`status-value${permissionHealth.label === "blocked" ? " status-alert" : ""}`}
              >
                {permissionHealth.label}
              </span>
              <span className="status-detail">{permissionHealth.detail}</span>
            </div>
            <div className="status-pill status-pill-stack">
              <span className="status-label">System</span>
              <span className={`status-value${badge.warning ? " status-alert" : ""}`}>
                {badge.label}
              </span>
              <button
                type="button"
                className="link-button"
                onClick={() => onOpenTechnicalDetails(badge.section)}
              >
                Technical details
              </button>
            </div>
          </>
        ) : (
          <button
            type="button"
            className="link-button"
            onClick={() => onOpenTechnicalDetails(badge.section)}
          >
            Technical details
          </button>
        )}
      </div>
    </header>
  );
});
