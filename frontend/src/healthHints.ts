import type { PermissionStatus } from "./api";

export type PermissionHealth = {
  label: "ready" | "partial" | "blocked";
  detail: string;
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
      detail: "capture listener failed",
    };
  }

  if (permissions.captureRunning && permissions.activeWindowAvailable) {
    return {
      label: "ready",
      detail: "listener running + window access",
    };
  }

  if (permissions.captureRunning) {
    return {
      label: "partial",
      detail: "listener running only",
    };
  }

  if (
    permissions.captureAvailable &&
    permissions.activeWindowAvailable &&
    !permissions.captureProbeConfirmed
  ) {
    return {
      label: "partial",
      detail: "probe only until listener starts",
    };
  }

  if (permissions.captureAvailable) {
    return {
      label: "partial",
      detail: permissions.captureProbeConfirmed ? "permissions ready, listener idle" : "capture unverified",
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
