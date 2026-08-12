import { invoke, listen } from "./bridge";
import { mapActivityDeletionResult } from "./activityDeletion";

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
  mapSessionSummary,
  mapSetupSteps,
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
  timestamp: string;
  modelId: string;
  // Which rule decided focusState (ADR-0004): "model" when the classifier's argmax stood,
  // else "risk" | "thrash" | "block" | "drift". null on rows from before verdicts carried
  // provenance — unknown, not "model".
  stateSource: string | null;
};

export type SessionRecord = {
  sessionId: string;
  goal: string;
  status: string;
  focusMode: string;
  startedAt: string | null;
  endedAt: string | null;
  // Roadmap 2.14. null means the question was never answered, which is what Skip leaves
  // behind — deliberately not "" so the UI can tell "skipped" from "answered with nothing".
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
  modelId: string | null;
};

export type ModelDeploymentHealth = {
  state: "ok" | "degraded";
  message: string | null;
  preservedPaths: string[];
  retryCleanupAvailable: boolean;
  rollbackAvailable: boolean;
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
   * Time the user was actually present (Roadmap 7.23 / ADR-0005).
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

// Roadmap 9.14. What a candidate file turned out to be, so the confirmation can state both
// halves of the trade: what is adopted and what is replaced.
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
  /**
   * Roadmap 10.13. Seconds of the longest unbroken focused stretch. It replaced a count of
   * consecutive non-DISTRACTED prediction rows shown under the time-like label "Focus streak";
   * rows are not time, and predictions arrive on input rather than on a clock.
   */
  longestFocusSecs: number;
};

export type PomodoroPhase = "work" | "shortBreak" | "longBreak";

export type PomodoroStatus = {
  running: boolean;
  // Roadmap 2.13. Both read as "not counting down", and they are not the same thing: paused
  // is the user's choice and resumes where it stopped; awaiting means a phase ended and the
  // next one is deliberately waiting to be started.
  paused: boolean;
  awaitingAcknowledgement: boolean;
  phase: PomodoroPhase;
  completedWorkIntervals: number;
  remainingMs: number;
};

// Roadmap 2.10. One status model, derived in the backend so the header and the tray cannot
// disagree about the only question this app must never be vague on.
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
};

// Roadmap 2.19. A target of 0 means "not set" -- there is no separate enabled flag to drift
// out of step with the number.
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
  /** Roadmap 7.23. Seconds without input before a session stops counting as attended. */
  idleThresholdSecs: number;
  pomodoro: PomodoroConfig;
};

export type PrivacySettings = {
  privateMode: boolean;
  excludedApps: string[];
  localOnly: boolean;
};

// Roadmap 7.6. The counts travel with the path so the UI can say "12 sessions, 340 windows"
// rather than only naming a file — the difference between "it worked" and "here is what it
// holds", which is the whole point of a legible export.
export type MyDataExportResult = {
  outputPath: string;
  sessionCount: number;
  windowCount: number;
  /** Roadmap 2.15. Distraction episodes recorded during the exported sessions. */
  episodeCount: number;
  /**
   * Roadmap 9.16. Per-record-type omissions. Both are zero now that the export is complete;
   * they exist so a reintroduced cap has to say which record type it dropped.
   */
  omittedSessions: number;
  omittedWindows: number;
  /** Derived from the two counts above, never stored on its own. */
  truncated: boolean;
  /** Body checksum, also written into the file, so a cut-short copy is detectable. */
  checksum: string;
};

// Roadmap 7.6. `opened` and `supported` are separate answers: an unsupported platform never
// opens anything, but a supported one can still be refused by the OS, and the UI says
// different things about "this build cannot" and "that did not work this time".
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

export type SummaryWindow = "day" | "week" | "7d" | "30d" | "all" | "custom";

export type ReviewWindowRequest = {
  window: string;
  since?: string;
};

export type SummaryReport = {
  window: SummaryWindow;
  generatedAt: string;
  sessionCount: number;
  completedSessionCount: number;
  focusSeconds: number;
  sampleCount: number;
  avgFocusScore: number;
  distractedFraction: number;
  /** Roadmap 10.13. Seconds, not a row count. See FocusSummary.longestFocusSecs. */
  longestFocusSecs: number;
  topContextApp: string;
  /**
   * Roadmap 2.19. Durable attended seconds for the Review comparison window — never wall-clock
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
  deployReady: boolean;
  message: string;
  onnxExported: boolean;
  metrics: Record<string, number> | null;
  qualityGatePassed?: boolean;
  qualityGateReason?: string;
  logTail: string;
};

export type RollbackClassifierModelResult = {
  success: boolean;
  message: string;
  modelId: string | null;
  classifier: ClassifierStatus;
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
  exportSupportBundle: () =>
    invoke<SupportBundleExportResult>("export_support_bundle"),
  getLatestPrediction: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_latest_prediction");
    return raw ? mapPrediction(raw) : null;
  },
  getPredictionHistory: async (limit = 8) => {
    const rows = await invoke<Record<string, unknown>[]>("get_prediction_history", { limit });
    return rows.map(mapPrediction);
  },
  getFocusSummary: async (range?: ReviewWindowRequest | { limit?: number }) => {
    const args =
      range && "window" in range
        ? { window: range.window, since: range.since }
        : { limit: (range as { limit?: number } | undefined)?.limit ?? 200 };
    const raw = await invoke<Record<string, unknown>>("get_focus_summary", args);
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
  // Roadmap 2.13. Each returns the status the timer actually reached; a control that does not
  // apply in the current state is a no-op, not an error.
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
  // Roadmap 2.14. Pass null (or omit) for an answer the user skipped or cleared; the backend
  // trims, treats blank as unanswered, and returns the saved row so the caller renders what
  // was actually stored.
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
  getSummaryReport: async (range: ReviewWindowRequest = { window: "day" }) => {
    const raw = await invoke<Record<string, unknown> | null>("get_summary_report", range);
    return mapSummaryReport(raw ?? {});
  },
  exportSummaryReport: async (range: ReviewWindowRequest) => {
    const raw = await invoke<Record<string, unknown>>("export_summary_report", range);
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
  // Resolves false when the session was already gone — the caller should refresh its list
  // rather than report a successful delete for a row that no longer existed.
  deleteSession: (sessionId: string) =>
    invoke<boolean>("delete_session", { sessionId }),
  exportMyData: async () => {
    const raw = await invoke<Record<string, unknown>>("export_my_data");
    return {
      outputPath: typeof raw.outputPath === "string" ? raw.outputPath : "",
      sessionCount: Number(raw.sessionCount ?? 0),
      windowCount: Number(raw.windowCount ?? 0),
      episodeCount: Number(raw.episodeCount ?? 0),
      omittedSessions: Number(raw.omittedSessions ?? 0),
      omittedWindows: Number(raw.omittedWindows ?? 0),
      truncated: Boolean(raw.truncated),
      checksum: typeof raw.checksum === "string" ? raw.checksum : "",
    } satisfies MyDataExportResult;
  },
  // Roadmap 9.14. Read-only: it reports whether a file could be imported and what it holds,
  // so the confirmation can name what is being adopted as well as what is being replaced.
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
  dismissSnapback: () => invoke("dismiss_snapback"),
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
  /**
   * Sustained work with no session running (Roadmap 2.7 / ADR-0005). Nothing is recorded
   * without a session, so this is the only signal a user gets that their work is going
   * unmeasured. It asks; it never starts a session on their behalf.
   */
  onUntrackedWork: (handler: (payload: { message: string }) => void) =>
    listen<{ message: string }>("untracked_work", (event) => handler(event.payload)),
  /**
   * Whether the user has gone away or come back (Roadmap 7.23 / ADR-0005). The engine has
   * emitted this since idle detection landed; nothing consumed it, so an active session that
   * was actually paused still displayed as running.
   */
  onIdle: (handler: (payload: { idle: boolean }) => void) =>
    listen<{ idle: boolean }>("idle", (event) => handler(event.payload)),
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
  explainPrediction,
  clamp,
  focusStateLabel,
  formatPercent,
  formatPercentCoarse,
  formatPomodoroRemaining,
  formatScore,
  formatScoreCoarse,
  formatTime,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
  verdictLevel,
} from "./utils";

export type { VerdictExplanation } from "./utils";
