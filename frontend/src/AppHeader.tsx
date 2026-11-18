import { memo, type ReactNode } from "react";

import { summarizePermissions } from "./healthHints";
import { settingsHealthBadge, type SettingsSection } from "./settingsSections";
import { RecordingStatusCard } from "./RecordingStatusCard";
import type { RecordingStatus } from "./api";
import type { SessionStatusView } from "./sessionStatus";
import type { Surface } from "./SurfaceNav";

type AppHeaderProps = {
  surface: Surface;
  /** True while a session runs, so the title is the goal itself. */
  sessionActive?: boolean;
  activeGoal?: string | null;
  /** Drawn above the title on Now (the session ribbon while a session runs). */
  children?: ReactNode;
};

/** The page title for each surface. On Now it is either the question or the running goal. */
export const AppHeader = memo(function AppHeader({
  surface,
  sessionActive = false,
  activeGoal = null,
  children,
}: AppHeaderProps) {
  const running = surface === "now" && sessionActive;
  const title =
    surface === "review"
      ? "Review your sessions"
      : surface === "settings"
        ? "Settings"
        : running
          ? activeGoal || "Session running"
          : "What are you working on?";

  return (
    <header className="app-header">
      {surface === "now" ? children : null}
      <h1 className={running ? "app-title-goal" : undefined}>{title}</h1>
      {surface === "review" ? <p className="subtitle">See how your sessions went.</p> : null}
    </header>
  );
});

type AppStatusProps = {
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
  /** Shared session/recording vocabulary — same source as Session Control. */
  sessionStatus?: SessionStatusView | null;
};

/**
 * The bottom of the rail: whether Snapback is recording, how to pause it, and (only when
 * something is wrong) which part is unhealthy. Visible on every surface.
 */
export const AppStatus = memo(function AppStatus({
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
  sessionStatus = null,
}: AppStatusProps) {
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

  return (
    <>
      {badge.warning ? (
        <div className="status-stack">
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
            <span className="status-value status-alert">{badge.label}</span>
            <button
              type="button"
              className="link-button"
              onClick={() => onOpenTechnicalDetails(badge.section)}
            >
              Technical details
            </button>
          </div>
        </div>
      ) : null}
      <RecordingStatusCard
        status={recordingStatus}
        unconfirmed={recordingStatusUnconfirmed}
        onPause={onPauseRecording}
        onResume={onResumeRecording}
        onResumeAlerts={onResumeAlerts}
        sessionStatus={sessionStatus}
      />
    </>
  );
});
