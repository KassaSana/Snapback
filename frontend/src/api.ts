import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

import {
  mapAppRule,
  mapAnalyticsSummary,
  mapAutostartStatus,
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
  mapSummaryExportResult,
  mapGoalCategories,
  mapPrediction,
  mapSettings,
  mapSession,
  mapSessionRecap,
  mapSessionSummary,
  mapSetupSteps,
  mapSnapbackPayload,
  mapTrainFromExportResult,
  mapTrainingDeployStatus,
} from "./apiMappers";

export type RiskLevel = "high" | "medium" | "low" | "unknown";

export type PredictionRecord = {
  sessionId: string;
  focusScore: number;
  distractionRisk: number;
  focusState: string;
  thrashScore: number;
  driftScore: number;
  goalAlignment: number;
  timestamp: string;
};

export type SessionRecord = {
  sessionId: string;
  goal: string;
  status: string;
  focusMode: string;
  startedAt: string | null;
  endedAt: string | null;
};

export type PermissionStatus = {
  captureAvailable: boolean;
  captureProbeConfirmed: boolean;
  activeWindowAvailable: boolean;
  message: string;
  setupSteps: string[];
};

export type CaptureFailurePayload = {
  reason: string;
  message: string;
  setupSteps: string[];
};

export type OverlayFailurePayload = {
  reason: string;
  message: string;
};

export type PersistenceFailurePayload = {
  reason: string;
  message: string;
};

export type LabelHotkeyPayload = {
  ok: boolean;
  message: string;
  label?: string;
  sessionId?: string;
};

export type ClassifierStatus = {
  backend: string;
  onnxRuntimeEnabled: boolean;
  modelPath: string | null;
};

export type HealthStatus = {
  status: string;
  captureRunning: boolean;
  captureFailed: boolean;
  captureFailureReason: string | null;
  overlayFailureReason: string | null;
  persistenceFailureReason: string | null;
  captureEventsDropped: number;
  captureStalled: boolean;
  permissions: PermissionStatus;
  classifier: ClassifierStatus;
};

export type DiagnosticsSnapshot = {
  health: HealthStatus;
  recentLogs: string[];
};

export type SessionRecap = {
  sessionId: string;
  goal: string;
  durationSecs: number;
  avgFocusScore: number;
  avgDistractionRisk: number;
  snapbackCount: number;
  thrashSpikes: number;
  deepFocusPct: number;
};

export type SessionSummary = {
  record: SessionRecord;
  recap: SessionRecap;
};

export type FocusSummary = {
  sampleCount: number;
  avgFocusScore: number;
  peakFocusScore: number;
  distractedSamples: number;
  distractedFraction: number;
  longestFocusStreak: number;
};

export type PomodoroPhase = "work" | "shortBreak" | "longBreak";

export type PomodoroStatus = {
  running: boolean;
  phase: PomodoroPhase;
  completedWorkIntervals: number;
  remainingMs: number;
};

export type FocusLabel =
  | "DISTRACTED"
  | "PSEUDO_PRODUCTIVE"
  | "PRODUCTIVE"
  | "DEEP_FOCUS";

export type LabelSource = "manual" | "hotkey" | "survey" | "auto";

export type AppRuleKind = "allow" | "block";

export type AppRuleRecord = {
  id: number;
  pattern: string;
  ruleType: AppRuleKind;
  note: string | null;
  createdAt: string;
  updatedAt: string;
};

export type ContextSnapshot = {
  appName: string;
  windowTitle: string;
  fileHint: string;
  projectHint: string;
  summary: string;
  timestamp: string;
};

export type SnapbackPayload = {
  summary: string;
  appName: string;
  windowTitle: string;
  fileHint: string;
  distractionDurationSecs: number;
};

export type ExportTrainingResult = {
  outputDir: string;
  featuresPath: string;
  labelsPath: string;
  featureCount: number;
  labelCount: number;
};

export type TrainingDeployStatus = {
  exportDir: string;
  featureCount: number;
  labelCount: number;
  labelBreakdown: Record<string, number>;
  hasExport: boolean;
  modelOnnxExists: boolean;
  metricsExists: boolean;
  metrics: Record<string, number> | null;
  pythonAvailable: boolean;
  repoPath: string | null;
  repoConfigured: boolean;
  pipelineCommand: string;
};

export type AppSettings = {
  defaultFocusMode: string;
};

export type PrivacySettings = {
  privateMode: boolean;
  excludedApps: string[];
  localOnly: boolean;
};

export type AnalyticsHour = {
  hour: number;
  sampleCount: number;
  avgFocusScore: number;
  distractedFraction: number;
};

export type AnalyticsApp = {
  appName: string;
  windowCount: number;
};

export type AnalyticsSummary = {
  sampleCount: number;
  avgFocusScore: number;
  productiveSessionStreak: number;
  hourly: AnalyticsHour[];
  topApps: AnalyticsApp[];
};

export type SummaryWindow = "day" | "week";

export type SummaryReport = {
  window: SummaryWindow;
  generatedAt: string;
  sessionCount: number;
  focusSeconds: number;
  sampleCount: number;
  avgFocusScore: number;
  distractedFraction: number;
  longestFocusStreak: number;
  topContextApp: string;
};

export type SummaryExportResult = {
  window: SummaryWindow;
  outputPath: string;
};

export type GoalCategory = {
  name: string;
  keywords: string[];
};

export type AutostartStatus = {
  enabled: boolean;
  supported: boolean;
};

export type TrainFromExportResult = {
  success: boolean;
  trainingSucceeded: boolean;
  deployReady: boolean;
  message: string;
  onnxExported: boolean;
  metrics: Record<string, number> | null;
  logTail: string;
};

export const api = {
  getHealth: async () => {
    const raw = await invoke<Record<string, unknown>>("get_health");
    return mapHealth(raw);
  },
  getDiagnostics: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_diagnostics");
    return mapDiagnosticsSnapshot(raw ?? {});
  },
  getLatestPrediction: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_latest_prediction");
    return raw ? mapPrediction(raw) : null;
  },
  getPredictionHistory: async (limit = 8) => {
    const rows = await invoke<Record<string, unknown>[]>("get_prediction_history", { limit });
    return rows.map(mapPrediction);
  },
  getFocusSummary: async (limit = 200) => {
    const raw = await invoke<Record<string, unknown>>("get_focus_summary", { limit });
    return mapFocusSummary(raw);
  },
  getPomodoroStatus: async () => {
    const raw = await invoke<Record<string, unknown>>("get_pomodoro_status");
    return mapPomodoroStatus(raw);
  },
  startPomodoro: async () => {
    const raw = await invoke<Record<string, unknown>>("start_pomodoro");
    return mapPomodoroStatus(raw);
  },
  stopPomodoro: async () => {
    const raw = await invoke<Record<string, unknown>>("stop_pomodoro");
    return mapPomodoroStatus(raw);
  },
  startSession: async (goal: string, focusMode = "normal") => {
    const raw = await invoke<Record<string, unknown>>("start_session", { goal, focusMode });
    return mapSession(raw);
  },
  stopSession: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("stop_session", { sessionId });
    return mapSession(raw);
  },
  getSession: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("get_session", { sessionId });
    return mapSession(raw);
  },
  getActiveSession: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_active_session");
    return raw ? mapSession(raw) : null;
  },
  submitLabel: (
    sessionId: string,
    label: FocusLabel,
    notes?: string,
    source: LabelSource = "manual",
  ) =>
    invoke("submit_label", { request: { sessionId, label, notes, source } }),
  getSessionRecap: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("get_session_recap", { sessionId });
    return mapSessionRecap(raw);
  },
  getSessionHistory: async (limit = 20) => {
    const rows = await invoke<Record<string, unknown>[]>("get_session_history", { limit });
    return rows.map(mapSessionSummary);
  },
  getSettings: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_settings");
    return mapSettings(raw ?? {});
  },
  getPrivacySettings: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_privacy_settings");
    return mapPrivacySettings(raw ?? {});
  },
  getAnalytics: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_analytics");
    return mapAnalyticsSummary(raw ?? {});
  },
  getSummaryReport: async (window: SummaryWindow = "day") => {
    const raw = await invoke<Record<string, unknown> | null>("get_summary_report", { window });
    return mapSummaryReport(raw ?? {});
  },
  exportSummaryReport: async (window: SummaryWindow) => {
    const raw = await invoke<Record<string, unknown>>("export_summary_report", { window });
    return mapSummaryExportResult(raw);
  },
  getGoalCategories: async () => {
    const raw = await invoke<Record<string, unknown>[] | null>("get_goal_categories");
    return mapGoalCategories(raw ?? []);
  },
  setGoalCategories: async (categories: GoalCategory[]) => {
    const raw = await invoke<Record<string, unknown>[] | null>("set_goal_categories", { categories });
    return mapGoalCategories(raw ?? []);
  },
  setPrivateMode: async (enabled: boolean) => {
    const raw = await invoke<Record<string, unknown>>("set_private_mode", { enabled });
    return mapPrivacySettings(raw);
  },
  setPrivacyExclusions: async (excludedApps: string[]) => {
    const raw = await invoke<Record<string, unknown>>("set_privacy_exclusions", {
      excludedApps,
    });
    return mapPrivacySettings(raw);
  },
  getAutostart: async () => {
    const raw = await invoke<Record<string, unknown>>("get_autostart");
    return mapAutostartStatus(raw);
  },
  setAutostart: async (enabled: boolean) => {
    const raw = await invoke<Record<string, unknown>>("set_autostart", { enabled });
    return mapAutostartStatus(raw);
  },
  setFocusMode: (mode: string) => invoke("set_focus_mode", { mode }),
  dismissSnapback: () => invoke("dismiss_snapback"),
  reloadClassifierModel: async () => {
    const raw = await invoke<Record<string, unknown>>("reload_classifier_model");
    return mapClassifierStatus(raw);
  },
  refreshPermissions: async () => {
    const raw = await invoke<Record<string, unknown>>("refresh_permissions");
    return mapPermissionStatus(raw);
  },
  // Can raise an OS dialog (macOS Accessibility), so only call this from an explicit
  // user action — never from a poll. refreshPermissions is the dialog-free probe.
  requestPermissions: async () => {
    const raw = await invoke<Record<string, unknown>>("request_permissions");
    return mapPermissionStatus(raw);
  },
  getAppRules: async () => {
    const rows = await invoke<Record<string, unknown>[]>("get_app_rules");
    return rows.map(mapAppRule);
  },
  upsertAppRule: async (pattern: string, ruleType: AppRuleKind, note?: string) => {
    const raw = await invoke<Record<string, unknown>>("upsert_app_rule", {
      request: { pattern, ruleType, note: note ?? null },
    });
    return mapAppRule(raw);
  },
  deleteAppRule: (id: number) => invoke("delete_app_rule", { id }),
  getContextTimeline: async (sessionId?: string, limit = 20) => {
    const rows = await invoke<Record<string, unknown>[]>("get_context_timeline", {
      sessionId: sessionId ?? null,
      limit,
    });
    return rows.map(mapContextSnapshot);
  },
  exportTrainingData: async (sessionId?: string) => {
    const raw = await invoke<Record<string, unknown>>("export_training_data", {
      sessionId: sessionId ?? null,
    });
    return mapExportTrainingResult(raw);
  },
  getTrainingDeployStatus: async () => {
    const raw = await invoke<Record<string, unknown>>("get_training_deploy_status");
    return mapTrainingDeployStatus(raw);
  },
  setTrainingRepoPath: (repoPath: string) =>
    invoke("set_training_repo_path", { repoPath }),
  trainFromExport: async () => {
    const raw = await invoke<Record<string, unknown>>("train_from_export");
    return mapTrainFromExportResult(raw);
  },
  onCaptureFailed: (handler: (payload: CaptureFailurePayload) => void) =>
    listen<Record<string, unknown>>("capture-failed", (event) => {
      const raw = event.payload;
      handler({
        reason: String(raw.reason ?? ""),
        message: String(raw.message ?? ""),
        setupSteps: mapSetupSteps(raw),
      });
    }),
  onOverlayFailed: (handler: (payload: OverlayFailurePayload) => void) =>
    listen<Record<string, unknown>>("overlay-failed", (event) => {
      const raw = event.payload;
      handler({
        reason: String(raw.reason ?? ""),
        message: String(raw.message ?? ""),
      });
    }),
  onPersistenceFailed: (handler: (payload: PersistenceFailurePayload) => void) =>
    listen<Record<string, unknown>>("persistence-failed", (event) => {
      const raw = event.payload;
      handler({
        reason: String(raw.reason ?? ""),
        message: String(raw.message ?? ""),
      });
    }),
  onPrediction: (handler: (record: PredictionRecord) => void) =>
    listen<Record<string, unknown>>("prediction", (event) => {
      handler(mapPrediction(event.payload));
    }),
  onSnapback: (handler: (payload: SnapbackPayload) => void) =>
    listen<Record<string, unknown>>("snapback", (event) => {
      handler(mapSnapbackPayload(event.payload));
    }),
  onPomodoro: (handler: (status: PomodoroStatus) => void) =>
    listen<Record<string, unknown>>("pomodoro", (event) => {
      handler(mapPomodoroStatus(event.payload));
    }),
  onHyperfocus: (handler: (payload: { message: string }) => void) =>
    listen<{ message: string }>("hyperfocus", (event) => handler(event.payload)),
  onLabelHotkey: (handler: (payload: LabelHotkeyPayload) => void) =>
    listen<Record<string, unknown>>("label-hotkey", (event) => {
      const raw = event.payload;
      handler({
        ok: Boolean(raw.ok ?? false),
        message: String(raw.message ?? ""),
        label: raw.label ? String(raw.label) : undefined,
        sessionId: raw.sessionId ? String(raw.sessionId) : undefined,
      });
    }),
};

export {
  buildSignals,
  clamp,
  focusStateLabel,
  formatPercent,
  formatPomodoroRemaining,
  formatScore,
  formatTime,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
} from "./utils";
