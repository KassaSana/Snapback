import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import {
  api,
  focusStateLabel,
  formatPercent,
  formatScore,
  formatTime,
  riskLabel,
  riskLevel,
  type ContextSnapshot,
  type PredictionRecord,
} from "./api";
import { classifierBackendLabel } from "./trainingHints";
import { ActivityCards } from "./ActivityCards";
import { AppHeader } from "./AppHeader";
import { LiveStatusCards } from "./LiveStatusCards";
import { RulesCard } from "./RulesCard";
import { PermissionsCard } from "./PermissionsCard";
import { SessionControlCard } from "./SessionControlCard";
import { SessionReviewCards } from "./SessionReviewCards";
import { TrainingDeployCard } from "./TrainingDeployCard";
import { shouldRefreshTimelineFromEvent } from "./timelineRefresh";
import { useAppRules } from "./useAppRules";
import { useHealth } from "./useHealth";
import { useTrainingDeploy } from "./useTrainingDeploy";
import { useSession } from "./useSession";

const HISTORY_LIMIT = 8;
const TIMELINE_LIMIT = 20;
const TIMELINE_POLL_MS = 30_000;

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

  const {
    appRules,
    handleAddAppRule,
    handleDeleteAppRule,
    refreshAppRules,
    ruleKind,
    ruleKindLabel,
    ruleKinds,
    ruleNote,
    rulePattern,
    rulePreview,
    rulesStatus,
    setRuleKind,
    setRuleNote,
    setRulePattern,
  } = useAppRules();

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

  const signals = useMemo(() => buildSignals(prediction), [prediction]);
  const riskValue = prediction?.distractionRisk ?? null;
  const riskBadgeLabel = prediction ? riskLabel(riskValue) : "No data";
  const riskClass = riskLevel(riskValue);

  return (
    <div className="app">
      <AppHeader
        activeWindowAvailable={activeWindowAvailable}
        captureFailed={captureFailed}
        captureRunning={captureRunning}
        classifierBackend={classifierBackend}
        classifierModelPath={classifierModelPath}
        classifierOnnxRuntimeEnabled={classifierOnnxRuntimeEnabled}
        healthStatus={healthStatus}
        permissionCaptureAvailable={permissionCaptureAvailable}
        permissionMessage={permissionMessage}
        permissionSteps={permissionSteps}
      />

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
        <LiveStatusCards
          hyperfocusNote={hyperfocusNote}
          prediction={prediction}
          riskBadgeLabel={riskBadgeLabel}
          riskClass={riskClass}
          signals={signals}
          snapbackNote={snapbackNote}
        />

        <SessionControlCard
          focusMode={focusMode}
          handleFocusModeChange={handleFocusModeChange}
          handleStartSession={handleStartSession}
          handleStopSession={handleStopSession}
          sessionGoal={sessionGoal}
          sessionId={sessionId}
          sessionRecord={sessionRecord}
          sessionStatusLabel={sessionStatusLabel}
          setSessionGoal={setSessionGoal}
        />

        <TrainingDeployCard
          canTrainFromExport={canTrainFromExport}
          classifierBackend={classifierBackend}
          classifierModelPath={classifierModelPath}
          copyStatus={copyStatus}
          deployMessage={deployMessage}
          deployMessageWarning={deployMessageWarning}
          deployStatus={deployStatus}
          handleCopyTrainingCommand={handleCopyTrainingCommand}
          handleExportTrainingData={handleExportTrainingData}
          handleLabel={handleLabel}
          handleReloadClassifierModel={handleReloadClassifierModel}
          handleSaveRepoPath={handleSaveRepoPath}
          handleTrainFromExport={handleTrainFromExport}
          labelStatus={labelStatus}
          labelStatusWarning={labelStatusWarning}
          modelReloadStatus={modelReloadStatus}
          repoPathInput={repoPathInput}
          setRepoPathInput={setRepoPathInput}
          setShowAdvancedCommand={setShowAdvancedCommand}
          showAdvancedCommand={showAdvancedCommand}
          trainFromExportHint={trainFromExportHint}
          trainingCommand={trainingCommand}
          trainingInProgress={trainingInProgress}
        />

        <ActivityCards
          contextTimeline={contextTimeline}
          historyLimit={HISTORY_LIMIT}
          predictionHistory={predictionHistory}
          refreshContextTimeline={refreshContextTimeline}
          sessionId={sessionId}
        />

        <SessionReviewCards
          handleLabel={handleLabel}
          handleSkipSurvey={handleSkipSurvey}
          recap={recap}
          surveyPending={surveyPending}
        />

        <RulesCard
          appRules={appRules}
          handleAddAppRule={handleAddAppRule}
          handleDeleteAppRule={handleDeleteAppRule}
          ruleKind={ruleKind}
          ruleKindLabel={ruleKindLabel}
          ruleKinds={ruleKinds}
          ruleNote={ruleNote}
          rulePattern={rulePattern}
          rulePreview={rulePreview}
          rulesStatus={rulesStatus}
          setRuleKind={setRuleKind}
          setRuleNote={setRuleNote}
          setRulePattern={setRulePattern}
        />

        <PermissionsCard
          captureFailed={captureFailed}
          captureFailureReason={captureFailureReason}
          onRefreshPermissions={handleRefreshPermissions}
          permissionMessage={permissionMessage}
          permissionSteps={permissionSteps}
        />
      </main>
    </div>
  );
}
