import { classifierBackendLabel, formatTrainingMetrics } from "./trainingHints";
import { summarizePermissions } from "./healthHints";

type AppHeaderProps = {
  activeWindowAvailable: boolean;
  captureFailed: boolean;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  classifierBackend: string;
  classifierModelPath: string | null;
  classifierOnnxRuntimeEnabled: boolean;
  classifierMetrics: Record<string, number> | null;
  healthStatus: string;
  permissionCaptureAvailable: boolean;
  permissionMessage: string | null;
  permissionSteps: string[];
};

const modelFileLabel = (path: string | null) => {
  if (!path) {
    return "No model file";
  }
  const normalized = path.replace(/\\/g, "/");
  const segments = normalized.split("/").filter(Boolean);
  return segments[segments.length - 1] ?? path;
};

export function AppHeader({
  activeWindowAvailable,
  captureFailed,
  captureProbeConfirmed,
  captureRunning,
  classifierBackend,
  classifierModelPath,
  classifierOnnxRuntimeEnabled,
  classifierMetrics,
  healthStatus,
  permissionCaptureAvailable,
  permissionMessage,
  permissionSteps,
}: AppHeaderProps) {
  const permissionHealth = summarizePermissions({
    captureAvailable: permissionCaptureAvailable,
    captureFailed,
    captureProbeConfirmed,
    captureRunning,
    activeWindowAvailable,
    message: permissionMessage ?? "",
    setupSteps: permissionSteps,
  });
  const classifierRuntimeLabel = classifierOnnxRuntimeEnabled
    ? "ONNX runtime enabled"
    : "ONNX runtime unavailable";
  const classifierQualityLabel = formatTrainingMetrics(classifierMetrics);
  const activeModelLabel =
    classifierBackend === "onnx"
      ? modelFileLabel(classifierModelPath)
      : classifierModelPath
        ? `${modelFileLabel(classifierModelPath)} available`
        : "Heuristic only";

  return (
    <header className="app-header">
      <div>
        <p className="eyebrow">Snapback</p>
        <h1>Live Focus Command Center</h1>
        <p className="subtitle">
          Measures how you work — deep focus, drift, and context-switch thrash — with snapback
          recovery when you return.
        </p>
      </div>
      <div className="status-stack">
        <div className="status-pill">
          <span className="status-label">App</span>
          <span className={`status-value${healthStatus === "online" ? "" : " status-alert"}`}>
            {healthStatus}
          </span>
        </div>
        <div className="status-pill">
          <span className="status-label">Capture</span>
          <span className={`status-value${captureFailed ? " status-alert" : ""}`}>
            {captureFailed ? "failed" : captureRunning ? "running" : "idle"}
          </span>
        </div>
        <div className="status-pill status-pill-stack">
          <span className="status-label">Permissions</span>
          <span
            className={`status-value${permissionHealth.label === "blocked" ? " status-alert" : ""}`}
          >
            {permissionHealth.label}
          </span>
          <span className="status-detail">{permissionHealth.detail}</span>
        </div>
        <div className="status-pill">
          <span className="status-label">Classifier</span>
          <span className="status-value">{classifierBackendLabel(classifierBackend)}</span>
        </div>
        <div className="status-pill status-pill-stack">
          <span className="status-label">Model</span>
          <span className="status-value">{activeModelLabel}</span>
          <span className="status-detail">
            {classifierQualityLabel ? `${classifierRuntimeLabel} · ${classifierQualityLabel}` : classifierRuntimeLabel}
          </span>
        </div>
      </div>
    </header>
  );
}
