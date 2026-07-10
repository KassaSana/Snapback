import assert from "node:assert/strict";

import {
  buildTrainingReadinessBlockers,
  buildExportSummary,
  buildPipelineCommand,
  buildTrainFromExportHint,
  classifierBackendLabel,
  classifyTrainDeployOutcome,
  formatLabelBreakdown,
  formatTrainingMetrics,
  isDeployReady,
} from "../src/trainingHints";

const outputDir = "/Users/me/Library/Application Support/com.snapback.app/exports/training";

assert.equal(
  buildExportSummary(12, 3, outputDir),
  "Exported 12 features and 3 labels to /Users/me/Library/Application Support/com.snapback.app/exports/training",
);

const command = buildPipelineCommand(outputDir);
assert.match(command, /^# Run from your Snapback repo root:/);
assert.match(command, /python3 -m ml\.pipeline_cli/);
assert.match(command, /--skip-export/);
assert.match(
  command,
  /--output-dir "\/Users\/me\/Library\/Application Support\/com\.snapback\.app\/exports\/training"/,
);

assert.equal(
  classifyTrainDeployOutcome({
    success: true,
    trainingSucceeded: true,
    deployReady: false,
    onnxExported: false,
    message: "Skipped ONNX",
    metrics: null,
    logTail: "",
  }),
  "trained-not-deployed",
);

assert.equal(
  isDeployReady({
    success: true,
    trainingSucceeded: true,
    deployReady: true,
    onnxExported: true,
    message: "Done",
    metrics: null,
    logTail: "",
  }),
  true,
);

assert.equal(
  classifyTrainDeployOutcome({
    success: false,
    trainingSucceeded: false,
    deployReady: false,
    onnxExported: false,
    message: "Python missing",
    metrics: null,
    logTail: "",
  }),
  "failed",
);

assert.equal(
  buildTrainFromExportHint({
    exportDir: outputDir,
    featureCount: 0,
    labelCount: 0,
    labelBreakdown: {},
    hasExport: false,
    modelOnnxExists: false,
    metricsExists: false,
    metrics: null,
    pythonAvailable: true,
    repoPath: null,
    repoConfigured: false,
    pipelineCommand: "",
  }),
  "Export training data first to generate features and labels.",
);

assert.equal(
  buildTrainFromExportHint({
    exportDir: outputDir,
    featureCount: 12,
    labelCount: 3,
    labelBreakdown: {},
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    metrics: null,
    pythonAvailable: true,
    repoPath: null,
    repoConfigured: false,
    pipelineCommand: "",
  }),
  "Set your Snapback repo path first, or set SNAPBACK_REPO.",
);

assert.equal(
  buildTrainFromExportHint({
    exportDir: outputDir,
    featureCount: 12,
    labelCount: 3,
    labelBreakdown: {},
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    metrics: null,
    pythonAvailable: false,
    repoPath: "/repo",
    repoConfigured: true,
    pipelineCommand: "",
  }),
  "Install Python training deps: pip install -r ml/requirements-train.txt",
);

assert.equal(
  formatLabelBreakdown({
    DEEP_FOCUS: 2,
    PRODUCTIVE: 3,
    DISTRACTED: 1,
  }),
  "deep focus 2 · productive 3 · distracted 1",
);

assert.equal(
  buildTrainFromExportHint({
    exportDir: outputDir,
    featureCount: 12,
    labelCount: 3,
    labelBreakdown: {
      PRODUCTIVE: 3,
    },
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    metrics: null,
    pythonAvailable: true,
    repoPath: "/repo",
    repoConfigured: true,
    pipelineCommand: "",
  }),
  null,
);

assert.deepEqual(
  buildTrainingReadinessBlockers({
    exportDir: outputDir,
    featureCount: 12,
    labelCount: 3,
    labelBreakdown: {
      PRODUCTIVE: 3,
    },
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    metrics: null,
    pythonAvailable: true,
    repoPath: "/repo",
    repoConfigured: true,
    pipelineCommand: "",
  }),
  [
    "Capture at least 8 labeled moments before training.",
    "Label at least two different focus states.",
  ],
);

assert.deepEqual(
  buildTrainingReadinessBlockers({
    exportDir: outputDir,
    featureCount: 20,
    labelCount: 10,
    labelBreakdown: {
      PRODUCTIVE: 6,
      DISTRACTED: 4,
    },
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    metrics: null,
    pythonAvailable: true,
    repoPath: "/repo",
    repoConfigured: true,
    pipelineCommand: "",
  }),
  [],
);

// buildPipelineCommand: when the backend already provides a ready command,
// it must be returned verbatim rather than rebuilding the default script.
assert.equal(
  buildPipelineCommand(outputDir, "python3 -m ml.pipeline_cli --custom"),
  "python3 -m ml.pipeline_cli --custom",
);

// classifierBackendLabel: only "onnx" maps to "ONNX"; anything else is Heuristic.
assert.equal(classifierBackendLabel("onnx"), "ONNX");
assert.equal(classifierBackendLabel("heuristic"), "Heuristic");
assert.equal(classifierBackendLabel(""), "Heuristic");

// formatTrainingMetrics: null in, null out (no metrics to show).
assert.equal(formatTrainingMetrics(null), null);

// Prefers cross-validated keys and formats each as a percentage.
assert.equal(
  formatTrainingMetrics({
    cv_accuracy: 0.912,
    precision_at_10pct: 0.8,
    recall_distracted: 0.75,
  }),
  "accuracy 91.2% · precision@10% 80.0% · recall distracted 75.0%",
);

// Falls back to the in_sample_* keys when the cv_* keys are absent.
assert.equal(
  formatTrainingMetrics({
    in_sample_accuracy: 0.5,
    in_sample_precision_at_10pct: 0.6,
    in_sample_recall_distracted: 0.4,
  }),
  "accuracy 50.0% · precision@10% 60.0% · recall distracted 40.0%",
);

// Only includes the parts that are present; unknown/empty metrics -> null.
assert.equal(
  formatTrainingMetrics({ cv_accuracy: 0.88 }),
  "accuracy 88.0%",
);
assert.equal(formatTrainingMetrics({ unrelated_metric: 1 }), null);

console.log("trainingHints.test.ts passed");
