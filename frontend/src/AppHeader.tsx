import { memo } from "react";

import { summarizePermissions } from "./healthHints";
import { settingsHealthBadge, type SettingsSection } from "./settingsSections";
import { RecordingStatusCard } from "./RecordingStatusCard";
import type { RecordingStatus } from "./api";
import type { SessionStatusView } from "./sessionStatus";
import type { Surface } from "./SurfaceNav";

type AppHeaderProps = {
  surface: Surface;
  activeWindowAvailable: boolean;
  captureFailed: boolean;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  healthStatus: string;
  /** The one model fact that is a failure rather than configuration. */
  modelDeploymentDegraded: boolean;
  permissionCaptureAvailable: boolean;
  permissionMessage: string | null;
  permissionSteps: string[];
  /** Opens the Settings section holding the technical details behind the badge. */
  onOpenTechnicalDetails: (section: SettingsSection) => void;
  recordingStatus: RecordingStatus;
  recordingStatusUnconfirmed: boolean;
  onPauseRecording: (minutes: number) => void | Promise<void>;
  onResumeRecording: () => void | Promise<void>;
  onResumeAlerts: () => void | Promise<void>;
  /** True while a session runs, so the headline reads as an active state. */
  sessionActive?: boolean;
  /** Shared session/recording vocabulary — same source as Session Control. */
  sessionStatus?: SessionStatusView | null;
  /** The running session's goal, named in the subtitle while it runs. */
  activeGoal?: string | null;
};

export const AppHeader = memo(function AppHeader({
  surface,
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
  recordingStatusUnconfirmed,
  onPauseRecording,
  onResumeRecording,
  onResumeAlerts,
  sessionActive = false,
  sessionStatus = null,
  activeGoal = null,
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

  // One badge for whether anything needs attention, linking to the section with the detail.
  // Reads the capture-failure and permission causes separately (they have different fixes);
  // `checking` keeps an unloaded payload from reading as a refusal.
  const badge = settingsHealthBadge({
    permissionBlocked: !permissionCaptureAvailable && healthStatus !== "checking",
    captureFailed,
    modelFailed: modelDeploymentDegraded,
  });

  const degraded = badge.warning;
  const nowHeadline = sessionActive
    ? (sessionStatus?.label ?? "running")
    : "What are you working on?";
  const nowSubtitle = sessionActive
    ? sessionStatus?.reason
      ? activeGoal
        ? `${sessionStatus.reason} Working on ${activeGoal}.`
        : sessionStatus.reason
      : activeGoal
        ? `Working on ${activeGoal}.`
        : "Recording your focus."
    : "Name a goal and start.";

  return (
    <header className="app-header">
      <div>
        <p className="eyebrow">Snapback</p>
        <h1>
          {surface === "review"
            ? "Review your sessions"
            : surface === "settings"
              ? "Settings"
              : nowHeadline}
        </h1>
        <p className="subtitle">
          {surface === "review"
            ? "See how your sessions went."
            : surface === "settings"
              ? "Choose how Snapback works for you."
              : nowSubtitle}
        </p>
      </div>
      <div className="status-stack">
        <RecordingStatusCard
          variant="header"
          status={recordingStatus}
          unconfirmed={recordingStatusUnconfirmed}
          onPause={onPauseRecording}
          onResume={onResumeRecording}
          onResumeAlerts={onResumeAlerts}
          sessionStatus={sessionActive ? sessionStatus : null}
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
        ) : null}
      </div>
    </header>
  );
});
