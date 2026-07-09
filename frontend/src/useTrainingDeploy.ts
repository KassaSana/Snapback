import { useCallback, useMemo, useState } from "react";

import { api, type ClassifierStatus } from "./api";
import {
  buildExportSummary,
  buildPipelineCommand,
  buildTrainFromExportHint,
  classifyTrainDeployOutcome,
  formatTrainingMetrics,
  isDeployReady,
} from "./trainingHints";

type UseTrainingDeployArgs = {
  sessionId: string | null;
  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
  onClassifierStatusChange: (status: ClassifierStatus) => void;
};

const buildModelReloadStatus = (status: ClassifierStatus) => {
  if (status.backend === "onnx") {
    return "Loaded trained ONNX model.";
  }
  if (status.modelPath) {
    return "Model file found but ONNX runtime is not active in this build.";
  }
  return "No model.onnx found. Run the training pipeline first.";
};

export const useTrainingDeploy = ({
  sessionId,
  setLabelStatus,
  setLabelStatusWarning,
  onClassifierStatusChange,
}: UseTrainingDeployArgs) => {
  const [deployStatus, setDeployStatus] = useState<Awaited<
    ReturnType<typeof api.getTrainingDeployStatus>
  > | null>(null);
  const [repoPathInput, setRepoPathInput] = useState("");
  const [trainingInProgress, setTrainingInProgress] = useState(false);
  const [deployMessage, setDeployMessage] = useState<string | null>(null);
  const [deployMessageWarning, setDeployMessageWarning] = useState(false);
  const [showAdvancedCommand, setShowAdvancedCommand] = useState(false);
  const [copyStatus, setCopyStatus] = useState<string | null>(null);
  const [modelReloadStatus, setModelReloadStatus] = useState<string | null>(null);

  const refreshDeployStatus = useCallback(async () => {
    try {
      const status = await api.getTrainingDeployStatus();
      setDeployStatus(status);
      if (status.repoPath && !repoPathInput) {
        setRepoPathInput(status.repoPath);
      }
    } catch {
      setDeployStatus(null);
    }
  }, [repoPathInput]);

  const handleExportTrainingData = useCallback(async () => {
    setCopyStatus(null);
    try {
      const result = await api.exportTrainingData(sessionId ?? undefined);
      if (result.featureCount === 0 && result.labelCount === 0) {
        setLabelStatus(
          "No training data yet. Run a session and tap feedback, then export again.",
        );
        setLabelStatusWarning(false);
        return;
      }
      setLabelStatus(
        buildExportSummary(result.featureCount, result.labelCount, result.outputDir),
      );
      setLabelStatusWarning(false);
      setDeployMessage("Export ready — train from export or reload if you already trained.");
      await refreshDeployStatus();
    } catch {
      setLabelStatus("Could not export training data.");
      setLabelStatusWarning(false);
    }
  }, [refreshDeployStatus, sessionId, setLabelStatus, setLabelStatusWarning]);

  const handleSaveRepoPath = useCallback(async () => {
    const trimmed = repoPathInput.trim();
    if (!trimmed) {
      setDeployMessage("Enter the folder that contains ml/pipeline_cli.py.");
      return;
    }
    try {
      await api.setTrainingRepoPath(trimmed);
      setDeployMessage("Saved repo path.");
      await refreshDeployStatus();
    } catch {
      setDeployMessage("Could not save repo path. Pick the Snapback repo root.");
    }
  }, [refreshDeployStatus, repoPathInput]);

  const handleTrainFromExport = useCallback(async () => {
    setTrainingInProgress(true);
    setDeployMessage(null);
    setDeployMessageWarning(false);
    setCopyStatus(null);
    try {
      const result = await api.trainFromExport();
      const metricsSummary = formatTrainingMetrics(result.metrics);
      const detail = metricsSummary ? ` ${metricsSummary}.` : "";
      const outcome = classifyTrainDeployOutcome(result);
      const deployNotReady = outcome === "trained-not-deployed";
      const messageParts = [
        deployNotReady
          ? `Deploy not ready: ${result.message}${detail}`
          : outcome === "failed"
            ? result.message
            : `${result.message}${detail}`,
      ];
      if (result.logTail) {
        messageParts.push(result.logTail);
      }
      setDeployMessage(messageParts.join("\n"));
      setDeployMessageWarning(deployNotReady || outcome === "failed");
      await refreshDeployStatus();
      if (isDeployReady(result)) {
        try {
          const status = await api.reloadClassifierModel();
          onClassifierStatusChange(status);
          setModelReloadStatus(buildModelReloadStatus(status));
        } catch {
          setModelReloadStatus("Training succeeded but reload failed — click Reload model.");
        }
      }
    } catch (err) {
      setDeployMessage(err instanceof Error ? err.message : "Training could not start.");
    } finally {
      setTrainingInProgress(false);
    }
  }, [onClassifierStatusChange, refreshDeployStatus]);

  const handleCopyTrainingCommand = useCallback(async () => {
    const command = deployStatus
      ? buildPipelineCommand(deployStatus.exportDir, deployStatus.pipelineCommand)
      : null;
    if (!command) {
      return;
    }
    try {
      await navigator.clipboard.writeText(command);
      setCopyStatus("Copied CLI command.");
    } catch {
      setCopyStatus("Select and copy the command manually.");
    }
  }, [deployStatus]);

  const handleReloadClassifierModel = useCallback(async () => {
    try {
      const status = await api.reloadClassifierModel();
      onClassifierStatusChange(status);
      setModelReloadStatus(buildModelReloadStatus(status));
    } catch {
      setModelReloadStatus("Could not reload classifier model.");
    }
  }, [onClassifierStatusChange]);

  const trainFromExportHint = useMemo(
    () => buildTrainFromExportHint(deployStatus),
    [deployStatus],
  );
  const trainingCommand = useMemo(
    () =>
      deployStatus
        ? buildPipelineCommand(deployStatus.exportDir, deployStatus.pipelineCommand)
        : null,
    [deployStatus],
  );
  const canTrainFromExport = useMemo(
    () =>
      !trainingInProgress &&
      Boolean(
        deployStatus?.hasExport && deployStatus.repoConfigured && deployStatus.pythonAvailable,
      ),
    [deployStatus, trainingInProgress],
  );

  return {
    canTrainFromExport,
    copyStatus,
    deployMessage,
    deployMessageWarning,
    deployStatus,
    handleCopyTrainingCommand,
    handleExportTrainingData,
    handleReloadClassifierModel,
    handleSaveRepoPath,
    handleTrainFromExport,
    modelReloadStatus,
    refreshDeployStatus,
    repoPathInput,
    setRepoPathInput,
    setShowAdvancedCommand,
    showAdvancedCommand,
    trainingCommand,
    trainFromExportHint,
    trainingInProgress,
  };
};
