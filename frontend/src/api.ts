import { deliversInApp } from "./alertDelivery";
import { alertDestination, type AlertDestination } from "./alertDestination";
import type { AlertDeliverySettings } from "./alertDelivery";
import { invoke, listen } from "./bridge";
import { mapActivityDeletionResult } from "./activityDeletion";

import {
  mapAppRule,
  mapAnalyticsSummary,
  mapAutostartStatus,
  mapClassifierStatus,
  mapContextSnapshot,
  mapDailySummary,
  mapDiagnosticsSnapshot,
  mapExportTrainingResult,
  mapFocusSummary,
  mapHealth,
  mapPermissionStatus,
  mapAttendedProgress,
  mapRecordingStatus,
  mapPomodoroStatus,
  mapPrivacySettings,
  mapSummaryReport,
  mapSummaryExportResult,
  mapGoalCategories,
  mapModelDeploymentHealth,
  mapPrediction,
  mapSettings,
  mapSession,
  mapSessionRecap,
  mapSessionLongestSnapback,
  mapSessionSummary,
  mapSnapbackPayload,
  mapTrainFromExportResult,
  mapTrainingDeployStatus,
  mapRollbackClassifierModelResult,
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
  timestampMs: number;
  modelId: string;
  // Which rule decided focusState (ADR-0004): "model", or "risk" | "thrash" | "block" |
  // "drift". null means unknown (older rows).
  stateSource: string | null;
};

export type SessionRecord = {
  sessionId: string;
  goal: string;
  status: string;
  focusMode: string;
  startedAtMs: number | null;
  endedAtMs: number | null;
  // null means unanswered (what Skip leaves), distinct from "".
  reflectionDone: string | null;
  reflectionNextStep: string | null;
};

export type PermissionStatus = {
  captureAvailable: boolean;
  captureProbeConfirmed: boolean;
  activeWindowAvailable: boolean;
  message: string;
  setupSteps: string[];
};

/** Payload of the `persistence-failed` event (see fixtures/ipc_commands.json). */
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
  /** The backend that made the most recent prediction — "heuristic" while a loaded model is degraded. */
  backend: string;
  onnxRuntimeEnabled: boolean;
  modelPath: string | null;
  modelId: string | null;
  /** A model is loaded but its last inference failed or was rejected; predictions are heuristic. */
  inferenceDegraded: boolean;
  /** Failed or rejected inferences since the model was loaded. */
  inferenceFailures: number;
};

export type ModelDeploymentHealth = {
  state: "ok" | "degraded";
  message: string | null;
  preservedPaths: string[];
  retryCleanupAvailable: boolean;
  rollbackAvailable: boolean;
};

/** What the process costs itself, carried on health so it travels in the
 *  support bundle. Engineering figures, not user-facing ones: nothing renders these today.
 *
 *  The `...P50Us` / `...P95Us` fields are histogram bucket **upper bounds**, not measured
 *  values (see `ranked_mutex.hpp`). Anything that ever displays one has to say "<=". The
 *  exact tail is the `...Max...` field beside it. */
export type RuntimeMetrics = {
  persistenceFailures: number;
  persistenceDroppedPredictions: number;
  engineWakeups: number;
  engineMaxDrainMs: number;
  maintenancePending: boolean;
  maintenanceRunning: boolean;
  maintenanceRowsDeleted: number;
  maintenanceElapsedMs: number;
  maintenanceResult: string;
  processCpuMs: number;
  captureRingHighWater: number;
  captureRingCapacity: number;
  storageLockAcquisitions: number;
  storageLockContended: number;
  storageLockHoldP50Us: number;
  storageLockHoldP95Us: number;
  storageLockMaxHoldUs: number;
  storageLockWaitP95Us: number;
  storageLockMaxWaitUs: number;
  sqliteBusyWaits: number;
  sqliteBusyExhausted: number;
  sqliteBusyMaxWaitMs: number;
};

/** The all-zero value, for the placeholder health a surface renders before the first
 *  get_diagnostics returns. Named rather than spelled out at each use so a field added above
 *  cannot be forgotten in one of them. */
export const EMPTY_RUNTIME_METRICS: RuntimeMetrics = {
  persistenceFailures: 0,
  persistenceDroppedPredictions: 0,
  engineWakeups: 0,
  engineMaxDrainMs: 0,
  maintenancePending: false,
  maintenanceRunning: false,
  maintenanceRowsDeleted: 0,
  maintenanceElapsedMs: 0,
  maintenanceResult: "waiting_for_ui",
  processCpuMs: 0,
  captureRingHighWater: 0,
  captureRingCapacity: 0,
  storageLockAcquisitions: 0,
  storageLockContended: 0,
  storageLockHoldP50Us: 0,
  storageLockHoldP95Us: 0,
  storageLockMaxHoldUs: 0,
  storageLockWaitP95Us: 0,
  storageLockMaxWaitUs: 0,
  sqliteBusyWaits: 0,
  sqliteBusyExhausted: 0,
  sqliteBusyMaxWaitMs: 0,
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
  lastPredictionAgeSecs: number | null;
  predictionSuppressionReason: string;
  permissions: PermissionStatus;
  classifier: ClassifierStatus;
  modelDeployment: ModelDeploymentHealth;
  runtime: RuntimeMetrics;
  developerToolsEnabled: boolean;
};

export type DiagnosticsSnapshot = {
  version: string;
  health: HealthStatus;
  recentLogs: string[];
  supportBundlePrivacyNotice: string;
};

export type SupportBundleExportResult = {
  outputPath: string;
  privacyNotice: string;
};


export type SessionRecap = {
  sessionId: string;
  goal: string;
  /** Wall clock from start to end, including time the user was away. */
  durationSecs: number;
  /**
   * Time the user was actually present (ADR-0005).
   *
   * `null` means "never measured" — sessions recorded before attended time existed — and is
   * deliberately not 0, so the UI can fall back to `durationSecs` instead of telling someone
   * they were present for none of a session that predates the feature.
   */
  activeSecs: number | null;
  avgFocusScore: number;
  avgDistractionRisk: number;
  snapbackCount: number;
  thrashSpikes: number;
  deepFocusPct: number;
};

export type SessionLongestSnapback = {
  durationSecs: number;
  /** The app the user returned to, not the app that caused the detour. */
  returnAppName: string;
};

// What a candidate file is, so the confirmation can state what is adopted and replaced.
export type DataImportCandidate = {
  acceptable: boolean;
  /** Why it was refused, already phrased for display. Empty when acceptable. */
  message: string;
  schemaVersion: number;
  sessionCount: number;
};

export type DataImportStaged = {
  ok: boolean;
  message: string;
  schemaVersion: number;
  sessionCount: number;
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
  /** Seconds of the longest unbroken focused stretch (not a row count). */
  longestFocusSecs: number;
};

export type PomodoroPhase = "work" | "shortBreak" | "longBreak";

export type PomodoroStatus = {
  running: boolean;
  // Paused resumes where it stopped; awaiting means a phase ended and the next is waiting.
  paused: boolean;
  awaitingAcknowledgement: boolean;
  phase: PomodoroPhase;
  completedWorkIntervals: number;
  remainingMs: number;
};

// Derived in the backend so the header and tray cannot disagree.
export type RecordingState =
  | "blocked"
  | "pausedPrivate"
  | "noSession"
  | "pausedIdle"
  | "recording";

export type RecordingStatus = {
  state: RecordingState;
  /** Milliseconds left on a timed privacy pause; 0 when indefinite or not paused. */
  privatePauseRemainingMs: number;
  /**
   * Milliseconds left on an alert snooze; 0 when not snoozed.
   *
   * Reported beside `state`, never instead of it. A snooze silences interventions while
   * recording continues, so `state` still reads "recording" throughout one.
   */
  alertSnoozeRemainingMs: number;
};

// A target of 0 means "not set".
export type AttendedProgress = {
  dailyTargetMins: number;
  dailyActualMins: number;
  weeklyTargetMins: number;
  weeklyActualMins: number;
};

export type PomodoroConfig = {
  workMs: number;
  shortBreakMs: number;
  longBreakMs: number;
  intervalsBeforeLongBreak: number;
  autoStartNextPhase: boolean;
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
  createdAtMs: number;
  updatedAtMs: number;
};

export type ContextSnapshot = {
  appName: string;
  windowTitle: string;
  fileHint: string;
  projectHint: string;
  summary: string;
  timestampMs: number;
};

export type SnapbackPayload = {
  summary: string;
  appName: string;
  windowTitle: string;
  fileHint: string;
  distractionDurationSecs: number;
};

export type FocusTargetResult = {
  ok: boolean;
  message: string;
};

export type FileDialogFilter = {
  name: string;
  pattern: string;
};

export type FileDialogOptions = {
  title?: string;
  defaultPath?: string;
  defaultName?: string;
  filters?: FileDialogFilter[];
};

export type FileDialogResult = {
  ok: boolean;
  cancelled: boolean;
  path: string;
  message: string;
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
  qualityGate?: {
    passed: boolean;
    metric: string;
    candidateScore: number;
    threshold: number;
    reason: string;
  };
  rollbackAvailable?: boolean;
  pythonAvailable: boolean;
  repoPath: string | null;
  repoConfigured: boolean;
  pipelineCommand: string;
};

export type AppSettings = {
  defaultFocusMode: string;
  /** Seconds without input before a session stops counting as attended. */
  idleThresholdSecs: number;
  pomodoro: PomodoroConfig;
  /** When and how an interruption may reach the user. */
  alerts: AlertDeliverySettings;
};

export type PrivacySettings = {
  privateMode: boolean;
  excludedApps: string[];
  localOnly: boolean;
};

// Counts travel with the path so the UI can say what the file holds.
export type MyDataExportResult = {
  outputPath: string;
  sessionCount: number;
  windowCount: number;
  /** Distraction episodes recorded during the exported sessions. */
  episodeCount: number;
  /**
   * Per-record-type omissions. Both are zero now that the export is complete;
   * they exist so a reintroduced cap has to say which record type it dropped.
   */
  omittedSessions: number;
  omittedWindows: number;
  /** Per-session interruption pagination omissions. */
  omittedEpisodes: number;
  /** Derived from the two counts above, never stored on its own. */
  truncated: boolean;
  /** Body checksum, also written into the file, so a cut-short copy is detectable. */
  checksum: string;
};

// "This build cannot" (supported) and "that did not work" (opened) are different answers.
export type OpenDataFolderResult = {
  opened: boolean;
  path: string;
  supported: boolean;
};

export type AnalyticsHour = {
  hour: number;
  sampleCount: number;
  avgFocusScore: number;
  distractedFraction: number;
};

/** One slice of a single session's focus over its own duration. */
export type FocusCurvePoint = {
  startMs: number;
  sampleCount: number;
  avgFocusScore: number;
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

/** One local calendar day of the Review trend series. The *Secs fields are durations
 *  (attended from spans; focused/deep from prediction run gaps), never row counts —
 *  the row count travels separately as sampleCount. Days with no data
 *  are omitted by the backend; charts fill the gaps. */
export type DailySummaryDay = {
  /** Local calendar date, "YYYY-MM-DD" — the bucketing key. */
  day: string;
  attendedSecs: number;
  focusedSecs: number;
  deepFocusSecs: number;
  avgFocusScore: number;
  sampleCount: number;
  sessionCount: number;
  snapbackCount: number;
};

export type DailySummary = {
  window: string;
  generatedAtMs: number;
  /** True when the requested range reached past retention and was clamped to it. */
  capped: boolean;
  days: DailySummaryDay[];
};

export type SummaryWindow = "day" | "week" | "7d" | "30d" | "all" | "custom";

export type ReviewWindowRequest = {
  window: string;
  since?: string;
};

export type SummaryReport = {
  window: SummaryWindow;
  generatedAtMs: number;
  sessionCount: number;
  completedSessionCount: number;
  /**
   * Summed wall-clock duration of the completed sessions in the range, start to end, including
   * idle or distracted time inside them. Not model-focused time and not attended time. The
   * name is historical; label it as session time.
   */
  focusSeconds: number;
  /** The session aggregates read at most this many of the newest sessions. */
  sessionLimit: number;
  /** True when that cap was binding, so the session figures describe only the latest N. */
  sessionsTruncated: boolean;
  sampleCount: number;
  avgFocusScore: number;
  distractedFraction: number;
  /** Seconds, not a row count. See FocusSummary.longestFocusSecs. */
  longestFocusSecs: number;
  topContextApp: string;
  /**
   * Durable attended seconds for the Review comparison window — never wall-clock
   * session-open time. `plannedMins` is 0 when no daily/weekly target applies to this range.
   */
  attendedSeconds: number;
  plannedMins: number;
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
  // The run was ended early (by the user, or by the app shutting down); nothing was deployed.
  cancelled?: boolean;
  deployReady: boolean;
  message: string;
  onnxExported: boolean;
  metrics: Record<string, number> | null;
  qualityGatePassed?: boolean;
  qualityGateReason?: string;
  logTail: string;
};

// One reading of a running training's log, pushed while trainFromExport is pending.
export type TrainingProgressPayload = {
  elapsedMs: number;
  logTail: string;
};

export type RollbackClassifierModelResult = {
  success: boolean;
  message: string;
  modelId: string | null;
  classifier: ClassifierStatus;
};

// A refusal shape (the browser demo's answer to disk access) must throw rather than map to
// defaults that read as success. Native success shapes carry none of these flags.
function throwIfUnavailable(raw: Record<string, unknown>, action: string): void {
  if (raw.ok === false || raw.supported === false || raw.cancelled === true) {
    const message = typeof raw.message === "string" && raw.message ? raw.message : action;
    throw new Error(message);
  }
}

export const api = {
  notifyFrontendReady: () => invoke<void>("notify_frontend_ready"),
  getHealth: async () => {
    const raw = await invoke<Record<string, unknown>>("get_health");
    return mapHealth(raw);
  },
  // Only acceptance-enabled desktop builds call this, to report a real webview round trip.
  reportAcceptanceVerdict: (verdict: Record<string, unknown>) =>
    invoke<{ accepted: boolean }>("report_acceptance_verdict", { verdict }),
  getDiagnostics: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_diagnostics");
    return mapDiagnosticsSnapshot(raw ?? {});
  },
  exportSupportBundle: async () => {
    const raw = await invoke<Record<string, unknown>>("export_support_bundle");
    throwIfUnavailable(raw, "Support bundle export is unavailable.");
    return {
      outputPath: typeof raw.outputPath === "string" ? raw.outputPath : "",
      privacyNotice: typeof raw.privacyNotice === "string" ? raw.privacyNotice : "",
    } satisfies SupportBundleExportResult;
  },
  getLatestPrediction: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_latest_prediction");
    return raw ? mapPrediction(raw) : null;
  },
  getPredictionHistory: async (limit = 8) => {
    const rows = await invoke<Record<string, unknown>[]>("get_prediction_history", { limit });
    return rows.map(mapPrediction);
  },
  getFocusSummary: async (range: ReviewWindowRequest = { window: "day" }) => {
    const raw = await invoke<Record<string, unknown>>("get_focus_summary", {
      window: range.window,
      since: range.since,
    });
    return mapFocusSummary(raw);
  },
  getRecordingStatus: async () => {
    const raw = await invoke<Record<string, unknown>>("get_recording_status");
    return mapRecordingStatus(raw);
  },
  pauseRecordingPrivately: async (minutes: number) => {
    const raw = await invoke<Record<string, unknown>>("pause_recording_privately", { minutes });
    return mapRecordingStatus(raw);
  },
  resumeRecording: async () => {
    const raw = await invoke<Record<string, unknown>>("resume_recording");
    return mapRecordingStatus(raw);
  },
  getAttendedProgress: async () => {
    const raw = await invoke<Record<string, unknown>>("get_attended_progress");
    return mapAttendedProgress(raw);
  },
  setAttendedTargets: async (dailyMins: number, weeklyMins: number) => {
    const raw = await invoke<Record<string, unknown>>("set_attended_targets", {
      dailyMins,
      weeklyMins,
    });
    return mapAttendedProgress(raw);
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
  // Each returns the status reached; an inapplicable control is a no-op, not an error.
  pausePomodoro: async () => {
    const raw = await invoke<Record<string, unknown>>("pause_pomodoro");
    return mapPomodoroStatus(raw);
  },
  resumePomodoro: async () => {
    const raw = await invoke<Record<string, unknown>>("resume_pomodoro");
    return mapPomodoroStatus(raw);
  },
  skipPomodoroPhase: async () => {
    const raw = await invoke<Record<string, unknown>>("skip_pomodoro_phase");
    return mapPomodoroStatus(raw);
  },
  restartPomodoroPhase: async () => {
    const raw = await invoke<Record<string, unknown>>("restart_pomodoro_phase");
    return mapPomodoroStatus(raw);
  },
  acknowledgePomodoroPhase: async () => {
    const raw = await invoke<Record<string, unknown>>("acknowledge_pomodoro_phase");
    return mapPomodoroStatus(raw);
  },
  setPomodoroConfig: async (config: PomodoroConfig) => {
    const raw = await invoke<Record<string, unknown>>("set_pomodoro_config", { config });
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
  // null (or omitted) means skipped or cleared; returns the stored row.
  saveSessionReflection: async (
    sessionId: string,
    done: string | null,
    nextStep: string | null,
  ) => {
    const raw = await invoke<Record<string, unknown>>("save_session_reflection", {
      sessionId,
      done,
      nextStep,
    });
    return mapSession(raw);
  },
  getSessionRecap: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("get_session_recap", { sessionId });
    return mapSessionRecap(raw);
  },
  getSessionAutoLabel: (sessionId: string): Promise<FocusLabel | null> =>
    invoke("get_session_auto_label", { sessionId }),
  getSessionFocusCurve: async (sessionId: string, buckets = 60): Promise<FocusCurvePoint[]> => {
    const rows = await invoke<Record<string, unknown>[]>("get_session_focus_curve", {
      sessionId,
      buckets,
    });
    return rows.map((row) => ({
      startMs: Number(row.startMs ?? 0),
      sampleCount: Number(row.sampleCount ?? 0),
      avgFocusScore: Number(row.avgFocusScore ?? 0),
    }));
  },
  getSessionLongestSnapback: async (sessionId: string): Promise<SessionLongestSnapback | null> => {
    const raw = await invoke<Record<string, unknown> | null>("get_session_longest_snapback", { sessionId });
    if (!raw) return null;
    return mapSessionLongestSnapback(raw);
  },
  getSessionHistory: async (range?: ReviewWindowRequest | { limit?: number }) => {
    const args =
      range && "window" in range
        ? { window: range.window, since: range.since }
        : { limit: (range as { limit?: number } | undefined)?.limit ?? 20 };
    const rows = await invoke<Record<string, unknown>[]>("get_session_history", args);
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
  getAnalytics: async (range: ReviewWindowRequest = { window: "all" }) => {
    const raw = await invoke<Record<string, unknown> | null>("get_analytics", range);
    return mapAnalyticsSummary(raw ?? {});
  },
  getDailySummary: async (range: ReviewWindowRequest = { window: "7d" }) => {
    const raw = await invoke<Record<string, unknown> | null>("get_daily_summary", range);
    return mapDailySummary(raw ?? {});
  },
  getSummaryReport: async (range: ReviewWindowRequest = { window: "day" }) => {
    const raw = await invoke<Record<string, unknown> | null>("get_summary_report", range);
    return mapSummaryReport(raw ?? {});
  },
  exportSummaryReport: async (range: ReviewWindowRequest) => {
    const raw = await invoke<Record<string, unknown>>("export_summary_report", range);
    throwIfUnavailable(raw, "Summary export is unavailable.");
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
  deleteAllActivityData: async () => {
    const raw = await invoke<unknown>("delete_all_activity_data");
    return mapActivityDeletionResult(raw);
  },
  // Resolves false when the session was already gone.
  deleteSession: (sessionId: string) =>
    invoke<boolean>("delete_session", { sessionId }),
  pickOpenFile: (options?: FileDialogOptions) =>
    invoke<FileDialogResult>("pick_open_file", { options: options ?? null }),
  pickSaveFile: (options?: FileDialogOptions) =>
    invoke<FileDialogResult>("pick_save_file", { options: options ?? null }),
  exportMyData: async () => {
    const raw = await invoke<Record<string, unknown>>("export_my_data");
    throwIfUnavailable(raw, "Data export is unavailable.");
    return {
      outputPath: typeof raw.outputPath === "string" ? raw.outputPath : "",
      sessionCount: Number(raw.sessionCount ?? 0),
      windowCount: Number(raw.windowCount ?? 0),
      episodeCount: Number(raw.episodeCount ?? 0),
      omittedSessions: Number(raw.omittedSessions ?? 0),
      omittedWindows: Number(raw.omittedWindows ?? 0),
      omittedEpisodes: Number(raw.omittedEpisodes ?? 0),
      truncated: Boolean(raw.truncated),
      checksum: typeof raw.checksum === "string" ? raw.checksum : "",
    } satisfies MyDataExportResult;
  },
  // Read-only: whether the file can be imported and what it holds.
  inspectDataImport: async (path: string) => {
    const raw = await invoke<Record<string, unknown>>("inspect_data_import", { path });
    return {
      acceptable: Boolean(raw.acceptable),
      message: typeof raw.message === "string" ? raw.message : "",
      schemaVersion: Number(raw.schemaVersion ?? 0),
      sessionCount: Number(raw.sessionCount ?? 0),
    } satisfies DataImportCandidate;
  },
  // Stages rather than applies: the running app holds the database open, so the swap happens
  // at the next launch. `message` says so, and is the only thing the UI shows.
  stageDataImport: async (path: string) => {
    const raw = await invoke<Record<string, unknown>>("stage_data_import", { path });
    return {
      ok: Boolean(raw.ok),
      message: typeof raw.message === "string" ? raw.message : "",
      schemaVersion: Number(raw.schemaVersion ?? 0),
      sessionCount: Number(raw.sessionCount ?? 0),
    } satisfies DataImportStaged;
  },
  cancelDataImport: async () => {
    const raw = await invoke<Record<string, unknown>>("cancel_data_import");
    return { cancelled: Boolean(raw.cancelled), pending: Boolean(raw.pending) };
  },
  getDataImportStatus: async () => {
    const raw = await invoke<Record<string, unknown>>("get_data_import_status");
    return { pending: Boolean(raw.pending) };
  },
  // `path` is populated even when `opened` is false, so a platform without a file-manager
  // backend (or an OS that refused) can still tell the user where their data lives.
  openDataFolder: async () => {
    const raw = await invoke<Record<string, unknown>>("open_data_folder");
    return {
      opened: Boolean(raw.opened),
      path: typeof raw.path === "string" ? raw.path : "",
      supported: Boolean(raw.supported),
    } satisfies OpenDataFolderResult;
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
  setIdleThreshold: async (seconds: number) => {
    const raw = await invoke<Record<string, unknown>>("set_idle_threshold", { seconds });
    return mapSettings(raw ?? {});
  },
  /**
   * Replaces every delivery preference at once. `snoozedUntilWallMs` is ignored
   * by the native side: a snooze belongs to the tray action that started it, and a Settings
   * save must not silently extend or cancel one.
   */
  setAlertDelivery: async (alerts: AlertDeliverySettings) => {
    const raw = await invoke<Record<string, unknown>>("set_alert_delivery", { alerts });
    return mapSettings(raw ?? {});
  },
  snoozeAlerts: async (minutes = 0) => {
    const raw = await invoke<Record<string, unknown>>("snooze_alerts", { minutes });
    return mapRecordingStatus(raw ?? {});
  },
  resumeAlerts: async () => {
    const raw = await invoke<Record<string, unknown>>("resume_alerts");
    return mapRecordingStatus(raw ?? {});
  },
  dismissSnapback: () => invoke("dismiss_snapback"),
  restoreSnapbackTarget: () => invoke<FocusTargetResult>("restore_snapback_target"),
  dismissUntrackedNudge: (minutes = 60) => invoke("dismiss_untracked_nudge", { minutes }),
  reloadClassifierModel: async () => {
    const raw = await invoke<Record<string, unknown>>("reload_classifier_model");
    return mapClassifierStatus(raw);
  },
  rollbackClassifierModel: async () => {
    const raw = await invoke<Record<string, unknown>>("rollback_classifier_model");
    return mapRollbackClassifierModelResult(raw);
  },
  retryModelDeploymentCleanup: async () => {
    const raw = await invoke<Record<string, unknown>>("retry_model_deployment_cleanup");
    return mapModelDeploymentHealth(raw);
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
    throwIfUnavailable(raw, "Training data export is unavailable.");
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
  // Only raises the request; the run reports its own end through trainFromExport's result.
  // `requested` is false when no run was in progress to cancel.
  cancelTraining: async () => {
    const raw = await invoke<Record<string, unknown>>("cancel_training");
    return { requested: Boolean(raw?.requested ?? false) };
  },
  onTrainingProgress: (handler: (payload: TrainingProgressPayload) => void) =>
    listen<Record<string, unknown>>("training-progress", (event) => {
      const raw = event.payload;
      handler({
        elapsedMs: Number(raw.elapsedMs ?? 0),
        logTail: String(raw.logTail ?? ""),
      });
    }),
  onPersistenceRecovered: (handler: () => void) => listen("persistence-recovered", handler),
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
  /**
   * `inApp` says whether this event may raise an in-app alert. It is passed
   * alongside the payload rather than used to drop the event, because a snapback also
   * refreshes the timeline: silencing the alert must not silence the state update.
   */
  onSnapback: (handler: (payload: SnapbackPayload, inApp: boolean) => void) =>
    listen<Record<string, unknown>>("snapback", (event) => {
      handler(mapSnapbackPayload(event.payload), deliversInApp(event.payload));
    }),
  /**
   * Never gated. This event is also how the timer card learns the phase changed, so a user who
   * turned Pomodoro alerts off would otherwise watch their timer freeze at 25:00.
   */
  onPomodoro: (handler: (status: PomodoroStatus) => void) =>
    listen<Record<string, unknown>>("pomodoro", (event) => {
      handler(mapPomodoroStatus(event.payload));
    }),
  onHyperfocus: (handler: (payload: { message: string }, inApp: boolean) => void) =>
    listen<{ message: string }>("hyperfocus", (event) =>
      handler(event.payload, deliversInApp(event.payload)),
    ),
  /**
   * Sustained work with no session running (ADR-0005). Nothing is recorded
   * without a session, so this is the only signal a user gets that their work is going
   * unmeasured. It asks; it never starts a session on their behalf.
   */
  onUntrackedWork: (handler: (payload: { message: string }, inApp: boolean) => void) =>
    listen<{ message: string }>("untracked_work", (event) =>
      handler(event.payload, deliversInApp(event.payload)),
    ),
  /** Whether the user has gone away or come back (ADR-0005). */
  /**
   * A native alert was clicked and the destination is one this side owns.
   *
   * Only fires for destinations the app has to navigate for. "return to work" never arrives
   * here: it is handled natively by restore_snapback_target, which raises another
   * application's window — navigating for it would pull the user back to Snapback at the
   * moment the native side was sending them away from it.
   */
  onAlertAction: (handler: (destination: AlertDestination) => void) =>
    listen<Record<string, unknown>>("alert_action", (event) => {
      handler(alertDestination(event.payload));
    }),
  onIdle: (handler: (payload: { idle: boolean }) => void) =>
    listen<{ idle: boolean }>("idle", (event) => handler(event.payload)),
  /**
   * The native side changed the answer to "am I being recorded?" --
   * a pause, resume, or snooze from the tray, or the private-mode toggle in Settings. The
   * payload is the same shape `getRecordingStatus` returns, so the header applies it rather
   * than asking again.
   */
  onRecordingStatus: (handler: (status: RecordingStatus) => void) =>
    listen<Record<string, unknown>>("recording-status", (event) => {
      handler(mapRecordingStatus(event.payload));
    }),
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
  displayVerdict,
  explainPrediction,
  clamp,
  focusStateLabel,
  formatPercent,
  formatPercentCoarse,
  formatPomodoroRemaining,
  formatScore,
  formatScoreCoarse,
  formatTime,
  isUncertainFocusGuess,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
  verdictLevel,
} from "./utils";

export type { DisplayVerdict, VerdictExplanation } from "./utils";
