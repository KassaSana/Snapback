/** Build the offline training command to run after in-app export. */
import type { TrainFromExportResult, TrainingDeployStatus } from "./api";

export type TrainDeployOutcome = "failed" | "trained-not-deployed" | "deploy-ready";
const MIN_TRAINING_LABELS = 8;
const MIN_SAMPLES_PER_LABEL = 2;

export const buildPipelineCommand = (outputDir: string, pipelineCommand?: string) => {
  if (pipelineCommand) {
    return pipelineCommand;
  }
  const quotedDir = `"${outputDir.replace(/"/g, '\\"')}"`;
  return [
    "# Run from your Snapback repo root:",
    "python3 -m ml.pipeline_cli \\",
    `  --output-dir ${quotedDir} \\`,
    "  --skip-export",
  ].join("\n");
};

export const buildExportSummary = (
  featureCount: number,
  labelCount: number,
  outputDir: string,
) =>
  `Exported ${featureCount} features and ${labelCount} labels to ${outputDir}`;

export const buildTrainFromExportHint = (status: TrainingDeployStatus | null) => {
  if (!status?.hasExport) {
    return "Export training data first to generate features and labels.";
  }
  if (!status.repoConfigured) {
    return "Set your Snapback repo path first, or set SNAPBACK_REPO.";
  }
  if (!status.pythonAvailable) {
    return "Install Python training deps: pip install -r ml/requirements-train.txt";
  }
  return null;
};

export const formatLabelBreakdown = (labelBreakdown: Record<string, number>) => {
  const orderedKeys = ["DEEP_FOCUS", "PRODUCTIVE", "PSEUDO_PRODUCTIVE", "DISTRACTED"];
  const entries = orderedKeys
    .filter((key) => Number(labelBreakdown[key] ?? 0) > 0)
    .map((key) => `${key.toLowerCase().replace(/_/g, " ")} ${labelBreakdown[key]}`);
  return entries.length > 0 ? entries.join(" · ") : "no labels exported yet";
};

export const buildTrainingReadinessBlockers = (status: TrainingDeployStatus | null) => {
  if (!status) {
    return ["Training status unavailable."];
  }
  const blockers: string[] = [];
  if (status.featureCount === 0) {
    blockers.push("Capture at least one active session and export features.");
  }
  if (status.labelCount === 0) {
    blockers.push("Add feedback labels before training.");
  }
  if (status.labelCount > 0 && status.labelCount < MIN_TRAINING_LABELS) {
    blockers.push(`Capture at least ${MIN_TRAINING_LABELS} labeled moments before training.`);
  }

  const activeLabels = Object.entries(status.labelBreakdown).filter(([, count]) => count > 0);
  if (status.labelCount > 0 && activeLabels.length < 2) {
    blockers.push("Label at least two different focus states.");
  } else if (activeLabels.some(([, count]) => count < MIN_SAMPLES_PER_LABEL)) {
    blockers.push(`Add at least ${MIN_SAMPLES_PER_LABEL} examples for each exported label.`);
  }
  return blockers;
};

export const classifierBackendLabel = (backend: string) =>
  backend === "onnx" ? "ONNX" : "Heuristic";

export const formatTrainingMetrics = (metrics: Record<string, number> | null) => {
  if (!metrics) {
    return null;
  }
  const accuracy = metrics.cv_accuracy ?? metrics.in_sample_accuracy;
  const precision = metrics.precision_at_10pct ?? metrics.in_sample_precision_at_10pct;
  const recall = metrics.recall_distracted ?? metrics.in_sample_recall_distracted;
  const parts: string[] = [];
  if (accuracy !== undefined) {
    parts.push(`accuracy ${(accuracy * 100).toFixed(1)}%`);
  }
  if (precision !== undefined) {
    parts.push(`precision@10% ${(precision * 100).toFixed(1)}%`);
  }
  if (recall !== undefined) {
    parts.push(`recall distracted ${(recall * 100).toFixed(1)}%`);
  }
  return parts.length > 0 ? parts.join(" · ") : null;
};

export const classifyTrainDeployOutcome = (
  result: TrainFromExportResult,
): TrainDeployOutcome => {
  if (result.trainingSucceeded && !result.deployReady) {
    return "trained-not-deployed";
  }
  if (!result.trainingSucceeded) {
    return "failed";
  }
  return "deploy-ready";
};

export const isDeployReady = (result: TrainFromExportResult) =>
  result.deployReady && classifyTrainDeployOutcome(result) === "deploy-ready";
