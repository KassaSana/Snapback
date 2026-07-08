import type { PermissionStatus } from "./api";

export type PermissionHealth = {
  label: "ready" | "partial" | "blocked";
  detail: string;
};

export const summarizePermissions = (permissions: PermissionStatus): PermissionHealth => {
  if (permissions.captureAvailable && permissions.activeWindowAvailable) {
    return {
      label: "ready",
      detail: "capture + active window access",
    };
  }

  if (permissions.captureAvailable) {
    return {
      label: "partial",
      detail: "capture access only",
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
