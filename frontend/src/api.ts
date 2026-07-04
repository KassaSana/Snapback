import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

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
  activeWindowAvailable: boolean;
  message: string;
  setupSteps: string[];
};

export type CaptureFailurePayload = {
  reason: string;
  message: string;
  setupSteps: string[];
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

function mapContextSnapshot(raw: Record<string, unknown>): ContextSnapshot {
  return {
    appName: String(raw.app_name ?? raw.appName ?? ""),
    windowTitle: String(raw.window_title ?? raw.windowTitle ?? ""),
    fileHint: String(raw.file_hint ?? raw.fileHint ?? ""),
    projectHint: String(raw.project_hint ?? raw.projectHint ?? ""),
    summary: String(raw.summary ?? ""),
    timestamp: String(raw.timestamp ?? ""),
  };
}

function mapSetupSteps(raw: Record<string, unknown>): string[] {
  const steps = raw.setup_steps ?? raw.setupSteps;
  return Array.isArray(steps) ? steps.map((step: unknown) => String(step)) : [];
}

function mapPermissionStatus(raw: Record<string, unknown>): PermissionStatus {
  return {
    captureAvailable: Boolean(raw.capture_available ?? raw.captureAvailable ?? false),
    activeWindowAvailable: Boolean(
      raw.active_window_available ?? raw.activeWindowAvailable ?? false,
    ),
    message: String(raw.message ?? ""),
    setupSteps: mapSetupSteps(raw),
  };
}

function mapClassifierStatus(raw: Record<string, unknown>): ClassifierStatus {
  return {
    backend: String(raw.backend ?? "heuristic"),
    onnxRuntimeEnabled: Boolean(raw.onnx_runtime_enabled ?? raw.onnxRuntimeEnabled ?? false),
    modelPath: (raw.model_path ?? raw.modelPath ?? null) as string | null,
  };
}

function mapHealth(raw: Record<string, unknown>): HealthStatus {
  return {
    status: String(raw.status ?? "offline"),
    captureRunning: Boolean(raw.capture_running ?? raw.captureRunning ?? false),
    captureFailed: Boolean(raw.capture_failed ?? raw.captureFailed ?? false),
    captureFailureReason: (raw.capture_failure_reason ??
      raw.captureFailureReason ??
      null) as string | null,
    permissions: mapPermissionStatus(
      (raw.permissions as Record<string, unknown>) ?? {},
    ),
    classifier: mapClassifierStatus(
      (raw.classifier as Record<string, unknown>) ?? {},
    ),
  };
}

function mapAppRule(raw: Record<string, unknown>): AppRuleRecord {
  return {
    id: Number(raw.id ?? 0),
    pattern: String(raw.pattern ?? ""),
    ruleType: String(raw.rule_type ?? raw.ruleType ?? "allow") as AppRuleKind,
    note: (raw.note ?? null) as string | null,
    createdAt: String(raw.created_at ?? raw.createdAt ?? ""),
    updatedAt: String(raw.updated_at ?? raw.updatedAt ?? ""),
  };
}

function mapPrediction(raw: Record<string, unknown>): PredictionRecord {
  return {
    sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
    focusScore: Number(raw.focus_score ?? raw.focusScore ?? 0),
    distractionRisk: Number(raw.distraction_risk ?? raw.distractionRisk ?? 0),
    focusState: String(raw.focus_state ?? raw.focusState ?? "UNKNOWN"),
    thrashScore: Number(raw.thrash_score ?? raw.thrashScore ?? 0),
    driftScore: Number(raw.drift_score ?? raw.driftScore ?? 0),
    goalAlignment: Number(raw.goal_alignment ?? raw.goalAlignment ?? 0.5),
    timestamp: String(raw.timestamp ?? ""),
  };
}

function mapSession(raw: Record<string, unknown>): SessionRecord {
  return {
    sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
    goal: String(raw.goal ?? ""),
    status: String(raw.status ?? ""),
    focusMode: String(raw.focus_mode ?? raw.focusMode ?? "normal"),
    startedAt: (raw.started_at ?? raw.startedAt ?? null) as string | null,
    endedAt: (raw.ended_at ?? raw.endedAt ?? null) as string | null,
  };
}

export type ExportTrainingResult = {
  outputDir: string;
  featuresPath: string;
  labelsPath: string;
  featureCount: number;
  labelCount: number;
};

function mapExportTrainingResult(raw: Record<string, unknown>): ExportTrainingResult {
  return {
    outputDir: String(raw.output_dir ?? raw.outputDir ?? ""),
    featuresPath: String(raw.features_path ?? raw.featuresPath ?? ""),
    labelsPath: String(raw.labels_path ?? raw.labelsPath ?? ""),
    featureCount: Number(raw.feature_count ?? raw.featureCount ?? 0),
    labelCount: Number(raw.label_count ?? raw.labelCount ?? 0),
  };
}

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
    return {
      sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
      goal: String(raw.goal ?? ""),
      durationSecs: Number(raw.duration_secs ?? raw.durationSecs ?? 0),
      avgFocusScore: Number(raw.avg_focus_score ?? raw.avgFocusScore ?? 0),
      avgDistractionRisk: Number(raw.avg_distraction_risk ?? raw.avgDistractionRisk ?? 0),
      snapbackCount: Number(raw.snapback_count ?? raw.snapbackCount ?? 0),
      thrashSpikes: Number(raw.thrash_spikes ?? raw.thrashSpikes ?? 0),
      deepFocusPct: Number(raw.deep_focus_pct ?? raw.deepFocusPct ?? 0),
    } satisfies SessionRecap;
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
  onCaptureFailed: (handler: (payload: CaptureFailurePayload) => void) =>
    listen<Record<string, unknown>>("capture-failed", (event) => {
      const raw = event.payload;
      handler({
        reason: String(raw.reason ?? ""),
        message: String(raw.message ?? ""),
        setupSteps: mapSetupSteps(raw),
      });
    }),
  onPrediction: (handler: (record: PredictionRecord) => void) =>
    listen<Record<string, unknown>>("prediction", (event) => {
      handler(mapPrediction(event.payload));
    }),
  onSnapback: (handler: (payload: Record<string, unknown>) => void) =>
    listen<Record<string, unknown>>("snapback", (event) => handler(event.payload)),
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

export const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max);

export const formatPercent = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  const pct = clamp(value, 0, 1) * 100;
  return `${pct.toFixed(1)}%`;
};

export const formatScore = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  const score = clamp(value, 0, 100);
  return score.toFixed(1);
};

export const formatTime = (isoString: string | null | undefined) => {
  if (!isoString) return "--";
  const date = new Date(isoString);
  if (Number.isNaN(date.getTime())) return "--";
  return date.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
};

export const riskLevel = (risk: number | null | undefined): RiskLevel => {
  if (risk === null || risk === undefined || Number.isNaN(risk)) return "unknown";
  if (risk >= 0.7) return "high";
  if (risk >= 0.4) return "medium";
  return "low";
};

export const riskLabel = (risk: number | null | undefined) => {
  const level = riskLevel(risk);
  if (level === "high") return "High risk";
  if (level === "medium") return "Medium risk";
  if (level === "low") return "Low risk";
  return "Unknown";
};

export const focusStateLabel = (state: string | null | undefined) => {
  switch (state) {
    case "DEEP_FOCUS":
      return "Deep work";
    case "PRODUCTIVE":
      return "Productive";
    case "PSEUDO_PRODUCTIVE":
      return "Drift";
    case "DISTRACTED":
      return "Distracted";
    default:
      return "Unknown";
  }
};

export const nextBackoffDelay = (attempt: number) => {
  const safeAttempt = Math.max(0, attempt);
  const baseMs = 500;
  const maxMs = 10000;
  const delay = baseMs * Math.pow(2, safeAttempt);
  return Math.min(delay, maxMs);
};
