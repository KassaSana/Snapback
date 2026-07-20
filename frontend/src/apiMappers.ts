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
  DiagnosticsSnapshot,
  ExportTrainingResult,
  FocusSummary,
  HealthStatus,
  PermissionStatus,
  PomodoroPhase,
  PomodoroStatus,
  PrivacySettings,
  PredictionRecord,
  SessionRecap,
  SessionRecord,
  SessionSummary,
  SnapbackPayload,
  TrainFromExportResult,
  TrainingDeployStatus,
} from "./api";

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

export function mapSettings(raw: Record<string, unknown>): AppSettings {
  return {
    defaultFocusMode: normalizeFocusMode(
      raw.default_focus_mode ?? raw.defaultFocusMode,
    ),
  };
}

export function mapAutostartStatus(raw: Record<string, unknown>): AutostartStatus {
  return {
    enabled: Boolean(raw.enabled ?? false),
    supported: Boolean(raw.supported ?? false),
  };
}

export function mapPrivacySettings(raw: Record<string, unknown>): PrivacySettings {
  const exclusions = raw.excluded_apps ?? raw.excludedApps;
  return {
    privateMode: Boolean(raw.private_mode ?? raw.privateMode ?? false),
    excludedApps: Array.isArray(exclusions) ? exclusions.map((value) => String(value)) : [],
    localOnly: Boolean(raw.local_only ?? raw.localOnly ?? true),
  };
}

export function mapAnalyticsSummary(raw: Record<string, unknown>): AnalyticsSummary {
  const hourlyRaw = Array.isArray(raw.hourly) ? raw.hourly : [];
  const topAppsValue = raw.top_apps ?? raw.topApps;
  const topAppsRaw = Array.isArray(topAppsValue) ? topAppsValue : [];
  return {
    sampleCount: Number(raw.sample_count ?? raw.sampleCount ?? 0),
    avgFocusScore: Number(raw.avg_focus_score ?? raw.avgFocusScore ?? 0),
    productiveSessionStreak: Number(
      raw.productive_session_streak ?? raw.productiveSessionStreak ?? 0,
    ),
    hourly: hourlyRaw.map((value) => {
      const row = (value ?? {}) as Record<string, unknown>;
      return {
        hour: Number(row.hour ?? 0),
        sampleCount: Number(row.sample_count ?? row.sampleCount ?? 0),
        avgFocusScore: Number(row.avg_focus_score ?? row.avgFocusScore ?? 0),
        distractedFraction: Number(row.distracted_fraction ?? row.distractedFraction ?? 0),
      };
    }),
    topApps: topAppsRaw.map((value) => {
      const row = (value ?? {}) as Record<string, unknown>;
      return {
        appName: String(row.app_name ?? row.appName ?? ""),
        windowCount: Number(row.window_count ?? row.windowCount ?? 0),
      };
    }),
  };
}

export function mapSummaryReport(raw: Record<string, unknown>): SummaryReport {
  const window = String(raw.window ?? "day");
  return {
    window: (window === "week" ? "week" : "day") as SummaryWindow,
    generatedAt: String(raw.generated_at ?? raw.generatedAt ?? ""),
    sessionCount: Number(raw.session_count ?? raw.sessionCount ?? 0),
    focusSeconds: Number(raw.focus_seconds ?? raw.focusSeconds ?? 0),
    sampleCount: Number(raw.sample_count ?? raw.sampleCount ?? 0),
    avgFocusScore: Number(raw.avg_focus_score ?? raw.avgFocusScore ?? 0),
    distractedFraction: Number(raw.distracted_fraction ?? raw.distractedFraction ?? 0),
    longestFocusStreak: Number(raw.longest_focus_streak ?? raw.longestFocusStreak ?? 0),
    topContextApp: String(raw.top_context_app ?? raw.topContextApp ?? ""),
  };
}

export function mapSummaryExportResult(raw: Record<string, unknown>): SummaryExportResult {
  return {
    window: String(raw.window ?? "day") === "week" ? "week" : "day",
    outputPath: String(raw.output_path ?? raw.outputPath ?? ""),
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
    appName: String(raw.app_name ?? raw.appName ?? ""),
    windowTitle: String(raw.window_title ?? raw.windowTitle ?? ""),
    fileHint: String(raw.file_hint ?? raw.fileHint ?? ""),
    projectHint: String(raw.project_hint ?? raw.projectHint ?? ""),
    summary: String(raw.summary ?? ""),
    timestamp: String(raw.timestamp ?? ""),
  };
}

export function mapSetupSteps(raw: Record<string, unknown>): string[] {
  const steps = raw.setup_steps ?? raw.setupSteps;
  return Array.isArray(steps) ? steps.map((step: unknown) => String(step)) : [];
}

export function mapPermissionStatus(raw: Record<string, unknown>): PermissionStatus {
  return {
    captureAvailable: Boolean(raw.capture_available ?? raw.captureAvailable ?? false),
    captureProbeConfirmed: Boolean(
      raw.capture_probe_confirmed ?? raw.captureProbeConfirmed ?? false,
    ),
    activeWindowAvailable: Boolean(
      raw.active_window_available ?? raw.activeWindowAvailable ?? false,
    ),
    message: String(raw.message ?? ""),
    setupSteps: mapSetupSteps(raw),
  };
}

export function mapClassifierStatus(raw: Record<string, unknown>): ClassifierStatus {
  return {
    backend: String(raw.backend ?? "heuristic"),
    onnxRuntimeEnabled: Boolean(raw.onnx_runtime_enabled ?? raw.onnxRuntimeEnabled ?? false),
    modelPath: (raw.model_path ?? raw.modelPath ?? null) as string | null,
  };
}

export function mapHealth(raw: Record<string, unknown>): HealthStatus {
  return {
    status: String(raw.status ?? "offline"),
    captureRunning: Boolean(raw.capture_running ?? raw.captureRunning ?? false),
    captureFailed: Boolean(raw.capture_failed ?? raw.captureFailed ?? false),
    captureFailureReason: (raw.capture_failure_reason ??
      raw.captureFailureReason ??
      null) as string | null,
    overlayFailureReason: (raw.overlay_failure_reason ??
      raw.overlayFailureReason ??
      null) as string | null,
    persistenceFailureReason: (raw.persistence_failure_reason ??
      raw.persistenceFailureReason ??
      null) as string | null,
    captureEventsDropped: Number(
      raw.capture_events_dropped ?? raw.captureEventsDropped ?? 0,
    ),
    captureStalled: Boolean(raw.capture_stalled ?? raw.captureStalled ?? false),
    permissions: mapPermissionStatus(
      (raw.permissions as Record<string, unknown>) ?? {},
    ),
    classifier: mapClassifierStatus(
      (raw.classifier as Record<string, unknown>) ?? {},
    ),
  };
}

export function mapDiagnosticsSnapshot(raw: Record<string, unknown>): DiagnosticsSnapshot {
  const logs = raw.recent_logs ?? raw.recentLogs;
  return {
    health: mapHealth((raw.health as Record<string, unknown>) ?? {}),
    recentLogs: Array.isArray(logs) ? logs.map((line) => String(line)) : [],
  };
}

export function mapAppRule(raw: Record<string, unknown>): AppRuleRecord {
  return {
    id: Number(raw.id ?? 0),
    pattern: String(raw.pattern ?? ""),
    ruleType: String(raw.rule_type ?? raw.ruleType ?? "allow") as AppRuleKind,
    note: (raw.note ?? null) as string | null,
    createdAt: String(raw.created_at ?? raw.createdAt ?? ""),
    updatedAt: String(raw.updated_at ?? raw.updatedAt ?? ""),
  };
}

export function mapPrediction(raw: Record<string, unknown>): PredictionRecord {
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

export function mapSession(raw: Record<string, unknown>): SessionRecord {
  return {
    sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
    goal: String(raw.goal ?? ""),
    status: String(raw.status ?? ""),
    focusMode: String(raw.focus_mode ?? raw.focusMode ?? "normal"),
    startedAt: (raw.started_at ?? raw.startedAt ?? null) as string | null,
    endedAt: (raw.ended_at ?? raw.endedAt ?? null) as string | null,
  };
}

export function mapSessionRecap(raw: Record<string, unknown>): SessionRecap {
  return {
    sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
    goal: String(raw.goal ?? ""),
    durationSecs: Number(raw.duration_secs ?? raw.durationSecs ?? 0),
    avgFocusScore: Number(raw.avg_focus_score ?? raw.avgFocusScore ?? 0),
    avgDistractionRisk: Number(raw.avg_distraction_risk ?? raw.avgDistractionRisk ?? 0),
    snapbackCount: Number(raw.snapback_count ?? raw.snapbackCount ?? 0),
    thrashSpikes: Number(raw.thrash_spikes ?? raw.thrashSpikes ?? 0),
    deepFocusPct: Number(raw.deep_focus_pct ?? raw.deepFocusPct ?? 0),
  };
}

export function mapFocusSummary(raw: Record<string, unknown>): FocusSummary {
  return {
    sampleCount: Number(raw.sample_count ?? raw.sampleCount ?? 0),
    avgFocusScore: Number(raw.avg_focus_score ?? raw.avgFocusScore ?? 0),
    peakFocusScore: Number(raw.peak_focus_score ?? raw.peakFocusScore ?? 0),
    distractedSamples: Number(raw.distracted_samples ?? raw.distractedSamples ?? 0),
    distractedFraction: Number(raw.distracted_fraction ?? raw.distractedFraction ?? 0),
    longestFocusStreak: Number(raw.longest_focus_streak ?? raw.longestFocusStreak ?? 0),
  };
}

export function mapPomodoroStatus(raw: Record<string, unknown>): PomodoroStatus {
  return {
    running: Boolean(raw.running ?? false),
    phase: normalizePomodoroPhase(raw.phase),
    completedWorkIntervals: Number(
      raw.completed_work_intervals ?? raw.completedWorkIntervals ?? 0,
    ),
    remainingMs: Number(raw.remaining_ms ?? raw.remainingMs ?? 0),
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
    outputDir: String(raw.output_dir ?? raw.outputDir ?? ""),
    featuresPath: String(raw.features_path ?? raw.featuresPath ?? ""),
    labelsPath: String(raw.labels_path ?? raw.labelsPath ?? ""),
    featureCount: Number(raw.feature_count ?? raw.featureCount ?? 0),
    labelCount: Number(raw.label_count ?? raw.labelCount ?? 0),
  };
}

export function mapTrainingDeployStatus(raw: Record<string, unknown>): TrainingDeployStatus {
  const labelBreakdownRaw =
    (raw.label_breakdown ?? raw.labelBreakdown ?? {}) as Record<string, unknown>;
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
  return {
    exportDir: String(raw.export_dir ?? raw.exportDir ?? ""),
    featureCount: Number(raw.feature_count ?? raw.featureCount ?? 0),
    labelCount: Number(raw.label_count ?? raw.labelCount ?? 0),
    labelBreakdown,
    hasExport: Boolean(raw.has_export ?? raw.hasExport ?? false),
    modelOnnxExists: Boolean(raw.model_onnx_exists ?? raw.modelOnnxExists ?? false),
    metricsExists: Boolean(raw.metrics_exists ?? raw.metricsExists ?? false),
    metrics,
    pythonAvailable: Boolean(raw.python_available ?? raw.pythonAvailable ?? false),
    repoPath: (raw.repo_path ?? raw.repoPath ?? null) as string | null,
    repoConfigured: Boolean(raw.repo_configured ?? raw.repoConfigured ?? false),
    pipelineCommand: String(raw.pipeline_command ?? raw.pipelineCommand ?? ""),
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
    trainingSucceeded: Boolean(raw.training_succeeded ?? raw.trainingSucceeded ?? raw.success ?? false),
    deployReady: Boolean(
      raw.deploy_ready ?? raw.deployReady ?? raw.onnx_exported ?? raw.onnxExported ?? false,
    ),
    message: String(raw.message ?? ""),
    onnxExported: Boolean(raw.onnx_exported ?? raw.onnxExported ?? false),
    metrics,
    logTail: String(raw.log_tail ?? raw.logTail ?? ""),
  };
}

export function mapSnapbackPayload(raw: Record<string, unknown>): SnapbackPayload {
  return {
    summary: String(raw.summary ?? "Previous task"),
    appName: String(raw.app_name ?? raw.appName ?? ""),
    windowTitle: String(raw.window_title ?? raw.windowTitle ?? ""),
    fileHint: String(raw.file_hint ?? raw.fileHint ?? ""),
    distractionDurationSecs: Number(
      raw.distraction_duration_secs ?? raw.distractionDurationSecs ?? 0,
    ),
  };
}
