import type { FocusLabel, TrainingDeployStatus } from "./api";
import {
  buildTrainingReadinessBlockers,
  formatLabelBreakdown,
  formatTrainingMetrics,
} from "./trainingHints";

type TrainingDeployCardProps = {
  canTrainFromExport: boolean;
  classifierBackend: string;
  classifierModelPath: string | null;
  copyStatus: string | null;
  deployMessage: string | null;
  deployMessageWarning: boolean;
  deployStatus: TrainingDeployStatus | null;
  handleCopyTrainingCommand: () => void | Promise<void>;
  handleExportTrainingData: () => void | Promise<void>;
  handleLabel: (label: FocusLabel) => void | Promise<void>;
  handleReloadClassifierModel: () => void | Promise<void>;
  handleSaveRepoPath: () => void | Promise<void>;
  handleTrainFromExport: () => void | Promise<void>;
  labelStatus: string | null;
  labelStatusWarning: boolean;
  modelReloadStatus: string | null;
  repoPathInput: string;
  setRepoPathInput: (value: string) => void;
  setShowAdvancedCommand: (updater: (current: boolean) => boolean) => void;
  showAdvancedCommand: boolean;
  trainFromExportHint: string | null;
  trainingCommand: string | null;
  trainingInProgress: boolean;
};

export function TrainingDeployCard({
  canTrainFromExport,
  classifierBackend,
  classifierModelPath,
  copyStatus,
  deployMessage,
  deployMessageWarning,
  deployStatus,
  handleCopyTrainingCommand,
  handleExportTrainingData,
  handleLabel,
  handleReloadClassifierModel,
  handleSaveRepoPath,
  handleTrainFromExport,
  labelStatus,
  labelStatusWarning,
  modelReloadStatus,
  repoPathInput,
  setRepoPathInput,
  setShowAdvancedCommand,
  showAdvancedCommand,
  trainFromExportHint,
  trainingCommand,
  trainingInProgress,
}: TrainingDeployCardProps) {
  const readinessBlockers = buildTrainingReadinessBlockers(deployStatus);
  const metricsSummary = deployStatus ? formatTrainingMetrics(deployStatus.metrics) : null;

  return (
    <section className="card feedback-card">
      <div className="card-header">
        <h2>Focus Feedback</h2>
        <span className="pill">train the model</span>
      </div>
      <p className="helper-text">
        One tap — was that moment actually focused? Global hotkeys: Ctrl+Shift+1 deep, 2 focused,
        3 drift, 4 distracted (works from any app during a session).
      </p>
      <div className="button-row feedback-row">
        <button className="secondary-button" onClick={() => void handleLabel("DEEP_FOCUS")}>
          Deep
        </button>
        <button className="secondary-button" onClick={() => void handleLabel("PRODUCTIVE")}>
          Focused
        </button>
        <button
          className="secondary-button"
          onClick={() => void handleLabel("PSEUDO_PRODUCTIVE")}
        >
          Drift
        </button>
        <button className="secondary-button" onClick={() => void handleLabel("DISTRACTED")}>
          Distracted
        </button>
        <button className="secondary-button" onClick={() => void handleExportTrainingData()}>
          Export training data
        </button>
      </div>
      {labelStatus ? (
        <p className={`helper-text${labelStatusWarning ? " alert" : ""}`}>{labelStatus}</p>
      ) : null}
      {deployStatus ? (
        <div className="training-deploy-block">
          <p className="deploy-title">Training readiness</p>
          <p className="helper-text">
            Exported rows: {deployStatus.featureCount} features and {deployStatus.labelCount} labels.
          </p>
          <p className="helper-text">
            Label balance: {formatLabelBreakdown(deployStatus.labelBreakdown)}
          </p>
          {metricsSummary ? <p className="helper-text">Latest quality: {metricsSummary}</p> : null}
          {readinessBlockers.length > 0 ? (
            <ul className="permission-steps">
              {readinessBlockers.map((blocker) => (
                <li key={blocker}>{blocker}</li>
              ))}
            </ul>
          ) : (
            <p className="helper-text">Ready to train from the current export.</p>
          )}
        </div>
      ) : null}

      <div className="training-deploy-block">
        <p className="deploy-title">Deploy trained model</p>
        <ol className="deploy-steps">
          <li className={deployStatus?.hasExport ? "deploy-step done" : "deploy-step"}>
            <span className="deploy-step-label">Export</span>
            <span className="deploy-step-detail">
              {deployStatus?.hasExport
                ? `${deployStatus.featureCount} features · ${deployStatus.labelCount} labels`
                : "Export training data above"}
            </span>
          </li>
          <li
            className={
              deployStatus?.modelOnnxExists || deployStatus?.metricsExists
                ? "deploy-step done"
                : "deploy-step"
            }
          >
            <span className="deploy-step-label">Train</span>
            <span className="deploy-step-detail">
              {deployStatus?.modelOnnxExists
                ? "model.onnx ready"
                : deployStatus?.metricsExists
                  ? "model.json trained (ONNX pending)"
                  : deployStatus?.pythonAvailable
                    ? "Run train from export"
                    : "Install Python 3 + xgboost"}
            </span>
          </li>
          <li className={classifierBackend === "onnx" ? "deploy-step done" : "deploy-step"}>
            <span className="deploy-step-label">Activate</span>
            <span className="deploy-step-detail">
              {classifierBackend === "onnx"
                ? "ONNX classifier active"
                : deployStatus?.modelOnnxExists
                  ? "Reload model to switch off heuristic"
                  : "Reload after training"}
            </span>
          </li>
        </ol>

        <label className="field deploy-repo-field">
          <span>Snapback repo path (for in-app training)</span>
          <input
            type="text"
            placeholder="C:\Users\you\Projects\Snapback"
            value={repoPathInput}
            onChange={(event) => setRepoPathInput(event.target.value)}
          />
        </label>
        <div className="button-row">
          <button className="secondary-button" onClick={() => void handleSaveRepoPath()}>
            Save repo path
          </button>
          <button
            className="primary-button"
            disabled={!canTrainFromExport}
            onClick={() => void handleTrainFromExport()}
          >
            {trainingInProgress ? "Training…" : "Train from export"}
          </button>
          <button className="secondary-button" onClick={() => void handleReloadClassifierModel()}>
            Reload model
          </button>
        </div>
        {trainFromExportHint ? <p className="helper-text alert">{trainFromExportHint}</p> : null}
        {deployMessage ? (
          <p className={`helper-text deploy-log${deployMessageWarning ? " alert" : ""}`}>
            {deployMessage}
          </p>
        ) : null}
        {modelReloadStatus ? <p className="helper-text">{modelReloadStatus}</p> : null}
        {classifierModelPath ? (
          <p className="helper-text">Model path: {classifierModelPath}</p>
        ) : null}
        <button
          type="button"
          className="link-button"
          onClick={() => setShowAdvancedCommand((current) => !current)}
        >
          {showAdvancedCommand ? "Hide CLI command" : "Show CLI command"}
        </button>
        {showAdvancedCommand && deployStatus && trainingCommand ? (
          <div className="training-command-block">
            <pre className="training-command">{trainingCommand}</pre>
            <button className="secondary-button" onClick={() => void handleCopyTrainingCommand()}>
              Copy CLI command
            </button>
            {copyStatus ? <p className="helper-text">{copyStatus}</p> : null}
          </div>
        ) : null}
      </div>
    </section>
  );
}
