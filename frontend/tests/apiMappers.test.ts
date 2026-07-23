import assert from "node:assert/strict";

import {
  mapAutostartStatus,
  mapAnalyticsSummary,
  mapAppRule,
  mapClassifierStatus,
  mapContextSnapshot,
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
  mapSetupSteps,
  mapSnapbackPayload,
  mapTrainFromExportResult,
  mapTrainingDeployStatus,
} from "../src/apiMappers";

const healthSnake = mapHealth({
  status: "online",
  capture_running: true,
  capture_failed: false,
  capture_failure_reason: null,
  overlay_failure_reason: "Overlay window failed",
  persistence_failure_reason: "Disk full",
  permissions: {
    capture_available: true,
    capture_probe_confirmed: false,
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
assert.equal(healthSnake.overlayFailureReason, "Overlay window failed");
assert.equal(healthSnake.persistenceFailureReason, "Disk full");
assert.equal(healthSnake.permissions.captureAvailable, true);
assert.equal(healthSnake.permissions.captureProbeConfirmed, false);
assert.equal(healthSnake.permissions.setupSteps[0], "Step one");
assert.equal(healthSnake.classifier.backend, "onnx");
assert.equal(healthSnake.classifier.modelPath, "/data/model.onnx");

const healthCamel = mapHealth({
  status: "degraded",
  captureRunning: false,
  captureFailed: true,
  captureFailureReason: "rdev",
  overlayFailureReason: null,
  persistenceFailureReason: null,
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

const settingsSnake = mapSettings({ default_focus_mode: "deep" });
assert.equal(settingsSnake.defaultFocusMode, "deep");

const settingsCamel = mapSettings({ defaultFocusMode: "recovery" });
assert.equal(settingsCamel.defaultFocusMode, "recovery");

const settingsUnknown = mapSettings({ defaultFocusMode: "bogus" });
assert.equal(settingsUnknown.defaultFocusMode, "normal");

const autostartSnake = mapAutostartStatus({ enabled: true, supported: true });
assert.deepEqual(autostartSnake, { enabled: true, supported: true });

const autostartMissing = mapAutostartStatus({});
assert.deepEqual(autostartMissing, { enabled: false, supported: false });

const privacy = mapPrivacySettings({ private_mode: true, excluded_apps: ["Banking"], local_only: true });
assert.deepEqual(privacy, { privateMode: true, excludedApps: ["Banking"], localOnly: true });

const analytics = mapAnalyticsSummary({
  sample_count: 2,
  avg_focus_score: 72,
  productive_session_streak: 3,
  hourly: [{ hour: 9, sample_count: 2, avg_focus_score: 72, distracted_fraction: 0.5 }],
  top_apps: [{ app_name: "Cursor", window_count: 4 }],
});
assert.equal(analytics.hourly[0].avgFocusScore, 72);
assert.equal(analytics.topApps[0].appName, "Cursor");

const report = mapSummaryReport({
  window: "week",
  session_count: 4,
  focus_seconds: 3600,
  avg_focus_score: 81,
  distracted_fraction: 0.2,
  longest_focus_streak: 8,
  top_context_app: "Cursor",
});
assert.equal(report.window, "week");
assert.equal(report.focusSeconds, 3600);

const categories = mapGoalCategories([{ name: "coding", keywords: ["code", "bug"] }]);
assert.deepEqual(categories, [{ name: "coding", keywords: ["code", "bug"] }]);

const diagnostics = mapDiagnosticsSnapshot({
  version: "0.2.0",
  health: { status: "online", capture_running: true, classifier: { backend: "heuristic" } },
  recent_logs: ["2026-07-19T00:00:00Z [INFO] ready"],
});
assert.equal(diagnostics.version, "0.2.0");
assert.equal(diagnostics.health.status, "online");
assert.equal(diagnostics.recentLogs[0].includes("ready"), true);

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

// --- mapTrainingDeployStatus: the most complex mapper, previously untested ---

const deployEmpty = mapTrainingDeployStatus({});
assert.deepEqual(deployEmpty.labelBreakdown, {});
assert.equal(deployEmpty.metrics, null);
assert.equal(deployEmpty.hasExport, false);
assert.equal(deployEmpty.pipelineCommand, "");

const deploySnake = mapTrainingDeployStatus({
  export_dir: "/data/export",
  feature_count: 120,
  label_count: 40,
  label_breakdown: { DEEP_FOCUS: 5, DISTRACTED: 2 },
  has_export: true,
  model_onnx_exists: true,
  metrics_exists: true,
  metrics: { cv_accuracy: 0.9 },
  python_available: true,
  repo_path: "/repo",
  repo_configured: true,
  pipeline_command: "python -m ml.pipeline_cli",
});
assert.equal(deploySnake.exportDir, "/data/export");
assert.deepEqual(deploySnake.labelBreakdown, { DEEP_FOCUS: 5, DISTRACTED: 2 });
assert.deepEqual(deploySnake.metrics, { cv_accuracy: 0.9 });
assert.equal(deploySnake.repoPath, "/repo");

const deployCamel = mapTrainingDeployStatus({
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
assert.deepEqual(deployCamel.labelBreakdown, { PRODUCTIVE: 3 });
assert.equal(deployCamel.metrics, null);
assert.equal(deployCamel.repoPath, null);

// The guard this mapper exists to enforce: `metrics` must be a plain object,
// not an array or a primitive, or it silently coerces into garbage
// (e.g. Object.entries on an array yields numeric-string keys). Rust never
// sends this shape today, but nothing in TypeScript's type system stops a
// malformed IPC payload from doing so at runtime — this is the one branch
// where "what TypeScript expects" and "what actually arrived" can diverge.
const deployMetricsArray = mapTrainingDeployStatus({ metrics: [1, 2, 3] });
assert.equal(deployMetricsArray.metrics, null);

const deployMetricsString = mapTrainingDeployStatus({ metrics: "not-an-object" });
assert.equal(deployMetricsString.metrics, null);

// --- mapSetupSteps: previously only exercised indirectly via mapHealth ---

assert.deepEqual(mapSetupSteps({}), []);
assert.deepEqual(mapSetupSteps({ setup_steps: ["Step one"] }), ["Step one"]);
assert.deepEqual(mapSetupSteps({ setupSteps: ["Step two"] }), ["Step two"]);
// A non-array value must degrade to [] rather than throwing when the
// caller later calls .map()/.length on the result.
assert.deepEqual(mapSetupSteps({ setup_steps: "oops" }), []);

// --- mapPermissionStatus: previously only exercised indirectly via mapHealth ---

const permissionsEmpty = mapPermissionStatus({});
assert.equal(permissionsEmpty.captureAvailable, false);
assert.equal(permissionsEmpty.message, "");
assert.deepEqual(permissionsEmpty.setupSteps, []);

const permissionsSnake = mapPermissionStatus({
  capture_available: true,
  capture_probe_confirmed: true,
  active_window_available: true,
  message: "OK",
  setup_steps: ["Grant access"],
});
assert.equal(permissionsSnake.captureAvailable, true);
assert.deepEqual(permissionsSnake.setupSteps, ["Grant access"]);

const permissionsCamel = mapPermissionStatus({
  captureAvailable: false,
  captureProbeConfirmed: false,
  activeWindowAvailable: false,
  message: "Denied",
  setupSteps: [],
});
assert.equal(permissionsCamel.message, "Denied");

// --- mapClassifierStatus ---

const classifierEmpty = mapClassifierStatus({});
assert.equal(classifierEmpty.backend, "heuristic");
assert.equal(classifierEmpty.onnxRuntimeEnabled, false);
assert.equal(classifierEmpty.modelPath, null);

const classifierSnake = mapClassifierStatus({
  backend: "onnx",
  onnx_runtime_enabled: true,
  model_path: "/data/model.onnx",
});
assert.equal(classifierSnake.backend, "onnx");
assert.equal(classifierSnake.modelPath, "/data/model.onnx");

const classifierCamel = mapClassifierStatus({
  backend: "heuristic",
  onnxRuntimeEnabled: false,
  modelPath: null,
});
assert.equal(classifierCamel.onnxRuntimeEnabled, false);

// --- mapAppRule ---

const appRuleEmpty = mapAppRule({});
assert.equal(appRuleEmpty.id, 0);
assert.equal(appRuleEmpty.ruleType, "allow");
assert.equal(appRuleEmpty.note, null);

const appRuleSnake = mapAppRule({
  id: 7,
  pattern: "youtube.com",
  rule_type: "block",
  note: "distracting",
  created_at: "2026-07-01T00:00:00Z",
  updated_at: "2026-07-02T00:00:00Z",
});
assert.equal(appRuleSnake.id, 7);
assert.equal(appRuleSnake.ruleType, "block");
assert.equal(appRuleSnake.note, "distracting");

const appRuleCamel = mapAppRule({
  id: 8,
  pattern: "github.com",
  ruleType: "allow",
  note: null,
  createdAt: "2026-07-03T00:00:00Z",
  updatedAt: "2026-07-04T00:00:00Z",
});
assert.equal(appRuleCamel.ruleType, "allow");
assert.equal(appRuleCamel.note, null);

// --- mapContextSnapshot ---

const contextEmpty = mapContextSnapshot({});
assert.equal(contextEmpty.appName, "");
assert.equal(contextEmpty.summary, "");

const contextSnake = mapContextSnapshot({
  app_name: "Code",
  window_title: "auth.ts",
  file_hint: "auth.ts",
  project_hint: "Snapback",
  summary: "Editing auth.ts",
  timestamp: "2026-07-08T00:00:00Z",
});
assert.equal(contextSnake.appName, "Code");
assert.equal(contextSnake.projectHint, "Snapback");

const contextCamel = mapContextSnapshot({
  appName: "Terminal",
  windowTitle: "zsh",
  fileHint: "",
  projectHint: "",
  summary: "Idle",
  timestamp: "2026-07-08T00:01:00Z",
});
assert.equal(contextCamel.appName, "Terminal");

// --- mapExportTrainingResult ---

const exportEmpty = mapExportTrainingResult({});
assert.equal(exportEmpty.outputDir, "");
assert.equal(exportEmpty.featureCount, 0);

const exportSnake = mapExportTrainingResult({
  output_dir: "/data/export",
  features_path: "/data/export/features.csv",
  labels_path: "/data/export/labels.csv",
  feature_count: 200,
  label_count: 50,
});
assert.equal(exportSnake.outputDir, "/data/export");
assert.equal(exportSnake.featureCount, 200);

const exportCamel = mapExportTrainingResult({
  outputDir: "/data/export2",
  featuresPath: "/data/export2/features.csv",
  labelsPath: "/data/export2/labels.csv",
  featureCount: 300,
  labelCount: 60,
});
assert.equal(exportCamel.labelCount, 60);

// --- mapFocusSummary ---

const focusSummaryEmpty = mapFocusSummary({});
assert.equal(focusSummaryEmpty.sampleCount, 0);
assert.equal(focusSummaryEmpty.avgFocusScore, 0);

const focusSummarySnake = mapFocusSummary({
  sample_count: 120,
  avg_focus_score: 68.4,
  peak_focus_score: 97.0,
  distracted_samples: 18,
  distracted_fraction: 0.15,
  longest_focus_streak: 42,
});
assert.equal(focusSummarySnake.sampleCount, 120);
assert.equal(focusSummarySnake.peakFocusScore, 97.0);
assert.equal(focusSummarySnake.distractedFraction, 0.15);
assert.equal(focusSummarySnake.longestFocusStreak, 42);

const focusSummaryCamel = mapFocusSummary({
  sampleCount: 80,
  avgFocusScore: 55.0,
  peakFocusScore: 90.0,
  distractedSamples: 30,
  distractedFraction: 0.375,
  longestFocusStreak: 10,
});
assert.equal(focusSummaryCamel.sampleCount, 80);
assert.equal(focusSummaryCamel.distractedSamples, 30);

// --- mapPomodoroStatus ---

const pomodoroEmpty = mapPomodoroStatus({});
assert.equal(pomodoroEmpty.running, false);
assert.equal(pomodoroEmpty.phase, "work");
assert.equal(pomodoroEmpty.remainingMs, 0);

const pomodoroSnake = mapPomodoroStatus({
  running: true,
  phase: "shortBreak",
  completed_work_intervals: 3,
  remaining_ms: 45_000,
});
assert.equal(pomodoroSnake.running, true);
assert.equal(pomodoroSnake.phase, "shortBreak");
assert.equal(pomodoroSnake.completedWorkIntervals, 3);
assert.equal(pomodoroSnake.remainingMs, 45_000);

const pomodoroCamel = mapPomodoroStatus({
  running: false,
  phase: "longBreak",
  completedWorkIntervals: 4,
  remainingMs: 0,
});
assert.equal(pomodoroCamel.phase, "longBreak");
assert.equal(pomodoroCamel.completedWorkIntervals, 4);

const pomodoroUnknownPhase = mapPomodoroStatus({ running: false, phase: "bogus" });
assert.equal(pomodoroUnknownPhase.phase, "work"); // falls back safely

console.log("apiMappers.test.ts passed");
