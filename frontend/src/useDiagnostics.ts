import { useCallback, useEffect, useState } from "react";

import { api, type DiagnosticsSnapshot } from "./api";

const emptyDiagnostics: DiagnosticsSnapshot = {
  version: "0.0.0-dev",
  health: {
    status: "offline",
    captureRunning: false,
    captureFailed: false,
    captureFailureReason: null,
    overlayFailureReason: null,
    persistenceFailureReason: null,
    captureEventsDropped: 0,
    captureStalled: false,
    lastPredictionAgeSecs: null,
    predictionSuppressionReason: "no_session",
    permissions: {
      captureAvailable: false,
      captureProbeConfirmed: false,
      activeWindowAvailable: false,
      message: "",
      setupSteps: [],
    },
    classifier: {
      backend: "heuristic",
      onnxRuntimeEnabled: false,
      modelPath: null,
      modelId: null,
    },
  },
  recentLogs: [],
  supportBundlePrivacyNotice: "",
};

export function useDiagnostics() {
  const [diagnostics, setDiagnostics] = useState(emptyDiagnostics);
  const [status, setStatus] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      setDiagnostics(await api.getDiagnostics());
      setStatus(null);
    } catch {
      setStatus("Diagnostics unavailable.");
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const exportBundle = useCallback(async () => {
    try {
      const result = await api.exportSupportBundle();
      setDiagnostics((current) => ({
        ...current,
        supportBundlePrivacyNotice: result.privacyNotice,
      }));
      setStatus(`Support bundle exported to ${result.outputPath}`);
    } catch {
      setStatus("Could not export the support bundle.");
    }
  }, []);

  return { diagnostics, exportBundle, refresh, status };
}
