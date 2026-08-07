import { memo, useCallback, useState } from "react";

import { api } from "./api";
import { useDiagnostics } from "./useDiagnostics";

export const DiagnosticsCard = memo(function DiagnosticsCard() {
  const { diagnostics, exportBundle, refresh, status } = useDiagnostics();
  const { health, recentLogs } = diagnostics;
  const [cleanupStatus, setCleanupStatus] = useState<string | null>(null);

  const retryModelCleanup = useCallback(async () => {
    try {
      const result = await api.retryModelDeploymentCleanup();
      setCleanupStatus(
        result.state === "ok"
          ? "Model deployment cleanup succeeded."
          : `Cleanup still degraded: ${result.message ?? "retry later"}`,
      );
      await refresh();
    } catch {
      setCleanupStatus("Could not retry model deployment cleanup.");
    }
  }, [refresh]);

  return (
    <section className="card diagnostics-card">
      <div className="card-header">
        <h2>Diagnostics</h2>
        <button className="icon-button" aria-label="Refresh diagnostics" title="Refresh diagnostics" onClick={() => void refresh()}>
          ↻
        </button>
      </div>
      <div className="diagnostics-summary">
        <span className={`health-dot health-dot-${health.status}`} aria-hidden="true" />
        <strong>{health.status}</strong>
        <span>Version {diagnostics.version}</span>
        <span>{health.captureEventsDropped} dropped capture events</span>
        <span>
          Prediction: {health.lastPredictionAgeSecs == null
            ? `none (${health.predictionSuppressionReason})`
            : `${health.lastPredictionAgeSecs.toFixed(1)}s old`}
        </span>
      </div>
      <p className="helper-text">
        Capture: {health.captureRunning ? "running" : "stopped"}. Classifier: {health.classifier.backend}.
      </p>
      {health.modelDeployment.state === "degraded" ? (
        <div className="diagnostics-error-block">
          <p className="diagnostics-error">
            Model deployment cleanup is degraded: {health.modelDeployment.message}
            {health.modelDeployment.preservedPaths.length > 0
              ? ` Preserved: ${health.modelDeployment.preservedPaths.join(", ")}.`
              : ""}
          </p>
          {health.modelDeployment.retryCleanupAvailable ? (
            <button className="secondary-button" onClick={() => void retryModelCleanup()}>
              Retry cleanup
            </button>
          ) : null}
        </div>
      ) : null}
      {health.captureFailureReason || health.overlayFailureReason || health.persistenceFailureReason ? (
        <p className="diagnostics-error">
          {health.captureFailureReason || health.overlayFailureReason || health.persistenceFailureReason}
        </p>
      ) : null}
      <div className="diagnostics-log" aria-label="Recent log lines">
        {recentLogs.length > 0 ? recentLogs.map((line, index) => <code key={`${line}-${index}`}>{line}</code>) : <span className="helper-text">No recent log lines.</span>}
      </div>
      {diagnostics.supportBundlePrivacyNotice ? (
        <p className="helper-text">{diagnostics.supportBundlePrivacyNotice}</p>
      ) : null}
      <div className="button-row">
        <button className="secondary-button" onClick={() => void exportBundle()}>
          Export support bundle
        </button>
      </div>
      {cleanupStatus ? <p className="helper-text">{cleanupStatus}</p> : null}
      {status ? <p className="helper-text">{status}</p> : null}
    </section>
  );
});
