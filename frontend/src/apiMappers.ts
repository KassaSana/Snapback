import type {
  AppRuleKind,
  AppRuleRecord,
  ClassifierStatus,
  ContextSnapshot,
  ExportTrainingResult,
  HealthStatus,
  PermissionStatus,
  PredictionRecord,
  SessionRecord,
  SnapbackPayload,
  TrainFromExportResult,
  TrainingDeployStatus,
} from "./api";

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
    permissions: mapPermissionStatus(
      (raw.permissions as Record<string, unknown>) ?? {},
    ),
    classifier: mapClassifierStatus(
      (raw.classifier as Record<string, unknown>) ?? {},
    ),
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
