import type { PermissionStatus } from "./api";

export type PermissionHealth = {
  label: "ready" | "partial" | "blocked";
  detail: string;
};

export type SessionCaptureReadiness = {
  captureRunning: boolean;
  captureFailed: boolean;
  permissionCaptureAvailable: boolean;
  activeWindowAvailable: boolean;
};

/**
 * Warning to show when a session is started while capture is compromised, or
 * `null` when capture looks healthy. The session still starts (warn, don't
 * block) — this just tells the user up front that the session may not record
 * anything, which is easy to miss if they didn't notice the header status.
 *
 * Ordered worst-first so the single most important reason is surfaced: a hard
 * capture failure, then no capture permission (e.g. a Wayland-only Linux
 * session), then missing active-window access, then capture simply not up yet.
 */
export const sessionStartCaptureWarning = (
  readiness: SessionCaptureReadiness,
): string | null => {
  if (readiness.captureFailed) {
    return "Session started, but input capture has failed. Snapback won't record activity until you fix permissions and restart it.";
  }
  if (!readiness.permissionCaptureAvailable) {
    return "Session started, but input capture isn't available, so it may not record activity. Check permissions. A Wayland-only Linux session needs X11 or XWayland.";
  }
  if (!readiness.activeWindowAvailable) {
    return "Session started, but active-window access isn't available. It may not record which apps and files you work in.";
  }
  if (!readiness.captureRunning) {
    return "Session started, but capture isn't running yet. It may not record the first moments until capture comes up.";
  }
  return null;
};

export const summarizeAppHealth = (input: {
  status: string;
  captureFailed: boolean;
}): "online" | "offline" | "degraded" => {
  if (input.captureFailed || input.status === "capture_failed" || input.status === "offline") {
    return "offline";
  }

  if (input.status === "degraded") {
    return "degraded";
  }

  return "online";
};

type PermissionHealthInput = PermissionStatus & {
  captureFailed: boolean;
  captureRunning: boolean;
};

export const summarizePermissions = (permissions: PermissionHealthInput): PermissionHealth => {
  if (permissions.captureFailed) {
    return {
      label: "blocked",
      detail: "capture failed",
    };
  }

  if (permissions.captureRunning && !permissions.captureProbeConfirmed) {
    return { label: "partial", detail: "running, no input seen yet" };
  }
  if (permissions.captureRunning && permissions.activeWindowAvailable) {
    return {
      label: "ready",
      detail: "running, with window access",
    };
  }

  if (permissions.captureRunning) {
    return {
      label: "partial",
      detail: "running, no window access",
    };
  }

  if (
    permissions.captureAvailable &&
    permissions.activeWindowAvailable &&
    !permissions.captureProbeConfirmed
  ) {
    return {
      label: "partial",
      detail: "not confirmed yet",
    };
  }

  if (permissions.captureAvailable) {
    return {
      label: "partial",
      detail: permissions.captureProbeConfirmed ? "permissions ready, not running" : "capture unverified",
    };
  }

  if (permissions.activeWindowAvailable) {
    return {
      label: "partial",
      detail: "active window access only",
    };
  }

  return {
    label: "blocked",
    detail: "permissions required",
  };
};
