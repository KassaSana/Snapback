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
  const [captureStalled, setCaptureStalled] = useState(false);
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
    setCaptureStalled(health.captureStalled);
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
    setCaptureStalled(false);
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

  const applyPermissionStatus = useCallback(
    (status: Awaited<ReturnType<typeof api.refreshPermissions>>) => {
      setPermissionCaptureAvailable(status.captureAvailable);
      setCaptureProbeConfirmed(status.captureProbeConfirmed);
      setActiveWindowAvailable(status.activeWindowAvailable);
      setPermissionMessage(status.message);
      setPermissionSteps(status.setupSteps);
    },
    [],
  );

  const handleRefreshPermissions = useCallback(async () => {
    try {
      applyPermissionStatus(await api.refreshPermissions());
      await refreshHealth();
    } catch {
      setPermissionMessage("Could not refresh permissions.");
    }
  }, [applyPermissionStatus, refreshHealth]);

  // Triggers the OS permission dialog, then reflects whatever the user chose. Separate
  // from refresh because refresh is also called on a timer — prompting from there would
  // pop a dialog repeatedly.
  const handleRequestPermissions = useCallback(async () => {
    try {
      applyPermissionStatus(await api.requestPermissions());
      await refreshHealth();
    } catch {
      setPermissionMessage("Could not request permissions.");
    }
  }, [applyPermissionStatus, refreshHealth]);

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
    captureStalled,
    classifierBackend,
    classifierModelPath,
    classifierOnnxRuntimeEnabled,
    handleRefreshPermissions,
    handleRequestPermissions,
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
