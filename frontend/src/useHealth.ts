import { useCallback, useState } from "react";

import {
  api,
  type CaptureFailurePayload,
  type ClassifierStatus,
  type OverlayFailurePayload,
  type PersistenceFailurePayload,
} from "./api";
import { summarizeAppHealth } from "./healthHints";

export const useHealth = () => {
  const [healthStatus, setHealthStatus] = useState<"checking" | "online" | "offline" | "degraded">(
    "checking",
  );
  const [captureRunning, setCaptureRunning] = useState(false);
  const [permissionCaptureAvailable, setPermissionCaptureAvailable] = useState(false);
  const [captureProbeConfirmed, setCaptureProbeConfirmed] = useState(false);
  const [activeWindowAvailable, setActiveWindowAvailable] = useState(false);
  const [permissionMessage, setPermissionMessage] = useState<string | null>(null);
  const [permissionSteps, setPermissionSteps] = useState<string[]>([]);
  const [captureFailed, setCaptureFailed] = useState(false);
  const [captureFailureReason, setCaptureFailureReason] = useState<string | null>(null);
  const [overlayFailureReason, setOverlayFailureReason] = useState<string | null>(null);
  const [persistenceFailureReason, setPersistenceFailureReason] = useState<string | null>(null);
  const [captureEventsDropped, setCaptureEventsDropped] = useState(0);
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
      summarizeAppHealth({
        status: health.status,
        captureFailed: health.captureFailed,
      }),
    );
    setCaptureRunning(health.captureRunning);
    setCaptureFailed(health.captureFailed);
    setCaptureFailureReason(health.captureFailureReason);
    setOverlayFailureReason(health.overlayFailureReason);
    setPersistenceFailureReason(health.persistenceFailureReason);
    setCaptureEventsDropped(health.captureEventsDropped);
    setPermissionCaptureAvailable(health.permissions.captureAvailable);
    setCaptureProbeConfirmed(health.permissions.captureProbeConfirmed);
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

  const applyOverlayFailure = useCallback((payload: OverlayFailurePayload) => {
    setOverlayFailureReason(payload.message);
  }, []);

  const applyPersistenceFailure = useCallback((payload: PersistenceFailurePayload) => {
    setPersistenceFailureReason(payload.message);
    setHealthStatus("degraded");
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
      setCaptureProbeConfirmed(status.captureProbeConfirmed);
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
    applyOverlayFailure,
    applyPersistenceFailure,
    captureEventsDropped,
    captureFailed,
    captureFailureReason,
    captureProbeConfirmed,
    captureRunning,
    classifierBackend,
    classifierModelPath,
    classifierOnnxRuntimeEnabled,
    handleRefreshPermissions,
    healthStatus,
    overlayFailureReason,
    persistenceFailureReason,
    permissionCaptureAvailable,
    permissionMessage,
    permissionSteps,
    refreshHealth,
    setOverlayFailureReason,
    setPersistenceFailureReason,
  };
};
