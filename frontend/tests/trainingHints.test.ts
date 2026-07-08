import assert from "node:assert/strict";

import {
  buildExportSummary,
  buildPipelineCommand,
  buildTrainFromExportHint,
  classifyTrainDeployOutcome,
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
    hasExport: false,
    modelOnnxExists: false,
    metricsExists: false,
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
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
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
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    pythonAvailable: false,
    repoPath: "/repo",
    repoConfigured: true,
    pipelineCommand: "",
  }),
  "Install Python training deps: pip install -r ml/requirements-train.txt",
);

assert.equal(
  buildTrainFromExportHint({
    exportDir: outputDir,
    featureCount: 12,
    labelCount: 3,
    hasExport: true,
    modelOnnxExists: false,
    metricsExists: false,
    pythonAvailable: true,
    repoPath: "/repo",
    repoConfigured: true,
    pipelineCommand: "",
  }),
  null,
);

console.log("trainingHints.test.ts passed");
