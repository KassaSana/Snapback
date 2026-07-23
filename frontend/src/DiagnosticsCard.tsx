import { useDiagnostics } from "./useDiagnostics";

export function DiagnosticsCard() {
  const { diagnostics, refresh, status } = useDiagnostics();
  const { health, recentLogs } = diagnostics;

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
      {health.captureFailureReason || health.overlayFailureReason || health.persistenceFailureReason ? (
        <p className="diagnostics-error">
          {health.captureFailureReason || health.overlayFailureReason || health.persistenceFailureReason}
        </p>
      ) : null}
      <div className="diagnostics-log" aria-label="Recent log lines">
        {recentLogs.length > 0 ? recentLogs.map((line, index) => <code key={`${line}-${index}`}>{line}</code>) : <span className="helper-text">No recent log lines.</span>}
      </div>
      {status ? <p className="helper-text">{status}</p> : null}
    </section>
  );
}
