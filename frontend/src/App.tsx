import { useAppEffects } from "./useAppEffects";

import { ActivityCards } from "./ActivityCards";
import { ActionErrorBanner } from "./ActionErrorBanner";
import { AppHeader } from "./AppHeader";
import { InsightsCard } from "./InsightsCard";
import { LiveStatusCards } from "./LiveStatusCards";
import { RulesCard } from "./RulesCard";
import { PermissionsCard } from "./PermissionsCard";
import { PermissionWizard } from "./PermissionWizard";
import { SessionControlCard } from "./SessionControlCard";
import { SessionReviewCards } from "./SessionReviewCards";
import { TrainingDeployCard } from "./TrainingDeployCard";
import { useAppRules } from "./useAppRules";
import { useFeedback } from "./useFeedback";
import { useHealth } from "./useHealth";
import { useInsights } from "./useInsights";
import { HISTORY_LIMIT, useLiveData } from "./useLiveData";
import { useTrainingDeploy } from "./useTrainingDeploy";
import { useSession } from "./useSession";

export default function App() {
  const feedback = useFeedback();

  const live = useLiveData();

  const { sessionHistory, refreshInsights } = useInsights();

  const {
    activeWindowAvailable,
    applyCaptureFailure,
    applyClassifierStatus,
    applyOverlayFailure,
    applyPersistenceFailure,
    captureEventsDropped,
    captureFailed,
    captureFailureReason,
    captureProbeConfirmed,
    captureRunning,
    captureStalled,
    classifierBackend,
    classifierModelPath,
    classifierOnnxRuntimeEnabled,
    handleRefreshPermissions,
    healthStatus,
    overlayFailureReason,
    persistenceFailureReason,
    permissionCaptureAvailable,
    permissionMessage,
    permissionSteps,
    refreshHealth,
    setOverlayFailureReason,
    setPersistenceFailureReason,
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
    setActionError: feedback.setActionError,
    setLabelStatus: feedback.setLabelStatus,
    setLabelStatusWarning: feedback.setLabelStatusWarning,
    captureReadiness: {
      captureRunning,
      captureFailed,
      permissionCaptureAvailable,
      activeWindowAvailable,
    },
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
    setLabelStatus: feedback.setLabelStatus,
    setLabelStatusWarning: feedback.setLabelStatusWarning,
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

  useAppEffects({
    refreshHealth,
    captureRunning,
    refreshInsights,
    refreshLatest: live.refreshLatest,
    refreshAppRules,
    refreshDeployStatus,
    hydrateActiveSession,
    sessionId,
    sessionStatus: sessionRecord?.status ?? null,
    refreshContextTimeline: live.refreshContextTimeline,
    applyCaptureFailure,
    applyOverlayFailure,
    applyPersistenceFailure,
    handlePrediction: live.handlePrediction,
    handleSnapback: live.handleSnapback,
    handleHyperfocus: live.handleHyperfocus,
    refreshTimelineFromEvent: live.refreshTimelineFromEvent,
    setLabelStatus: feedback.setLabelStatus,
    setLabelStatusWarning: feedback.setLabelStatusWarning,
  });

  return (
    <div className="app">
      <PermissionWizard
        healthChecked={healthStatus !== "checking"}
        captureProbeConfirmed={captureProbeConfirmed}
        captureRunning={captureRunning}
        permissionMessage={permissionMessage}
        permissionSteps={permissionSteps}
        onRefreshPermissions={handleRefreshPermissions}
      />

      <AppHeader
        activeWindowAvailable={activeWindowAvailable}
        captureFailed={captureFailed}
        captureProbeConfirmed={captureProbeConfirmed}
        captureRunning={captureRunning}
        classifierBackend={classifierBackend}
        classifierMetrics={deployStatus?.metrics ?? null}
        classifierModelPath={classifierModelPath}
        classifierOnnxRuntimeEnabled={classifierOnnxRuntimeEnabled}
        healthStatus={healthStatus}
        permissionCaptureAvailable={permissionCaptureAvailable}
        permissionMessage={permissionMessage}
        permissionSteps={permissionSteps}
      />

      <ActionErrorBanner
        error={feedback.actionError ?? overlayFailureReason ?? persistenceFailureReason}
        onDismiss={() => {
          feedback.setActionError(null);
          setOverlayFailureReason(null);
          setPersistenceFailureReason(null);
        }}
      />

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
          labelStatus={feedback.labelStatus}
          labelStatusWarning={feedback.labelStatusWarning}
          modelReloadStatus={modelReloadStatus}
          repoPathInput={repoPathInput}
          setRepoPathInput={setRepoPathInput}
          setShowAdvancedCommand={setShowAdvancedCommand}
          showAdvancedCommand={showAdvancedCommand}
          trainFromExportHint={trainFromExportHint}
          trainingCommand={trainingCommand}
          trainingInProgress={trainingInProgress}
        />

        <InsightsCard sessionHistory={sessionHistory} />

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
          captureEventsDropped={captureEventsDropped}
          captureFailed={captureFailed}
          captureFailureReason={captureFailureReason}
          captureProbeConfirmed={captureProbeConfirmed}
          captureRunning={captureRunning}
          captureStalled={captureStalled}
          onRefreshPermissions={handleRefreshPermissions}
          permissionMessage={permissionMessage}
          permissionSteps={permissionSteps}
        />
      </main>
    </div>
  );
}
