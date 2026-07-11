import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

import {
  mapAppRule,
  mapClassifierStatus,
  mapContextSnapshot,
  mapExportTrainingResult,
  mapHealth,
  mapPermissionStatus,
  mapPrediction,
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
  permissions: PermissionStatus;
  classifier: ClassifierStatus;
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
  getLatestPrediction: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_latest_prediction");
    return raw ? mapPrediction(raw) : null;
  },
  getPredictionHistory: async (limit = 8) => {
    const rows = await invoke<Record<string, unknown>[]>("get_prediction_history", { limit });
    return rows.map(mapPrediction);
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
  setFocusMode: (mode: string) => invoke("set_focus_mode", { mode }),
  reloadClassifierModel: async () => {
    const raw = await invoke<Record<string, unknown>>("reload_classifier_model");
    return mapClassifierStatus(raw);
  },
  refreshPermissions: async () => {
    const raw = await invoke<Record<string, unknown>>("refresh_permissions");
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
  formatScore,
  formatTime,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
} from "./utils";
