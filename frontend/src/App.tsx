import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import {
  api,
  focusStateLabel,
  formatPercent,
  formatScore,
  formatTime,
  riskLabel,
  riskLevel,
  type AppRuleKind,
  type AppRuleRecord,
  type ContextSnapshot,
  type PredictionRecord,
} from "./api";
import { classifierBackendLabel } from "./trainingHints";
import { buildAppRulePreview } from "./appRulePreview";
import { summarizePermissions } from "./healthHints";
import { shouldRefreshTimelineFromEvent } from "./timelineRefresh";
import { useHealth } from "./useHealth";
import { useTrainingDeploy } from "./useTrainingDeploy";
import { FOCUS_MODES, type FocusMode, useSession } from "./useSession";

const HISTORY_LIMIT = 8;
const TIMELINE_LIMIT = 20;
const TIMELINE_POLL_MS = 30_000;
const APP_RULE_KINDS: AppRuleKind[] = ["allow", "block"];

const ruleKindLabel = (kind: AppRuleKind) => (kind === "allow" ? "Allow" : "Block");

const modelFileLabel = (path: string | null) => {
  if (!path) {
    return "No model file";
  }
  const normalized = path.replace(/\\/g, "/");
  const segments = normalized.split("/").filter(Boolean);
  return segments[segments.length - 1] ?? path;
};

const buildSignals = (record: PredictionRecord | null) => {
  if (!record) {
    return ["Waiting for live capture."];
  }

  const level = riskLevel(record.distractionRisk);
  const signals = [
    `Focus state: ${focusStateLabel(record.focusState)}`,
    `Thrash: ${(record.thrashScore * 100).toFixed(0)}% · Drift: ${(record.driftScore * 100).toFixed(0)}% · Goal fit: ${(record.goalAlignment * 100).toFixed(0)}%`,
    `Risk level: ${level}`,
    `Focus score: ${formatScore(record.focusScore)}`,
  ];

  if (record.focusState === "PSEUDO_PRODUCTIVE") {
    signals.push("Drift detected — tab/title churn or scattered typing in a work app.");
  } else if (record.thrashScore >= 0.6) {
    signals.push("Context-switch thrash — jumping between apps/windows rapidly.");
  } else if (record.focusState === "DEEP_FOCUS") {
    signals.push("Deep work detected. Hyperfocus guardrail is watching.");
  } else if (level === "low") {
    signals.push("Focus is stable. Keep momentum.");
  }

  return signals;
};

export default function App() {
  const [prediction, setPrediction] = useState<PredictionRecord | null>(null);
  const [predictionHistory, setPredictionHistory] = useState<PredictionRecord[]>([]);
  const [hyperfocusNote, setHyperfocusNote] = useState<string | null>(null);
  const [snapbackNote, setSnapbackNote] = useState<string | null>(null);
  const [labelStatus, setLabelStatus] = useState<string | null>(null);
  const [labelStatusWarning, setLabelStatusWarning] = useState(false);
  const [actionError, setActionError] = useState<string | null>(null);
  const [appRules, setAppRules] = useState<AppRuleRecord[]>([]);
  const [rulePattern, setRulePattern] = useState("");
  const [ruleKind, setRuleKind] = useState<AppRuleKind>("allow");
  const [ruleNote, setRuleNote] = useState("");
  const [rulesStatus, setRulesStatus] = useState<string | null>(null);
  const [contextTimeline, setContextTimeline] = useState<ContextSnapshot[]>([]);
  const lastTimelineRefreshAtRef = useRef<number | null>(null);

  const {
    activeWindowAvailable,
    applyCaptureFailure,
    applyClassifierStatus,
    captureFailed,
    captureFailureReason,
    captureRunning,
    classifierBackend,
    classifierModelPath,
    classifierOnnxRuntimeEnabled,
    handleRefreshPermissions,
    healthStatus,
    permissionCaptureAvailable,
    permissionMessage,
    permissionSteps,
    refreshHealth,
  } = useHealth();

  const refreshContextTimeline = useCallback(async (sid?: string | null) => {
    if (!sid) {
      setContextTimeline([]);
      return;
    }

    lastTimelineRefreshAtRef.current = Date.now();
    try {
      const rows = await api.getContextTimeline(sid, TIMELINE_LIMIT);
      setContextTimeline(rows);
    } catch {
      setContextTimeline([]);
    }
  }, []);

  const resetTimelineRefreshGate = useCallback(() => {
    lastTimelineRefreshAtRef.current = null;
  }, []);

  const {
    focusMode,
    handleFocusModeChange,
    handleLabel,
    handleSkipSurvey,
    handleStartSession,
    handleStopSession,
    hydrateActiveSession,
    recap,
    sessionGoal,
    sessionId,
    sessionRecord,
    sessionStatusLabel,
    setSessionGoal,
    surveyPending,
  } = useSession({
    refreshContextTimeline,
    resetTimelineRefreshGate,
    setActionError,
    setLabelStatus,
    setLabelStatusWarning,
  });

  const {
    canTrainFromExport,
    copyStatus,
    deployMessage,
    deployMessageWarning,
    deployStatus,
    handleCopyTrainingCommand,
    handleExportTrainingData,
    handleReloadClassifierModel,
    handleSaveRepoPath,
    handleTrainFromExport,
    modelReloadStatus,
    refreshDeployStatus,
    repoPathInput,
    setRepoPathInput,
    setShowAdvancedCommand,
    showAdvancedCommand,
    trainingCommand,
    trainFromExportHint,
    trainingInProgress,
  } = useTrainingDeploy({
    sessionId,
    setLabelStatus,
    setLabelStatusWarning,
    onClassifierStatusChange: applyClassifierStatus,
  });

  const refreshTimelineFromEvent = useCallback((sid?: string | null) => {
    if (!sid || sessionRecord?.status !== "ACTIVE") {
      return;
    }

    const now = Date.now();
    if (!shouldRefreshTimelineFromEvent(lastTimelineRefreshAtRef.current, now)) {
      return;
    }

    lastTimelineRefreshAtRef.current = now;
    void refreshContextTimeline(sid);
  }, [refreshContextTimeline, sessionRecord?.status]);

  const pushPrediction = useCallback((record: PredictionRecord | null) => {
    if (!record) {
      setPrediction(null);
      return;
    }

    setPrediction(record);
    setPredictionHistory((current) => {
      const last = current[0];
      const isDuplicate =
        last &&
        last.timestamp === record.timestamp &&
        last.focusScore === record.focusScore &&
        last.distractionRisk === record.distractionRisk;
      const next = isDuplicate ? current : [record, ...current];
      return next.slice(0, HISTORY_LIMIT);
    });
  }, []);

  const refreshAppRules = useCallback(async () => {
    try {
      const rules = await api.getAppRules();
      setAppRules(rules);
    } catch {
      setRulesStatus("Could not load app rules.");
    }
  }, []);

  const refreshLatest = useCallback(async () => {
    try {
      const latest = await api.getLatestPrediction();
      pushPrediction(latest);
      const history = await api.getPredictionHistory(HISTORY_LIMIT);
      if (history.length > 0) {
        setPredictionHistory(history);
      }
    } catch {
      pushPrediction(null);
    }
  }, [pushPrediction]);

  useEffect(() => {
    void refreshHealth();
    void refreshLatest();
    void refreshAppRules();
    void refreshDeployStatus();
    void hydrateActiveSession();
  }, [hydrateActiveSession, refreshHealth, refreshLatest, refreshAppRules, refreshDeployStatus]);

  useEffect(() => {
    if (!sessionId || sessionRecord?.status !== "ACTIVE") {
      return;
    }

    const timer = window.setInterval(() => {
      void refreshContextTimeline(sessionId);
    }, TIMELINE_POLL_MS);

    return () => window.clearInterval(timer);
  }, [sessionId, sessionRecord?.status, refreshContextTimeline]);

  useEffect(() => {
    const unsubs: Array<Promise<() => void>> = [];
    unsubs.push(
      api.onCaptureFailed((payload) => {
        applyCaptureFailure(payload);
      }),
    );
    unsubs.push(
      api.onPrediction((record) => {
        pushPrediction(record);
        if (record.sessionId === sessionId) {
          refreshTimelineFromEvent(record.sessionId);
        }
      }),
    );
    unsubs.push(
      api.onSnapback((payload) => {
        setSnapbackNote(`Snapback: ${payload.summary}`);
        refreshTimelineFromEvent(sessionId);
      }),
    );
    unsubs.push(
      api.onHyperfocus((payload) => {
        setHyperfocusNote(payload.message);
      }),
    );
    unsubs.push(
      api.onLabelHotkey((payload) => {
        setLabelStatus(payload.message);
        setLabelStatusWarning(!payload.ok);
      }),
    );

    return () => {
      void Promise.all(unsubs).then((handlers) => handlers.forEach((off) => off()));
    };
  }, [pushPrediction, applyCaptureFailure, refreshTimelineFromEvent, sessionId]);

  const handleAddAppRule = async () => {
    const pattern = rulePattern.trim();
    if (!pattern) {
      setRulesStatus("Enter an app name or keyword (e.g. discord, notion).");
      return;
    }

    try {
      const saved = await api.upsertAppRule(pattern, ruleKind, ruleNote.trim() || undefined);
      await refreshAppRules();
      setRulePattern("");
      setRuleNote("");
      setRulesStatus(`Saved ${ruleKindLabel(saved.ruleType).toLowerCase()} rule for "${saved.pattern}".`);
    } catch {
      setRulesStatus("Could not save app rule.");
    }
  };

  const handleDeleteAppRule = async (rule: AppRuleRecord) => {
    try {
      await api.deleteAppRule(rule.id);
      setAppRules((current) => current.filter((entry) => entry.id !== rule.id));
      setRulesStatus(`Removed rule for "${rule.pattern}".`);
    } catch {
      setRulesStatus("Could not delete app rule.");
    }
  };

  const signals = useMemo(() => buildSignals(prediction), [prediction]);
  const riskValue = prediction?.distractionRisk ?? null;
  const riskBadgeLabel = prediction ? riskLabel(riskValue) : "No data";
  const riskClass = riskLevel(riskValue);
  const rulePreview = buildAppRulePreview(rulePattern, ruleKind, ruleNote);
  const permissionHealth = summarizePermissions({
    captureAvailable: permissionCaptureAvailable,
    activeWindowAvailable,
    message: permissionMessage ?? "",
    setupSteps: permissionSteps,
  });
  const classifierRuntimeLabel = classifierOnnxRuntimeEnabled
    ? "ONNX runtime enabled"
    : "ONNX runtime unavailable";
  const activeModelLabel =
    classifierBackend === "onnx"
      ? modelFileLabel(classifierModelPath)
      : classifierModelPath
        ? `${modelFileLabel(classifierModelPath)} available`
        : "Heuristic only";

  return (
    <div className="app">
      <header className="app-header">
        <div>
          <p className="eyebrow">Snapback</p>
          <h1>Live Focus Command Center</h1>
          <p className="subtitle">
            Measures how you work — deep focus, drift, and context-switch thrash — with snapback
            recovery when you return.
          </p>
        </div>
        <div className="status-stack">
          <div className="status-pill">
            <span className="status-label">App</span>
            <span className="status-value">{healthStatus}</span>
          </div>
          <div className="status-pill">
            <span className="status-label">Capture</span>
            <span className={`status-value${captureFailed ? " status-alert" : ""}`}>
              {captureFailed ? "failed" : captureRunning ? "running" : "idle"}
            </span>
          </div>
          <div className="status-pill status-pill-stack">
            <span className="status-label">Permissions</span>
            <span
              className={`status-value${permissionHealth.label === "blocked" ? " status-alert" : ""}`}
            >
              {permissionHealth.label}
            </span>
            <span className="status-detail">{permissionHealth.detail}</span>
          </div>
          <div className="status-pill">
            <span className="status-label">Classifier</span>
            <span className="status-value">{classifierBackendLabel(classifierBackend)}</span>
          </div>
          <div className="status-pill status-pill-stack">
            <span className="status-label">Model</span>
            <span className="status-value">{activeModelLabel}</span>
            <span className="status-detail">{classifierRuntimeLabel}</span>
          </div>
        </div>
      </header>

      {actionError ? (
        <div className="action-error-banner" role="alert">
          <p>{actionError}</p>
          <button
            type="button"
            className="ghost-button"
            onClick={() => setActionError(null)}
          >
            Dismiss
          </button>
        </div>
      ) : null}

      <main className="grid">
        <section className="card live-card">
          <div className="card-header">
            <h2>Live Prediction</h2>
            <span className={`risk-badge risk-${riskClass}`}>{riskBadgeLabel}</span>
          </div>
          <div className="metrics">
            <div className="metric">
              <p className="metric-label">Focus score</p>
              <p className="metric-value">{formatScore(prediction?.focusScore ?? null)}</p>
            </div>
            <div className="metric">
              <p className="metric-label">Distraction risk</p>
              <p className="metric-value">{formatPercent(prediction?.distractionRisk ?? null)}</p>
            </div>
            <div className="metric">
              <p className="metric-label">State</p>
              <p className="metric-value state-value">
                {focusStateLabel(prediction?.focusState ?? null)}
              </p>
            </div>
          </div>
          <div className="meta">
            <div>
              <p className="meta-label">Last update</p>
              <p className="meta-value">{formatTime(prediction?.timestamp ?? null)}</p>
            </div>
            <div>
              <p className="meta-label">Session</p>
              <p className="meta-value">{prediction?.sessionId || "--"}</p>
            </div>
          </div>
        </section>

        <section className="card session-card">
          <div className="card-header">
            <h2>Session Control</h2>
            <span className="session-status">{sessionStatusLabel}</span>
          </div>
          <label className="field">
            <span>Focus goal</span>
            <input
              type="text"
              placeholder="Ship the snapback overlay"
              value={sessionGoal}
              onChange={(event) => setSessionGoal(event.target.value)}
            />
          </label>
          <label className="field">
            <span>Focus mode</span>
            <select
              value={focusMode}
              onChange={(event) =>
                void handleFocusModeChange(event.target.value as FocusMode)
              }
            >
              {FOCUS_MODES.map((mode) => (
                <option key={mode} value={mode}>
                  {mode}
                </option>
              ))}
            </select>
          </label>
          <div className="button-row">
            <button className="primary-button" onClick={handleStartSession}>
              Start session
            </button>
            <button className="secondary-button" onClick={handleStopSession}>
              Stop session
            </button>
          </div>
          <div className="meta">
            <div>
              <p className="meta-label">Session ID</p>
              <p className="meta-value">{sessionId || "--"}</p>
            </div>
            <div>
              <p className="meta-label">Started</p>
              <p className="meta-value">{formatTime(sessionRecord?.startedAt ?? null)}</p>
            </div>
            <div>
              <p className="meta-label">Ended</p>
              <p className="meta-value">{formatTime(sessionRecord?.endedAt ?? null)}</p>
            </div>
          </div>
        </section>

        <section className="card signals-card">
          <div className="card-header">
            <h2>Signals</h2>
            <span className="pill">rolling 30s</span>
          </div>
          <ul className="signal-list">
            {signals.map((signal, index) => (
              <li key={`${signal}-${index}`}>{signal}</li>
            ))}
          </ul>
          {hyperfocusNote ? <p className="helper-text alert">{hyperfocusNote}</p> : null}
          {snapbackNote ? <p className="helper-text snapback">{snapbackNote}</p> : null}
        </section>

        <section className="card feedback-card">
          <div className="card-header">
            <h2>Focus Feedback</h2>
            <span className="pill">train the model</span>
          </div>
          <p className="helper-text">
            One tap — was that moment actually focused? Global hotkeys: Ctrl+Shift+1 deep, 2 focused,
            3 drift, 4 distracted (works from any app during a session).
          </p>
          <div className="button-row feedback-row">
            <button className="secondary-button" onClick={() => void handleLabel("DEEP_FOCUS")}>
              Deep
            </button>
            <button className="secondary-button" onClick={() => void handleLabel("PRODUCTIVE")}>
              Focused
            </button>
            <button
              className="secondary-button"
              onClick={() => void handleLabel("PSEUDO_PRODUCTIVE")}
            >
              Drift
            </button>
            <button className="secondary-button" onClick={() => void handleLabel("DISTRACTED")}>
              Distracted
            </button>
            <button className="secondary-button" onClick={() => void handleExportTrainingData()}>
              Export training data
            </button>
          </div>
          {labelStatus ? (
            <p className={`helper-text${labelStatusWarning ? " alert" : ""}`}>{labelStatus}</p>
          ) : null}

          <div className="training-deploy-block">
            <p className="deploy-title">Deploy trained model</p>
            <ol className="deploy-steps">
              <li className={deployStatus?.hasExport ? "deploy-step done" : "deploy-step"}>
                <span className="deploy-step-label">Export</span>
                <span className="deploy-step-detail">
                  {deployStatus?.hasExport
                    ? `${deployStatus.featureCount} features · ${deployStatus.labelCount} labels`
                    : "Export training data above"}
                </span>
              </li>
              <li
                className={
                  deployStatus?.modelOnnxExists || deployStatus?.metricsExists
                    ? "deploy-step done"
                    : "deploy-step"
                }
              >
                <span className="deploy-step-label">Train</span>
                <span className="deploy-step-detail">
                  {deployStatus?.modelOnnxExists
                    ? "model.onnx ready"
                    : deployStatus?.metricsExists
                      ? "model.json trained (ONNX pending)"
                      : deployStatus?.pythonAvailable
                        ? "Run train from export"
                        : "Install Python 3 + xgboost"}
                </span>
              </li>
              <li className={classifierBackend === "onnx" ? "deploy-step done" : "deploy-step"}>
                <span className="deploy-step-label">Activate</span>
                <span className="deploy-step-detail">
                  {classifierBackend === "onnx"
                    ? "ONNX classifier active"
                    : deployStatus?.modelOnnxExists
                      ? "Reload model to switch off heuristic"
                      : "Reload after training"}
                </span>
              </li>
            </ol>

            <label className="field deploy-repo-field">
              <span>Snapback repo path (for in-app training)</span>
              <input
                type="text"
                placeholder="C:\Users\you\Projects\Snapback"
                value={repoPathInput}
                onChange={(event) => setRepoPathInput(event.target.value)}
              />
            </label>
            <div className="button-row">
              <button className="secondary-button" onClick={() => void handleSaveRepoPath()}>
                Save repo path
              </button>
              <button
                className="primary-button"
                disabled={!canTrainFromExport}
                onClick={() => void handleTrainFromExport()}
              >
                {trainingInProgress ? "Training…" : "Train from export"}
              </button>
              <button className="secondary-button" onClick={() => void handleReloadClassifierModel()}>
                Reload model
              </button>
            </div>
            {trainFromExportHint ? (
              <p className="helper-text alert">
                {trainFromExportHint}
              </p>
            ) : null}
            {deployMessage ? (
              <p
                className={`helper-text deploy-log${deployMessageWarning ? " alert" : ""}`}
              >
                {deployMessage}
              </p>
            ) : null}
            {modelReloadStatus ? <p className="helper-text">{modelReloadStatus}</p> : null}
            {classifierModelPath ? (
              <p className="helper-text">Model path: {classifierModelPath}</p>
            ) : null}
            <button
              type="button"
              className="link-button"
              onClick={() => setShowAdvancedCommand((current) => !current)}
            >
              {showAdvancedCommand ? "Hide CLI command" : "Show CLI command"}
            </button>
            {showAdvancedCommand && deployStatus ? (
              <div className="training-command-block">
                <pre className="training-command">
                  {trainingCommand}
                </pre>
                <button className="secondary-button" onClick={() => void handleCopyTrainingCommand()}>
                  Copy CLI command
                </button>
                {copyStatus ? <p className="helper-text">{copyStatus}</p> : null}
              </div>
            ) : null}
          </div>
        </section>

        <section className="card history-card">
          <div className="card-header">
            <h2>Recent Predictions</h2>
            <span className="pill">latest {HISTORY_LIMIT}</span>
          </div>
          <ul className="history-list">
            {predictionHistory.length === 0 ? (
              <li className="history-empty">No predictions yet.</li>
            ) : (
              predictionHistory.map((entry) => (
                <li
                  key={`${entry.timestamp}-${entry.sessionId}-${entry.focusScore}`}
                  className="history-item"
                >
                  <div>
                    <p className="history-time">{formatTime(entry.timestamp)}</p>
                    <p className="history-session">{focusStateLabel(entry.focusState)}</p>
                  </div>
                  <div className="history-metrics">
                    <span className="history-score">{formatScore(entry.focusScore)}</span>
                    <span className={`history-risk risk-${riskLevel(entry.distractionRisk)}`}>
                      {formatPercent(entry.distractionRisk)}
                    </span>
                  </div>
                </li>
              ))
            )}
          </ul>
        </section>

        <section className="card timeline-card">
          <div className="card-header">
            <h2>Context Timeline</h2>
            <span className="pill">session trail</span>
          </div>
          <p className="helper-text">
            Where you were working during this session — apps, files, and parsed summaries.
          </p>
          {!sessionId ? (
            <p className="helper-text">Start a session to record context snapshots.</p>
          ) : (
            <>
              <button
                className="ghost-button"
                onClick={() => void refreshContextTimeline(sessionId)}
              >
                Refresh timeline
              </button>
              <ol className="timeline-list">
                {contextTimeline.length === 0 ? (
                  <li className="timeline-empty">No context snapshots yet.</li>
                ) : (
                  contextTimeline.map((entry, index) => (
                    <li
                      key={`${entry.timestamp}-${entry.appName}-${index}`}
                      className="timeline-item"
                    >
                      <div className="timeline-marker" aria-hidden="true" />
                      <div className="timeline-body">
                        <p className="timeline-time">{formatTime(entry.timestamp)}</p>
                        <p className="timeline-summary">{entry.summary || entry.windowTitle}</p>
                        <p className="timeline-meta">
                          {entry.appName}
                          {entry.fileHint ? ` · ${entry.fileHint}` : ""}
                          {entry.projectHint ? ` · ${entry.projectHint}` : ""}
                        </p>
                      </div>
                    </li>
                  ))
                )}
              </ol>
            </>
          )}
        </section>

        {surveyPending && recap ? (
          <section className="card survey-card">
            <div className="card-header">
              <h2>Session Check-in</h2>
              <span className="pill">end of session</span>
            </div>
            <p className="helper-text">
              We saved an automatic label from your recap. Override it if your gut says
              different.
            </p>
            <div className="button-row feedback-row">
              <button
                className="secondary-button"
                onClick={() => void handleLabel("DEEP_FOCUS", "survey")}
              >
                Deep
              </button>
              <button
                className="secondary-button"
                onClick={() => void handleLabel("PRODUCTIVE", "survey")}
              >
                Focused
              </button>
              <button
                className="secondary-button"
                onClick={() => void handleLabel("PSEUDO_PRODUCTIVE", "survey")}
              >
                Drift
              </button>
              <button
                className="secondary-button"
                onClick={() => void handleLabel("DISTRACTED", "survey")}
              >
                Distracted
              </button>
              <button className="ghost-button" onClick={handleSkipSurvey}>
                Keep automatic label
              </button>
            </div>
          </section>
        ) : null}

        {recap ? (
          <section className="card recap-card">
            <div className="card-header">
              <h2>Session Recap</h2>
              <span className="pill">summary</span>
            </div>
            <div className="meta">
              <div>
                <p className="meta-label">Duration</p>
                <p className="meta-value">{Math.round(recap.durationSecs / 60)} min</p>
              </div>
              <div>
                <p className="meta-label">Avg focus</p>
                <p className="meta-value">{formatScore(recap.avgFocusScore)}</p>
              </div>
              <div>
                <p className="meta-label">Deep work</p>
                <p className="meta-value">{recap.deepFocusPct.toFixed(0)}%</p>
              </div>
              <div>
                <p className="meta-label">Snapbacks</p>
                <p className="meta-value">{recap.snapbackCount}</p>
              </div>
              <div>
                <p className="meta-label">Thrash spikes</p>
                <p className="meta-value">{recap.thrashSpikes}</p>
              </div>
            </div>
          </section>
        ) : null}

        <section className="card rules-card">
          <div className="card-header">
            <h2>Personal App Rules</h2>
            <span className="pill">your overrides</span>
          </div>
          <p className="helper-text">
            Match part of an app name or window title. Allow marks matching windows as on-task for
            you. Block raises distraction scoring for matches — it does not close or block apps.
          </p>
          <label className="field">
            <span>Pattern</span>
            <input
              type="text"
              placeholder="discord, notion, youtube"
              value={rulePattern}
              onChange={(event) => setRulePattern(event.target.value)}
            />
          </label>
          <label className="field">
            <span>Rule type</span>
            <select
              value={ruleKind}
              onChange={(event) => setRuleKind(event.target.value as AppRuleKind)}
            >
              {APP_RULE_KINDS.map((kind) => (
                <option key={kind} value={kind}>
                  {ruleKindLabel(kind)}
                </option>
              ))}
            </select>
          </label>
          <label className="field">
            <span>Note (optional)</span>
            <input
              type="text"
              placeholder="study group server"
              value={ruleNote}
              onChange={(event) => setRuleNote(event.target.value)}
            />
          </label>
          {rulePreview ? <p className="rule-preview">{rulePreview}</p> : null}
          <button className="primary-button" onClick={() => void handleAddAppRule()}>
            Save rule
          </button>
          <ul className="rules-list">
            {appRules.length === 0 ? (
              <li className="rules-empty">No personal rules yet.</li>
            ) : (
              appRules.map((rule) => (
                <li key={rule.id} className="rules-item">
                  <div>
                    <div className="rules-item-header">
                      <span className="rules-pattern">{rule.pattern}</span>
                      <span className={`rules-badge rules-badge-${rule.ruleType}`}>
                        {ruleKindLabel(rule.ruleType)}
                      </span>
                    </div>
                    {rule.note ? <p className="rules-note">{rule.note}</p> : null}
                  </div>
                  <button
                    className="secondary-button rules-delete"
                    onClick={() => void handleDeleteAppRule(rule)}
                  >
                    Remove
                  </button>
                </li>
              ))
            )}
          </ul>
          {rulesStatus ? <p className="helper-text">{rulesStatus}</p> : null}
        </section>

        <section className={`card config-card${captureFailed ? " config-card-alert" : ""}`}>
          <div className="card-header">
            <h2>Permissions</h2>
            <span className={`pill${captureFailed ? " pill-alert" : ""}`}>
              {captureFailed ? "capture failed" : "local desktop"}
            </span>
          </div>
          {captureFailed ? (
            <p className="helper-text alert">
              Capture listener stopped
              {captureFailureReason ? `: ${captureFailureReason}` : "."}
            </p>
          ) : null}
          <p className="helper-text">
            {permissionMessage ||
              "Snapback runs locally. Grant Accessibility + Input Monitoring on macOS."}
          </p>
          {permissionSteps.length > 0 ? (
            <ol className="permission-steps">
              {permissionSteps.map((step) => (
                <li key={step}>{step}</li>
              ))}
            </ol>
          ) : null}
          <button className="secondary-button" onClick={() => void handleRefreshPermissions()}>
            Refresh permissions
          </button>
        </section>
      </main>
    </div>
  );
}
