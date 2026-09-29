import { ALERT_CHANNELS } from "./alertDelivery";
import type { AlertChannel, AlertDeliverySettings } from "./alertDelivery";
import type {
  AppSettings,
  AnalyticsSummary,
  SummaryExportResult,
  SummaryReport,
  SummaryWindow,
  GoalCategory,
  AutostartStatus,
  AppRuleKind,
  AppRuleRecord,
  ClassifierStatus,
  ContextSnapshot,
  DailySummary,
  DailySummaryDay,
  DiagnosticsSnapshot,
  ExportTrainingResult,
  FocusSummary,
  HealthStatus,
  RuntimeMetrics,
  ModelDeploymentHealth,
  PermissionStatus,
  PomodoroPhase,
  PomodoroStatus,
  AttendedProgress,
  RecordingState,
  RecordingStatus,
  PrivacySettings,
  PredictionRecord,
  SessionRecap,
  SessionLongestSnapback,
  SessionRecord,
  SessionSummary,
  SnapbackPayload,
  TrainFromExportResult,
  TrainingDeployStatus,
  RollbackClassifierModelResult,
} from "./api";
import { normalizeIdleThresholdSecs } from "./idleThreshold";

const FOCUS_MODE_VALUES = new Set(["deep", "normal", "recovery"]);

function normalizeFocusMode(value: unknown): string {
  const mode = String(value ?? "normal").toLowerCase();
  return FOCUS_MODE_VALUES.has(mode) ? mode : "normal";
}

const POMODORO_PHASE_VALUES: PomodoroPhase[] = ["work", "shortBreak", "longBreak"];

function normalizePomodoroPhase(value: unknown): PomodoroPhase {
  const phase = String(value ?? "work");
  return (POMODORO_PHASE_VALUES as string[]).includes(phase)
    ? (phase as PomodoroPhase)
    : "work";
}

// ADR-0007. `null` means "no such moment" and must survive as null -- a session with no
// `endedAtMs` is still running, and coercing that to 0 would date it to 1970 and report it as
// finished. Number(null) is 0, so the null check cannot be folded into the conversion.
function toMsOrNull(value: unknown): number | null {
  if (value === null || value === undefined) return null;
  const ms = Number(value);
  return Number.isFinite(ms) ? ms : null;
}

// Roadmap 2.16. Channels arrive as an array of names, so an unknown one from a newer build is
// dropped rather than taking the whole object down with it -- the same tolerance the native
// from_json has, for the same reason.
function mapAlertChannels(raw: unknown, fallback: AlertChannel[]): AlertChannel[] {
  if (!Array.isArray(raw)) return fallback;
  return ALERT_CHANNELS.filter((channel) => raw.includes(channel));
}

export function mapAlertDelivery(raw: unknown): AlertDeliverySettings {
  const source = (raw ?? {}) as Record<string, unknown>;
  return {
    snapback: mapAlertChannels(source.snapback, ["overlay"]),
    hyperfocus: mapAlertChannels(source.hyperfocus, ["native"]),
    pomodoro: mapAlertChannels(source.pomodoro, ["inApp"]),
    preview: source.preview === "generic" ? "generic" : "detailed",
    quietHoursEnabled: source.quietHoursEnabled === true,
    quietHoursStartMin: Number(source.quietHoursStartMin ?? 22 * 60),
    quietHoursEndMin: Number(source.quietHoursEndMin ?? 7 * 60),
    snoozedUntilWallMs: Number(source.snoozedUntilWallMs ?? 0),
  };
}

export function mapSettings(raw: Record<string, unknown>): AppSettings {
  const pomodoro = (raw.pomodoro ?? {}) as Record<string, unknown>;
  return {
    alerts: mapAlertDelivery(raw.alerts),
    defaultFocusMode: normalizeFocusMode(
      raw.defaultFocusMode,
    ),
    idleThresholdSecs: normalizeIdleThresholdSecs(
      raw.idleThresholdSecs,
    ),
    pomodoro: {
      workMs: Number(pomodoro.workMs ?? 25 * 60 * 1000),
      shortBreakMs: Number(pomodoro.shortBreakMs ?? 5 * 60 * 1000),
      longBreakMs: Number(pomodoro.longBreakMs ?? 15 * 60 * 1000),
      intervalsBeforeLongBreak: Number(
        pomodoro.intervalsBeforeLongBreak ?? 4,
      ),
      autoStartNextPhase: Boolean(
        pomodoro.autoStartNextPhase ?? true,
      ),
    },
  };
}

export function mapAutostartStatus(raw: Record<string, unknown>): AutostartStatus {
  return {
    enabled: Boolean(raw.enabled ?? false),
    supported: Boolean(raw.supported ?? false),
  };
}

export function mapPrivacySettings(raw: Record<string, unknown>): PrivacySettings {
  const exclusions = raw.excludedApps;
  return {
    privateMode: Boolean(raw.privateMode ?? false),
    excludedApps: Array.isArray(exclusions) ? exclusions.map((value) => String(value)) : [],
    localOnly: Boolean(raw.localOnly ?? true),
  };
}

export function mapAnalyticsSummary(raw: Record<string, unknown>): AnalyticsSummary {
  const hourlyRaw = Array.isArray(raw.hourly) ? raw.hourly : [];
  const topAppsValue = raw.topApps;
  const topAppsRaw = Array.isArray(topAppsValue) ? topAppsValue : [];
  return {
    sampleCount: Number(raw.sampleCount ?? 0),
    avgFocusScore: Number(raw.avgFocusScore ?? 0),
    productiveSessionStreak: Number(
      raw.productiveSessionStreak ?? 0,
    ),
    hourly: hourlyRaw.map((value) => {
      const row = (value ?? {}) as Record<string, unknown>;
      return {
        hour: Number(row.hour ?? 0),
        sampleCount: Number(row.sampleCount ?? 0),
        avgFocusScore: Number(row.avgFocusScore ?? 0),
        distractedFraction: Number(row.distractedFraction ?? 0),
      };
    }),
    topApps: topAppsRaw.map((value) => {
      const row = (value ?? {}) as Record<string, unknown>;
      return {
        appName: String(row.appName ?? ""),
        windowCount: Number(row.windowCount ?? 0),
      };
    }),
  };
}

export function mapDailySummary(raw: Record<string, unknown>): DailySummary {
  const daysRaw = Array.isArray(raw.days) ? raw.days : [];
  return {
    window: String(raw.window ?? "7d"),
    generatedAtMs: Number(raw.generatedAtMs ?? 0),
    capped: Boolean(raw.capped ?? false),
    days: daysRaw.map((value): DailySummaryDay => {
      const row = (value ?? {}) as Record<string, unknown>;
      return {
        day: String(row.day ?? ""),
        attendedSecs: Number(row.attendedSecs ?? 0),
        focusedSecs: Number(row.focusedSecs ?? 0),
        deepFocusSecs: Number(row.deepFocusSecs ?? 0),
        avgFocusScore: Number(row.avgFocusScore ?? 0),
        sampleCount: Number(row.sampleCount ?? 0),
        sessionCount: Number(row.sessionCount ?? 0),
        snapbackCount: Number(row.snapbackCount ?? 0),
      };
    }),
  };
}

export function mapSummaryReport(raw: Record<string, unknown>): SummaryReport {
  const window = String(raw.window ?? "day");
  const allowed: SummaryWindow[] = ["day", "week", "7d", "30d", "all", "custom"];
  const normalized = (allowed.includes(window as SummaryWindow)
    ? window
    : "day") as SummaryWindow;
  return {
    window: normalized,
    generatedAtMs: Number(raw.generatedAtMs ?? 0),
    sessionCount: Number(raw.sessionCount ?? 0),
    completedSessionCount: Number(
      raw.completedSessionCount ?? 0,
    ),
    focusSeconds: Number(raw.focusSeconds ?? 0),
    sessionLimit: Number(raw.sessionLimit ?? 0),
    sessionsTruncated: Boolean(raw.sessionsTruncated ?? false),
    sampleCount: Number(raw.sampleCount ?? 0),
    avgFocusScore: Number(raw.avgFocusScore ?? 0),
    distractedFraction: Number(raw.distractedFraction ?? 0),
    longestFocusSecs: Number(raw.longestFocusSecs ?? 0),
    topContextApp: String(raw.topContextApp ?? ""),
    attendedSeconds: Number(raw.attendedSeconds ?? 0),
    plannedMins: Number(raw.plannedMins ?? 0),
  };
}

export function mapSummaryExportResult(raw: Record<string, unknown>): SummaryExportResult {
  return {
    window: String(raw.window ?? "day") === "week" ? "week" : "day",
    outputPath: String(raw.outputPath ?? ""),
  };
}

export function mapGoalCategories(raw: Record<string, unknown>[]): GoalCategory[] {
  return raw.map((value) => {
    const row = value ?? {};
    const keywords = row.keywords;
    return {
      name: String(row.name ?? ""),
      keywords: Array.isArray(keywords) ? keywords.map((keyword) => String(keyword)) : [],
    };
  });
}

export function mapContextSnapshot(raw: Record<string, unknown>): ContextSnapshot {
  return {
    appName: String(raw.appName ?? ""),
    windowTitle: String(raw.windowTitle ?? ""),
    fileHint: String(raw.fileHint ?? ""),
    projectHint: String(raw.projectHint ?? ""),
    summary: String(raw.summary ?? ""),
    timestampMs: Number(raw.timestampMs ?? 0),
  };
}

export function mapSetupSteps(raw: Record<string, unknown>): string[] {
  const steps = raw.setupSteps;
  return Array.isArray(steps) ? steps.map((step: unknown) => String(step)) : [];
}

export function mapPermissionStatus(raw: Record<string, unknown>): PermissionStatus {
  return {
    captureAvailable: Boolean(raw.captureAvailable ?? false),
    captureProbeConfirmed: Boolean(
      raw.captureProbeConfirmed ?? false,
    ),
    activeWindowAvailable: Boolean(
      raw.activeWindowAvailable ?? false,
    ),
    message: String(raw.message ?? ""),
    setupSteps: mapSetupSteps(raw),
  };
}

export function mapClassifierStatus(raw: Record<string, unknown>): ClassifierStatus {
  return {
    backend: String(raw.backend ?? "heuristic"),
    onnxRuntimeEnabled: Boolean(raw.onnxRuntimeEnabled ?? false),
    modelPath: (raw.modelPath ?? null) as string | null,
    modelId: (raw.modelId ?? null) as string | null,
    inferenceDegraded: Boolean(raw.inferenceDegraded ?? false),
    inferenceFailures: Number(raw.inferenceFailures ?? 0),
  };
}

export function mapModelDeploymentHealth(raw: Record<string, unknown>): ModelDeploymentHealth {
  const preserved = raw.preservedPaths;
  return {
    state: String(raw.state ?? "ok") === "degraded" ? "degraded" : "ok",
    message: (raw.message ?? null) as string | null,
    preservedPaths: Array.isArray(preserved) ? preserved.map((path) => String(path)) : [],
    retryCleanupAvailable: Boolean(
      raw.retryCleanupAvailable ?? false,
    ),
    rollbackAvailable: Boolean(raw.rollbackAvailable ?? false),
  };
}

export function mapRuntimeMetrics(raw: Record<string, unknown>): RuntimeMetrics {
  // One reader for every field, because a counter that fails to map reads as 0 on this side
  // and a 0 contention count is indistinguishable from good news.
  const num = (key: string): number => Number(raw[key] ?? 0);
  return {
    engineWakeups: num("engineWakeups"),
    processCpuMs: num("processCpuMs"),
    captureRingHighWater: num("captureRingHighWater"),
    captureRingCapacity: num("captureRingCapacity"),
    storageLockAcquisitions: num("storageLockAcquisitions"),
    storageLockContended: num("storageLockContended"),
    storageLockHoldP50Us: num("storageLockHoldP50Us"),
    storageLockHoldP95Us: num("storageLockHoldP95Us"),
    storageLockMaxHoldUs: num("storageLockMaxHoldUs"),
    storageLockWaitP95Us: num("storageLockWaitP95Us"),
    storageLockMaxWaitUs: num("storageLockMaxWaitUs"),
    sqliteBusyWaits: num("sqliteBusyWaits"),
    sqliteBusyExhausted: num("sqliteBusyExhausted"),
    sqliteBusyMaxWaitMs: num("sqliteBusyMaxWaitMs"),
  };
}

export function mapHealth(raw: Record<string, unknown>): HealthStatus {
  return {
    status: String(raw.status ?? "offline"),
    captureRunning: Boolean(raw.captureRunning ?? false),
    captureFailed: Boolean(raw.captureFailed ?? false),
    captureFailureReason: (raw.captureFailureReason ??
      null) as string | null,
    overlayFailureReason: (raw.overlayFailureReason ??
      null) as string | null,
    persistenceFailureReason: (raw.persistenceFailureReason ??
      null) as string | null,
    captureEventsDropped: Number(
      raw.captureEventsDropped ?? 0,
    ),
    captureStalled: Boolean(raw.captureStalled ?? false),
    lastPredictionAgeSecs:
      raw.lastPredictionAgeSecs == null ? null : Number(raw.lastPredictionAgeSecs),
    predictionSuppressionReason: String(
      raw.predictionSuppressionReason ?? "none",
    ),
    permissions: mapPermissionStatus(
      (raw.permissions as Record<string, unknown>) ?? {},
    ),
    classifier: mapClassifierStatus(
      (raw.classifier as Record<string, unknown>) ?? {},
    ),
    modelDeployment: mapModelDeploymentHealth(
      (raw.modelDeployment as Record<string, unknown>) ?? {},
    ),
    runtime: mapRuntimeMetrics((raw.runtime as Record<string, unknown>) ?? {}),
    developerToolsEnabled: Boolean(
      raw.developerToolsEnabled ?? false,
    ),
  };
}

export function mapDiagnosticsSnapshot(raw: Record<string, unknown>): DiagnosticsSnapshot {
  const logs = raw.recentLogs;
  return {
    version: String(raw.version ?? "0.0.0-dev"),
    health: mapHealth((raw.health as Record<string, unknown>) ?? {}),
    recentLogs: Array.isArray(logs) ? logs.map((line) => String(line)) : [],
    supportBundlePrivacyNotice: String(raw.supportBundlePrivacyNotice ?? ""),
  };
}

export function mapAppRule(raw: Record<string, unknown>): AppRuleRecord {
  return {
    id: Number(raw.id ?? 0),
    pattern: String(raw.pattern ?? ""),
    ruleType: String(raw.ruleType ?? "allow") as AppRuleKind,
    note: (raw.note ?? null) as string | null,
    createdAtMs: Number(raw.createdAtMs ?? 0),
    updatedAtMs: Number(raw.updatedAtMs ?? 0),
  };
}

export function mapPrediction(raw: Record<string, unknown>): PredictionRecord {
  return {
    sessionId: String(raw.sessionId ?? ""),
    focusScore: Number(raw.focusScore ?? 0),
    distractionRisk: Number(raw.distractionRisk ?? 0),
    focusState: String(raw.focusState ?? "UNKNOWN"),
    thrashScore: Number(raw.thrashScore ?? 0),
    driftScore: Number(raw.driftScore ?? 0),
    goalAlignment: Number(raw.goalAlignment ?? 0.5),
    timestampMs: Number(raw.timestampMs ?? 0),
    modelId: String(raw.modelId ?? "heuristic:snapback-features-v1-31"),
    stateSource: (raw.stateSource ?? null) as string | null,
  };
}

export function mapSession(raw: Record<string, unknown>): SessionRecord {
  return {
    sessionId: String(raw.sessionId ?? ""),
    goal: String(raw.goal ?? ""),
    status: String(raw.status ?? ""),
    focusMode: String(raw.focusMode ?? "normal"),
    startedAtMs: toMsOrNull(raw.startedAtMs),
    endedAtMs: toMsOrNull(raw.endedAtMs),
    reflectionDone: (raw.reflectionDone ?? null) as string | null,
    reflectionNextStep: (raw.reflectionNextStep ?? null) as
      | string
      | null,
  };
}

function activeSecsOf(raw: Record<string, unknown>): number | null {
  const value = raw.activeSecs;
  if (value === null || value === undefined) return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

export function mapSessionRecap(raw: Record<string, unknown>): SessionRecap {
  return {
    sessionId: String(raw.sessionId ?? ""),
    goal: String(raw.goal ?? ""),
    durationSecs: Number(raw.durationSecs ?? 0),
    // Not `?? 0`: null and absent both mean "not measured", and collapsing them to 0 would
    // report every pre-7.23 session as fully unattended.
    activeSecs: activeSecsOf(raw),
    avgFocusScore: Number(raw.avgFocusScore ?? 0),
    avgDistractionRisk: Number(raw.avgDistractionRisk ?? 0),
    snapbackCount: Number(raw.snapbackCount ?? 0),
    thrashSpikes: Number(raw.thrashSpikes ?? 0),
    deepFocusPct: Number(raw.deepFocusPct ?? 0),
  };
}

export function mapSessionLongestSnapback(raw: Record<string, unknown>): SessionLongestSnapback {
  return {
    durationSecs: Number(raw.durationSecs ?? 0),
    returnAppName: String(raw.returnAppName ?? ""),
  };
}

export function mapFocusSummary(raw: Record<string, unknown>): FocusSummary {
  return {
    sampleCount: Number(raw.sampleCount ?? 0),
    avgFocusScore: Number(raw.avgFocusScore ?? 0),
    peakFocusScore: Number(raw.peakFocusScore ?? 0),
    distractedSamples: Number(raw.distractedSamples ?? 0),
    distractedFraction: Number(raw.distractedFraction ?? 0),
    longestFocusSecs: Number(raw.longestFocusSecs ?? 0),
  };
}

const RECORDING_STATES: RecordingState[] = [
  "blocked",
  "pausedPrivate",
  "noSession",
  "pausedIdle",
  "recording",
];

export function mapRecordingStatus(raw: Record<string, unknown>): RecordingStatus {
  const state = String(raw.state ?? "blocked");
  return {
    // An unknown state reads as "blocked" rather than "recording": if the two ends disagree
    // about what is happening, the safe thing to show is that nothing is being captured.
    state: (RECORDING_STATES as string[]).includes(state)
      ? (state as RecordingState)
      : "blocked",
    privatePauseRemainingMs: Number(
      raw.privatePauseRemainingMs ?? 0,
    ),
    alertSnoozeRemainingMs: Number(
      raw.alertSnoozeRemainingMs ?? 0,
    ),
  };
}

export function mapAttendedProgress(raw: Record<string, unknown>): AttendedProgress {
  return {
    dailyTargetMins: Number(raw.dailyTargetMins ?? 0),
    dailyActualMins: Number(raw.dailyActualMins ?? 0),
    weeklyTargetMins: Number(raw.weeklyTargetMins ?? 0),
    weeklyActualMins: Number(raw.weeklyActualMins ?? 0),
  };
}

export function mapPomodoroStatus(raw: Record<string, unknown>): PomodoroStatus {
  return {
    running: Boolean(raw.running ?? false),
    paused: Boolean(raw.paused ?? false),
    awaitingAcknowledgement: Boolean(
      raw.awaitingAcknowledgement ?? false,
    ),
    phase: normalizePomodoroPhase(raw.phase),
    completedWorkIntervals: Number(
      raw.completedWorkIntervals ?? 0,
    ),
    remainingMs: Number(raw.remainingMs ?? 0),
  };
}

export function mapSessionSummary(raw: Record<string, unknown>): SessionSummary {
  return {
    record: mapSession((raw.record ?? {}) as Record<string, unknown>),
    recap: mapSessionRecap((raw.recap ?? {}) as Record<string, unknown>),
  };
}

export function mapExportTrainingResult(raw: Record<string, unknown>): ExportTrainingResult {
  return {
    outputDir: String(raw.outputDir ?? ""),
    featuresPath: String(raw.featuresPath ?? ""),
    labelsPath: String(raw.labelsPath ?? ""),
    featureCount: Number(raw.featureCount ?? 0),
    labelCount: Number(raw.labelCount ?? 0),
  };
}

export function mapTrainingDeployStatus(raw: Record<string, unknown>): TrainingDeployStatus {
  const labelBreakdownRaw =
    (raw.labelBreakdown ?? {}) as Record<string, unknown>;
  const labelBreakdown: Record<string, number> = {};
  for (const [key, value] of Object.entries(labelBreakdownRaw)) {
    labelBreakdown[key] = Number(value);
  }
  const metricsRaw = (raw.metrics ?? null) as Record<string, unknown> | null;
  let metrics: Record<string, number> | null = null;
  if (metricsRaw && typeof metricsRaw === "object" && !Array.isArray(metricsRaw)) {
    metrics = {};
    for (const [key, value] of Object.entries(metricsRaw)) {
      metrics[key] = Number(value);
    }
  }
  const qualityGateRaw = (raw.qualityGate ?? {}) as Record<string, unknown>;
  return {
    exportDir: String(raw.exportDir ?? ""),
    featureCount: Number(raw.featureCount ?? 0),
    labelCount: Number(raw.labelCount ?? 0),
    labelBreakdown,
    hasExport: Boolean(raw.hasExport ?? false),
    modelOnnxExists: Boolean(raw.modelOnnxExists ?? false),
    metricsExists: Boolean(raw.metricsExists ?? false),
    metrics,
    qualityGate: {
      passed: Boolean(qualityGateRaw.passed ?? false),
      metric: String(qualityGateRaw.metric ?? ""),
      candidateScore: Number(qualityGateRaw.candidateScore ?? 0),
      threshold: Number(qualityGateRaw.threshold ?? 0),
      reason: String(qualityGateRaw.reason ?? ""),
    },
    rollbackAvailable: Boolean(raw.rollbackAvailable ?? false),
    pythonAvailable: Boolean(raw.pythonAvailable ?? false),
    repoPath: (raw.repoPath ?? null) as string | null,
    repoConfigured: Boolean(raw.repoConfigured ?? false),
    pipelineCommand: String(raw.pipelineCommand ?? ""),
  };
}

export function mapTrainFromExportResult(raw: Record<string, unknown>): TrainFromExportResult {
  const metricsRaw = raw.metrics;
  let metrics: Record<string, number> | null = null;
  if (metricsRaw && typeof metricsRaw === "object" && !Array.isArray(metricsRaw)) {
    metrics = {};
    for (const [key, value] of Object.entries(metricsRaw as Record<string, unknown>)) {
      metrics[key] = Number(value);
    }
  }

  return {
    success: Boolean(raw.success ?? false),
    trainingSucceeded: Boolean(raw.trainingSucceeded ?? false),
    cancelled: Boolean(raw.cancelled ?? false),
    deployReady: Boolean(
      raw.deployReady ?? false,
    ),
    message: String(raw.message ?? ""),
    onnxExported: Boolean(raw.onnxExported ?? false),
    metrics,
    qualityGatePassed: Boolean(raw.qualityGatePassed ?? false),
    qualityGateReason: String(raw.qualityGateReason ?? ""),
    logTail: String(raw.logTail ?? ""),
  };
}

export function mapRollbackClassifierModelResult(
  raw: Record<string, unknown>,
): RollbackClassifierModelResult {
  return {
    success: Boolean(raw.success ?? false),
    message: String(raw.message ?? ""),
    modelId: (raw.modelId ?? null) as string | null,
    classifier: mapClassifierStatus((raw.classifier as Record<string, unknown>) ?? {}),
  };
}

export function mapSnapbackPayload(raw: Record<string, unknown>): SnapbackPayload {
  return {
    summary: String(raw.summary ?? "Previous task"),
    appName: String(raw.appName ?? ""),
    windowTitle: String(raw.windowTitle ?? ""),
    fileHint: String(raw.fileHint ?? ""),
    distractionDurationSecs: Number(
      raw.distractionDurationSecs ?? 0,
    ),
  };
}
