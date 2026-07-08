import { useCallback, useEffect, useState } from "react";

import {
  api,
  formatPercent,
  formatScore,
  formatTime,
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
import { useAppRules } from "./useAppRules";
import { useHealth } from "./useHealth";
import { HISTORY_LIMIT, TIMELINE_POLL_MS, useLiveData } from "./useLiveData";
import { useTrainingDeploy } from "./useTrainingDeploy";
import { useSession } from "./useSession";


export default function App() {
  const [labelStatus, setLabelStatus] = useState<string | null>(null);
  const [labelStatusWarning, setLabelStatusWarning] = useState(false);
  const [actionError, setActionError] = useState<string | null>(null);

  const live = useLiveData();

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
    refreshContextTimeline: live.refreshContextTimeline,
    resetTimelineRefreshGate: live.resetTimelineRefreshGate,
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

  useEffect(() => {
    void refreshHealth();
    void live.refreshLatest();
    void refreshAppRules();
    void refreshDeployStatus();
    void hydrateActiveSession();
  }, [hydrateActiveSession, refreshHealth, live.refreshLatest, refreshAppRules, refreshDeployStatus]);

  useEffect(() => {
    if (!sessionId || sessionRecord?.status !== "ACTIVE") {
      return;
    }

    const timer = window.setInterval(() => {
      void live.refreshContextTimeline(sessionId);
    }, TIMELINE_POLL_MS);

    return () => window.clearInterval(timer);
  }, [sessionId, sessionRecord?.status, live.refreshContextTimeline]);

  useEffect(() => {
    const unsubs: Array<Promise<() => void>> = [];
    unsubs.push(
      api.onCaptureFailed((payload) => {
        applyCaptureFailure(payload);
      }),
    );
    unsubs.push(
      api.onPrediction((record) => {
        live.handlePrediction(record);
        if (record.sessionId === sessionId && sessionRecord?.status === "ACTIVE") {
          live.refreshTimelineFromEvent(record.sessionId);
        }
      }),
    );
    unsubs.push(
      api.onSnapback((payload) => {
        live.handleSnapback(payload);
        if (sessionRecord?.status === "ACTIVE") {
          live.refreshTimelineFromEvent(sessionId);
        }
      }),
    );
    unsubs.push(
      api.onHyperfocus((payload) => {
        live.handleHyperfocus(payload);
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
  }, [
    applyCaptureFailure,
    live.handleHyperfocus,
    live.handlePrediction,
    live.handleSnapback,
    live.refreshTimelineFromEvent,
    sessionId,
    sessionRecord?.status,
  ]);

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
          hyperfocusNote={live.hyperfocusNote}
          prediction={live.prediction}
          riskBadgeLabel={live.riskBadgeLabel}
          riskClass={live.riskClass}
          signals={live.signals}
          snapbackNote={live.snapbackNote}
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
          contextTimeline={live.contextTimeline}
          historyLimit={HISTORY_LIMIT}
          predictionHistory={live.predictionHistory}
          refreshContextTimeline={live.refreshContextTimeline}
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
