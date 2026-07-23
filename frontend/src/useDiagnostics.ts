import { useCallback, useEffect, useState } from "react";

import { api, type DiagnosticsSnapshot } from "./api";

const emptyDiagnostics: DiagnosticsSnapshot = {
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
    classifier: { backend: "heuristic", onnxRuntimeEnabled: false, modelPath: null },
  },
  recentLogs: [],
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

  return { diagnostics, refresh, status };
}
