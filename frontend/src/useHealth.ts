import { useCallback, useState } from "react";

import { api, type CaptureFailurePayload, type ClassifierStatus } from "./api";

export const useHealth = () => {
  const [healthStatus, setHealthStatus] = useState<"checking" | "online" | "offline">("checking");
  const [captureRunning, setCaptureRunning] = useState(false);
  const [permissionCaptureAvailable, setPermissionCaptureAvailable] = useState(false);
  const [activeWindowAvailable, setActiveWindowAvailable] = useState(false);
  const [permissionMessage, setPermissionMessage] = useState<string | null>(null);
  const [permissionSteps, setPermissionSteps] = useState<string[]>([]);
  const [captureFailed, setCaptureFailed] = useState(false);
  const [captureFailureReason, setCaptureFailureReason] = useState<string | null>(null);
  const [classifierBackend, setClassifierBackend] = useState("heuristic");
  const [classifierOnnxRuntimeEnabled, setClassifierOnnxRuntimeEnabled] = useState(false);
  const [classifierModelPath, setClassifierModelPath] = useState<string | null>(null);

  const applyClassifierStatus = useCallback((status: ClassifierStatus) => {
    setClassifierBackend(status.backend);
    setClassifierOnnxRuntimeEnabled(status.onnxRuntimeEnabled);
    setClassifierModelPath(status.modelPath);
  }, []);

  const applyHealth = useCallback((health: Awaited<ReturnType<typeof api.getHealth>>) => {
    setHealthStatus(
      health.captureFailed ? "offline" : health.status === "degraded" ? "offline" : "online",
    );
    setCaptureRunning(health.captureRunning);
    setCaptureFailed(health.captureFailed);
    setCaptureFailureReason(health.captureFailureReason);
    setPermissionCaptureAvailable(health.permissions.captureAvailable);
    setActiveWindowAvailable(health.permissions.activeWindowAvailable);
    setPermissionMessage(health.permissions.message);
    setPermissionSteps(health.permissions.setupSteps);
    applyClassifierStatus(health.classifier);
  }, [applyClassifierStatus]);

  const applyCaptureFailure = useCallback((payload: CaptureFailurePayload) => {
    setCaptureFailed(true);
    setCaptureRunning(false);
    setCaptureFailureReason(payload.reason);
    setPermissionMessage(payload.message);
    setPermissionSteps(payload.setupSteps);
    setHealthStatus("offline");
  }, []);

  const refreshHealth = useCallback(async () => {
    try {
      const health = await api.getHealth();
      applyHealth(health);
    } catch {
      setHealthStatus("offline");
    }
  }, [applyHealth]);

  const handleRefreshPermissions = useCallback(async () => {
    try {
      const status = await api.refreshPermissions();
      setPermissionCaptureAvailable(status.captureAvailable);
      setActiveWindowAvailable(status.activeWindowAvailable);
      setPermissionMessage(status.message);
      setPermissionSteps(status.setupSteps);
      await refreshHealth();
    } catch {
      setPermissionMessage("Could not refresh permissions.");
    }
  }, [refreshHealth]);

  return {
    activeWindowAvailable,
    applyCaptureFailure,
    applyClassifierStatus,
    captureFailed,
    captureFailureReason,
    captureRunning,
    classifierBackend,
    classifierModelPath,
    classifierOnnxRuntimeEnabled,
    handleRefreshPermissions,
    healthStatus,
    permissionCaptureAvailable,
    permissionMessage,
    permissionSteps,
    refreshHealth,
  };
};
