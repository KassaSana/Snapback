import assert from "node:assert/strict";

import {
  buildTrainingReadinessBlockers,
  buildExportSummary,
  buildPipelineCommand,
  buildTrainFromExportHint,
  classifyTrainDeployOutcome,
  formatLabelBreakdown,
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

console.log("trainingHints.test.ts passed");
