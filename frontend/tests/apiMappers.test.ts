import assert from "node:assert/strict";

import {
  mapAutostartStatus,
  mapAnalyticsSummary,
  mapAppRule,
  mapClassifierStatus,
  mapContextSnapshot,
  mapDailySummary,
  mapDiagnosticsSnapshot,
  mapExportTrainingResult,
  mapFocusSummary,
  mapHealth,
  mapPermissionStatus,
  mapPomodoroStatus,
  mapPrivacySettings,
  mapSummaryReport,
  mapGoalCategories,
  mapPrediction,
  mapSettings,
  mapSession,
  mapSessionLongestSnapback,
  mapSetupSteps,
  mapSnapbackPayload,
  mapTrainFromExportResult,
  mapTrainingDeployStatus,
} from "../src/apiMappers";

assert.deepEqual(mapSessionLongestSnapback({ durationSecs: 360, returnAppName: "Code" }), {
  durationSecs: 360,
  returnAppName: "Code",
});

const healthOnline = mapHealth({
  status: "online",
  captureRunning: true,
  captureFailed: false,
  captureFailureReason: null,
  overlayFailureReason: "Overlay window failed",
  persistenceFailureReason: "Disk full",
  permissions: {
    captureAvailable: true,
    captureProbeConfirmed: false,
    activeWindowAvailable: false,
    message: "OK",
    setupSteps: ["Step one"],
  },
  classifier: {
    backend: "onnx",
    onnxRuntimeEnabled: true,
    modelPath: "/data/model.onnx",
  },
});

assert.equal(healthOnline.status, "online");
assert.equal(healthOnline.captureRunning, true);
assert.equal(healthOnline.overlayFailureReason, "Overlay window failed");
assert.equal(healthOnline.persistenceFailureReason, "Disk full");
assert.equal(healthOnline.permissions.captureAvailable, true);
assert.equal(healthOnline.permissions.captureProbeConfirmed, false);
assert.equal(healthOnline.permissions.setupSteps[0], "Step one");
assert.equal(healthOnline.classifier.backend, "onnx");
assert.equal(healthOnline.classifier.modelPath, "/data/model.onnx");
assert.equal(healthOnline.developerToolsEnabled, false);
assert.equal(healthOnline.modelDeployment.state, "ok");

const healthDegraded = mapHealth({
  status: "degraded",
  captureRunning: false,
  captureFailed: true,
  captureFailureReason: "rdev",
  overlayFailureReason: null,
  persistenceFailureReason: null,
  developerToolsEnabled: true,
  modelDeployment: {
    state: "degraded",
    message: "cleanup blocked",
    preservedPaths: ["model.onnx"],
    retryCleanupAvailable: true,
    rollbackAvailable: true,
  },
  permissions: {
    captureAvailable: false,
    captureProbeConfirmed: false,
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

assert.equal(healthDegraded.captureFailed, true);
assert.equal(healthDegraded.captureFailureReason, "rdev");
assert.equal(healthDegraded.developerToolsEnabled, true);
assert.equal(healthDegraded.modelDeployment.state, "degraded");
assert.equal(healthDegraded.modelDeployment.retryCleanupAvailable, true);
assert.equal(healthDegraded.classifier.onnxRuntimeEnabled, false);

const prediction = mapPrediction({
  sessionId: "sess-1",
  focusScore: 72.5,
  distractionRisk: 0.42,
  focusState: "PRODUCTIVE",
  thrashScore: 0.1,
  driftScore: 0.2,
  goalAlignment: 0.8,
  timestampMs: Date.parse("2026-07-07T12:00:00Z"),
  stateSource: "drift",
});

assert.equal(prediction.sessionId, "sess-1");
assert.equal(prediction.focusScore, 72.5);
assert.equal(prediction.distractionRisk, 0.42);
assert.equal(prediction.focusState, "PRODUCTIVE");
assert.equal(prediction.stateSource, "drift");

// Rows written before verdicts carried provenance (ADR-0004) map to null — unknown is not
// the same claim as "model".
assert.equal(mapPrediction({ sessionId: "old" }).stateSource, null);

const session = mapSession({
  sessionId: "sess-2",
  goal: "Ship overlay",
  status: "ACTIVE",
  focusMode: "deep",
  startedAtMs: Date.parse("2026-07-07T10:00:00Z"),
  endedAtMs: null,
});

assert.equal(session.sessionId, "sess-2");
assert.equal(session.focusMode, "deep");
assert.equal(session.endedAtMs, null);

assert.equal(mapSettings({ defaultFocusMode: "recovery" }).defaultFocusMode, "recovery");

const settingsUnknown = mapSettings({ defaultFocusMode: "bogus" });
assert.equal(settingsUnknown.defaultFocusMode, "normal");

assert.deepEqual(mapAutostartStatus({ enabled: true, supported: true }), {
  enabled: true,
  supported: true,
});

const autostartMissing = mapAutostartStatus({});
assert.deepEqual(autostartMissing, { enabled: false, supported: false });

const privacy = mapPrivacySettings({ privateMode: true, excludedApps: ["Banking"], localOnly: true });
assert.deepEqual(privacy, { privateMode: true, excludedApps: ["Banking"], localOnly: true });

const analytics = mapAnalyticsSummary({
  sampleCount: 2,
  avgFocusScore: 72,
  productiveSessionStreak: 3,
  hourly: [{ hour: 9, sampleCount: 2, avgFocusScore: 72, distractedFraction: 0.5 }],
  topApps: [{ appName: "Cursor", windowCount: 4 }],
});
assert.equal(analytics.hourly[0].avgFocusScore, 72);
assert.equal(analytics.topApps[0].appName, "Cursor");

const report = mapSummaryReport({
  window: "week",
  sessionCount: 4,
  completedSessionCount: 3,
  focusSeconds: 3600,
  avgFocusScore: 81,
  distractedFraction: 0.2,
  longestFocusSecs: 8,
  topContextApp: "Cursor",
  attendedSeconds: 1800,
  plannedMins: 120,
});
assert.equal(report.window, "week");
assert.equal(report.focusSeconds, 3600);
assert.equal(report.completedSessionCount, 3);
// The session cap defaults closed: an older backend that does not send it is not truncated.
assert.equal(report.sessionLimit, 0);
assert.equal(report.sessionsTruncated, false);
assert.equal(
  mapSummaryReport({ window: "all", sessionLimit: 500, sessionsTruncated: true }).sessionsTruncated,
  true,
);
assert.equal(report.attendedSeconds, 1800);
assert.equal(report.plannedMins, 120);

const todayReport = mapSummaryReport({ window: "day", attendedSeconds: 600, plannedMins: 90 });
assert.equal(todayReport.window, "day");
assert.equal(todayReport.attendedSeconds, 600);
assert.equal(todayReport.plannedMins, 90);

const rangeReport = mapSummaryReport({ window: "30d", attendedSeconds: 0, plannedMins: 0 });
assert.equal(rangeReport.window, "30d");
assert.equal(rangeReport.plannedMins, 0);

const categories = mapGoalCategories([{ name: "coding", keywords: ["code", "bug"] }]);
assert.deepEqual(categories, [{ name: "coding", keywords: ["code", "bug"] }]);

const diagnostics = mapDiagnosticsSnapshot({
  version: "0.2.0",
  health: { status: "online", captureRunning: true, classifier: { backend: "heuristic" } },
  recentLogs: ["2026-07-19T00:00:00Z [INFO] ready"],
  supportBundlePrivacyNotice: "Review before sharing.",
});
assert.equal(diagnostics.version, "0.2.0");
assert.equal(diagnostics.health.status, "online");
assert.equal(diagnostics.recentLogs[0].includes("ready"), true);
assert.equal(diagnostics.supportBundlePrivacyNotice, "Review before sharing.");

const trainDeployed = mapTrainFromExportResult({
  success: true,
  trainingSucceeded: true,
  deployReady: true,
  message: "Training complete",
  onnxExported: true,
  metrics: { cv_accuracy: 0.91 },
  logTail: "done",
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
  appName: "Code",
  windowTitle: "auth.ts - Snapback",
  fileHint: "auth.ts",
  distractionDurationSecs: 45,
});

assert.equal(snapback.summary, "auth.ts — Snapback");
assert.equal(snapback.appName, "Code");
assert.equal(snapback.distractionDurationSecs, 45);

// --- mapTrainingDeployStatus: the most complex mapper, previously untested ---

const deployEmpty = mapTrainingDeployStatus({});
assert.deepEqual(deployEmpty.labelBreakdown, {});
assert.equal(deployEmpty.metrics, null);
assert.equal(deployEmpty.hasExport, false);
assert.equal(deployEmpty.pipelineCommand, "");

const deployReadyRepo = mapTrainingDeployStatus({
  exportDir: "/data/export",
  featureCount: 120,
  labelCount: 40,
  labelBreakdown: { DEEP_FOCUS: 5, DISTRACTED: 2 },
  hasExport: true,
  modelOnnxExists: true,
  metricsExists: true,
  metrics: { cv_accuracy: 0.9 },
  pythonAvailable: true,
  repoPath: "/repo",
  repoConfigured: true,
  pipelineCommand: "python -m ml.pipeline_cli",
});
assert.equal(deployReadyRepo.exportDir, "/data/export");
assert.deepEqual(deployReadyRepo.labelBreakdown, { DEEP_FOCUS: 5, DISTRACTED: 2 });
assert.deepEqual(deployReadyRepo.metrics, { cv_accuracy: 0.9 });
assert.equal(deployReadyRepo.repoPath, "/repo");

const deployNoRepo = mapTrainingDeployStatus({
  exportDir: "/data/export2",
  featureCount: 10,
  labelCount: 5,
  labelBreakdown: { PRODUCTIVE: 3 },
  hasExport: true,
  modelOnnxExists: false,
  metricsExists: false,
  metrics: null,
  pythonAvailable: false,
  repoPath: null,
  repoConfigured: false,
  pipelineCommand: "",
});
assert.deepEqual(deployNoRepo.labelBreakdown, { PRODUCTIVE: 3 });
assert.equal(deployNoRepo.metrics, null);
assert.equal(deployNoRepo.repoPath, null);

// The guard this mapper exists to enforce: `metrics` must be a plain object,
// not an array or a primitive, or it silently coerces into garbage
// (e.g. Object.entries on an array yields numeric-string keys). The native layer never
// sends this shape today, but nothing in TypeScript's type system stops a
// malformed IPC payload from doing so at runtime — this is the one branch
// where "what TypeScript expects" and "what actually arrived" can diverge.
const deployMetricsArray = mapTrainingDeployStatus({ metrics: [1, 2, 3] });
assert.equal(deployMetricsArray.metrics, null);

const deployMetricsString = mapTrainingDeployStatus({ metrics: "not-an-object" });
assert.equal(deployMetricsString.metrics, null);

// --- mapSetupSteps: previously only exercised indirectly via mapHealth ---

assert.deepEqual(mapSetupSteps({}), []);
assert.deepEqual(mapSetupSteps({ setupSteps: ["Step one"] }), ["Step one"]);
assert.deepEqual(mapSetupSteps({ setupSteps: ["Step two"] }), ["Step two"]);
// A non-array value must degrade to [] rather than throwing when the
// caller later calls .map()/.length on the result.
assert.deepEqual(mapSetupSteps({ setupSteps: "oops" }), []);

// --- mapPermissionStatus: previously only exercised indirectly via mapHealth ---

const permissionsEmpty = mapPermissionStatus({});
assert.equal(permissionsEmpty.captureAvailable, false);
assert.equal(permissionsEmpty.message, "");
assert.deepEqual(permissionsEmpty.setupSteps, []);

const permissionsGranted = mapPermissionStatus({
  captureAvailable: true,
  captureProbeConfirmed: true,
  activeWindowAvailable: true,
  message: "OK",
  setupSteps: ["Grant access"],
});
assert.equal(permissionsGranted.captureAvailable, true);
assert.deepEqual(permissionsGranted.setupSteps, ["Grant access"]);
assert.equal(permissionsGranted.message, "OK");

// --- mapClassifierStatus ---

const classifierEmpty = mapClassifierStatus({});
assert.equal(classifierEmpty.backend, "heuristic");
assert.equal(classifierEmpty.onnxRuntimeEnabled, false);
assert.equal(classifierEmpty.modelPath, null);

const classifierOnnx = mapClassifierStatus({
  backend: "onnx",
  onnxRuntimeEnabled: true,
  modelPath: "/data/model.onnx",
});
assert.equal(classifierOnnx.backend, "onnx");
assert.equal(classifierOnnx.modelPath, "/data/model.onnx");

const classifierHeuristic = mapClassifierStatus({
  backend: "heuristic",
  onnxRuntimeEnabled: false,
  modelPath: null,
});
assert.equal(classifierHeuristic.onnxRuntimeEnabled, false);
// Inference health defaults to healthy when an older backend does not send it.
assert.equal(classifierHeuristic.inferenceDegraded, false);
assert.equal(classifierHeuristic.inferenceFailures, 0);
const classifierDegraded = mapClassifierStatus({
  backend: "heuristic",
  onnxRuntimeEnabled: false,
  modelPath: "/data/model.onnx",
  inferenceDegraded: true,
  inferenceFailures: 3,
});
assert.equal(classifierDegraded.inferenceDegraded, true);
assert.equal(classifierDegraded.inferenceFailures, 3);

// --- mapAppRule ---

const appRuleEmpty = mapAppRule({});
assert.equal(appRuleEmpty.id, 0);
assert.equal(appRuleEmpty.ruleType, "allow");
assert.equal(appRuleEmpty.note, null);

const appRuleBlock = mapAppRule({
  id: 7,
  pattern: "youtube.com",
  ruleType: "block",
  note: "distracting",
  createdAtMs: Date.parse("2026-07-01T00:00:00Z"),
  updatedAtMs: Date.parse("2026-07-02T00:00:00Z"),
});
assert.equal(appRuleBlock.id, 7);
assert.equal(appRuleBlock.ruleType, "block");
assert.equal(appRuleBlock.note, "distracting");

// --- mapContextSnapshot ---

const contextEmpty = mapContextSnapshot({});
assert.equal(contextEmpty.appName, "");
assert.equal(contextEmpty.summary, "");

const contextCode = mapContextSnapshot({
  appName: "Code",
  windowTitle: "auth.ts",
  fileHint: "auth.ts",
  projectHint: "Snapback",
  summary: "Editing auth.ts",
  timestampMs: Date.parse("2026-07-08T00:00:00Z"),
});
assert.equal(contextCode.appName, "Code");
assert.equal(contextCode.projectHint, "Snapback");

// --- mapExportTrainingResult ---

const exportEmpty = mapExportTrainingResult({});
assert.equal(exportEmpty.outputDir, "");
assert.equal(exportEmpty.featureCount, 0);

const exported = mapExportTrainingResult({
  outputDir: "/data/export",
  featuresPath: "/data/export/features.csv",
  labelsPath: "/data/export/labels.csv",
  featureCount: 200,
  labelCount: 50,
});
assert.equal(exported.outputDir, "/data/export");
assert.equal(exported.featureCount, 200);
assert.equal(exported.labelCount, 50);

// --- mapFocusSummary ---

const focusSummaryEmpty = mapFocusSummary({});
assert.equal(focusSummaryEmpty.sampleCount, 0);
assert.equal(focusSummaryEmpty.avgFocusScore, 0);

const focusSummaryFull = mapFocusSummary({
  sampleCount: 120,
  avgFocusScore: 68.4,
  peakFocusScore: 97.0,
  distractedSamples: 18,
  distractedFraction: 0.15,
  longestFocusSecs: 42,
});
assert.equal(focusSummaryFull.sampleCount, 120);
assert.equal(focusSummaryFull.peakFocusScore, 97.0);
assert.equal(focusSummaryFull.distractedFraction, 0.15);
assert.equal(focusSummaryFull.longestFocusSecs, 42);
assert.equal(focusSummaryFull.distractedSamples, 18);

// --- mapPomodoroStatus ---

const pomodoroEmpty = mapPomodoroStatus({});
assert.equal(pomodoroEmpty.running, false);
assert.equal(pomodoroEmpty.phase, "work");
assert.equal(pomodoroEmpty.remainingMs, 0);

const pomodoroBreak = mapPomodoroStatus({
  running: true,
  phase: "shortBreak",
  completedWorkIntervals: 3,
  remainingMs: 45_000,
});
assert.equal(pomodoroBreak.running, true);
assert.equal(pomodoroBreak.phase, "shortBreak");
assert.equal(pomodoroBreak.completedWorkIntervals, 3);
assert.equal(pomodoroBreak.remainingMs, 45_000);

assert.equal(mapPomodoroStatus({ phase: "longBreak" }).phase, "longBreak");

const pomodoroUnknownPhase = mapPomodoroStatus({ running: false, phase: "bogus" });
assert.equal(pomodoroUnknownPhase.phase, "work"); // falls back safely

const dailyFull = mapDailySummary({
  window: "7d",
  generatedAtMs: 1_787_000_000_000,
  capped: true,
  days: [
    {
      day: "2026-08-05",
      attendedSecs: 1800,
      focusedSecs: 1200,
      deepFocusSecs: 600,
      avgFocusScore: 63.5,
      sampleCount: 412,
      sessionCount: 2,
      snapbackCount: 3,
    },
  ],
});
assert.equal(dailyFull.window, "7d");
assert.equal(dailyFull.capped, true);
assert.equal(dailyFull.days.length, 1);
assert.equal(dailyFull.days[0].day, "2026-08-05");
assert.equal(dailyFull.days[0].attendedSecs, 1800);
assert.equal(dailyFull.days[0].focusedSecs, 1200);
assert.equal(dailyFull.days[0].deepFocusSecs, 600);
assert.equal(dailyFull.days[0].avgFocusScore, 63.5);
assert.equal(dailyFull.days[0].sampleCount, 412);
assert.equal(dailyFull.days[0].sessionCount, 2);
assert.equal(dailyFull.days[0].snapbackCount, 3);

const dailyPartial = mapDailySummary({
  window: "30d",
  generatedAtMs: 5,
  days: [{ day: "2026-08-06", attendedSecs: 60, deepFocusSecs: 30 }],
});
assert.equal(dailyPartial.generatedAtMs, 5);
assert.equal(dailyPartial.capped, false);
assert.equal(dailyPartial.days[0].attendedSecs, 60);
assert.equal(dailyPartial.days[0].deepFocusSecs, 30);
assert.equal(dailyPartial.days[0].focusedSecs, 0);

const dailyEmpty = mapDailySummary({});
assert.equal(dailyEmpty.window, "7d");
assert.equal(dailyEmpty.days.length, 0);

console.log("apiMappers.test.ts passed");
