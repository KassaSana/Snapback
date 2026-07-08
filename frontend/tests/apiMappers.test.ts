import assert from "node:assert/strict";

import {
  mapHealth,
  mapPrediction,
  mapSession,
  mapSnapbackPayload,
  mapTrainFromExportResult,
} from "../src/apiMappers";

const healthSnake = mapHealth({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_failure_reason: null,
  permissions: {
    capture_available: true,
    active_window_available: false,
    message: "OK",
    setup_steps: ["Step one"],
  },
  classifier: {
    backend: "onnx",
    onnx_runtime_enabled: true,
    model_path: "/data/model.onnx",
  },
});

assert.equal(healthSnake.status, "online");
assert.equal(healthSnake.captureRunning, true);
assert.equal(healthSnake.permissions.captureAvailable, true);
assert.equal(healthSnake.permissions.setupSteps[0], "Step one");
assert.equal(healthSnake.classifier.backend, "onnx");
assert.equal(healthSnake.classifier.modelPath, "/data/model.onnx");

const healthCamel = mapHealth({
  status: "degraded",
  captureRunning: false,
  captureFailed: true,
  captureFailureReason: "rdev",
  permissions: {
    captureAvailable: false,
    activeWindowAvailable: true,
    message: "Denied",
    setupSteps: [],
  },
  classifier: {
    backend: "heuristic",
    onnxRuntimeEnabled: false,
    modelPath: null,
  },
});

assert.equal(healthCamel.captureFailed, true);
assert.equal(healthCamel.captureFailureReason, "rdev");
assert.equal(healthCamel.classifier.onnxRuntimeEnabled, false);

const prediction = mapPrediction({
  session_id: "sess-1",
  focus_score: 72.5,
  distraction_risk: 0.42,
  focus_state: "PRODUCTIVE",
  thrash_score: 0.1,
  drift_score: 0.2,
  goal_alignment: 0.8,
  timestamp: "2026-07-07T12:00:00Z",
});

assert.equal(prediction.sessionId, "sess-1");
assert.equal(prediction.focusScore, 72.5);
assert.equal(prediction.distractionRisk, 0.42);
assert.equal(prediction.focusState, "PRODUCTIVE");

const session = mapSession({
  sessionId: "sess-2",
  goal: "Ship overlay",
  status: "ACTIVE",
  focusMode: "deep",
  startedAt: "2026-07-07T10:00:00Z",
  endedAt: null,
});

assert.equal(session.sessionId, "sess-2");
assert.equal(session.focusMode, "deep");
assert.equal(session.endedAt, null);

const trainDeployed = mapTrainFromExportResult({
  success: true,
  training_succeeded: true,
  deploy_ready: true,
  message: "Training complete",
  onnx_exported: true,
  metrics: { cv_accuracy: 0.91 },
  log_tail: "done",
});

assert.equal(trainDeployed.success, true);
assert.equal(trainDeployed.trainingSucceeded, true);
assert.equal(trainDeployed.deployReady, true);
assert.equal(trainDeployed.onnxExported, true);
assert.equal(trainDeployed.metrics?.cv_accuracy, 0.91);

const trainNotDeployed = mapTrainFromExportResult({
  success: false,
  trainingSucceeded: true,
  deployReady: false,
  onnxExported: false,
  message: "Skipped ONNX export",
});

assert.equal(trainNotDeployed.success, false);
assert.equal(trainNotDeployed.trainingSucceeded, true);
assert.equal(trainNotDeployed.deployReady, false);
assert.equal(trainNotDeployed.onnxExported, false);

const snapback = mapSnapbackPayload({
  summary: "auth.ts — Snapback",
  app_name: "Code",
  window_title: "auth.ts - Snapback",
  file_hint: "auth.ts",
  distraction_duration_secs: 45,
});

assert.equal(snapback.summary, "auth.ts — Snapback");
assert.equal(snapback.appName, "Code");
assert.equal(snapback.distractionDurationSecs, 45);

console.log("apiMappers.test.ts passed");
