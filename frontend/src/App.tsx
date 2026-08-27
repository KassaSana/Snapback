import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { useAppEffects } from "./useAppEffects";

import { ActivityCards } from "./ActivityCards";
import { AnalyticsCard } from "./AnalyticsCard";
import { DiagnosticsCard } from "./DiagnosticsCard";
import { GoalCategoriesCard } from "./GoalCategoriesCard";
import { ActionErrorBanner } from "./ActionErrorBanner";
import { AppHeader } from "./AppHeader";
import { FocusSummaryCard } from "./FocusSummaryCard";
import { InsightsCard } from "./InsightsCard";
import { FocusStateHero } from "./FocusStateHero";
import { SignalsCard } from "./SignalsCard";
import { RulesCard } from "./RulesCard";
import { SettingsCard } from "./SettingsCard";
import { SummaryCard } from "./SummaryCard";
import { PermissionsCard } from "./PermissionsCard";
import { PrivacyCard } from "./PrivacyCard";
import { PermissionWizard } from "./PermissionWizard";
import { AttendedTargetsCard } from "./AttendedTargetsCard";
import { RecordingStatusCard } from "./RecordingStatusCard";
import { PomodoroCard } from "./PomodoroCard";
import { SessionControlCard } from "./SessionControlCard";
import { recentGoals } from "./sessionCockpit";
import { sessionStatusLabel } from "./sessionStatus";
import { SessionReviewCards } from "./SessionReviewCards";
import { FocusFeedbackCard } from "./FocusFeedbackCard";
import { TrainingDeployCard } from "./TrainingDeployCard";
import { useAppRules } from "./useAppRules";
import { useFeedback } from "./useFeedback";
import { useHealth } from "./useHealth";
import { HISTORY_LIMIT, useLiveData } from "./useLiveData";
import { useCockpitHistory } from "./useCockpitHistory";
import { useAppearance } from "./useAppearance";
import { ReviewRangeBar } from "./ReviewRangeBar";
import { reviewRangeLabel } from "./reviewRange";
import { useReviewWorkflow } from "./useReviewWorkflow";
import { useAttendedTargets } from "./useAttendedTargets";
import { useRecordingStatus } from "./useRecordingStatus";
import { usePomodoro } from "./usePomodoro";
import { useTrainingDeploy } from "./useTrainingDeploy";
import { useSession } from "./useSession";
import { useAutostart } from "./useAutostart";
import { useAlertDelivery } from "./useAlertDelivery";
import { useIdleThreshold } from "./useIdleThreshold";
import { usePrivacy } from "./usePrivacy";
import type { AlertDestination } from "./alertDestination";
import { SurfaceNav, surfacePanelId, surfaceTabId, type Surface } from "./SurfaceNav";
import { SettingsNav } from "./SettingsNav";
import { OnboardingGuide } from "./OnboardingGuide";
import { DataImportCard } from "./DataImportCard";
import { useDataImport } from "./useDataImport";
import { captureIsReady } from "./permissionWizardState";
import {
  clearOnboardingComplete,
  currentOnboardingStep,
  onboardingFailure,
  readOnboardingComplete,
  shouldShowOnboarding,
  writeOnboardingComplete,
} from "./onboardingJourney";
import {
  DEFAULT_SETTINGS_SECTION,
  SETTINGS_SECTION_BLURBS,
  SETTINGS_SECTION_LABELS,
  parseSettingsDeepLink,
  settingsPanelId,
  settingsSectionForFailure,
  settingsTabId,
  type SettingsSection,
} from "./settingsSections";
import type { FocusLabel } from "./api";

export default function App() {
  // Which surface is showing (ADR-0003). Defaults to Now: it is the 95% case, and the
  // only one that matters while a session is running.
  const [surface, setSurface] = useState<Surface>("now");
  // Roadmap 10.9. Settings' second level. Seeded from the URL hash so a support instruction
  // ("open #settings/privacy") lands on the right group; a hash naming anything else leaves
  // both levels alone rather than bouncing the user somewhere they did not ask for.
  const [settingsSection, setSettingsSection] = useState<SettingsSection>(() => {
    const deepLink =
      typeof globalThis.location === "undefined"
        ? null
        : parseSettingsDeepLink(globalThis.location.hash);
    return deepLink ?? DEFAULT_SETTINGS_SECTION;
  });
  // Set once a failure has steered the user; without it the effect below would drag them back
  // to Privacy every time it re-ran, making the other sections unusable while capture is down.
  const failureRevealed = useRef(false);

  const openSettingsSection = useCallback((section: SettingsSection) => {
    setSurface("settings");
    setSettingsSection(section);
  }, []);

  // Roadmap 2.16. Where a clicked native alert lands. The native side already raised the
  // window and already decided *which* destination (src/app/alert_routing.hpp); this turns
  // that into a screen, which is the half it deliberately does not know about.
  //
  // `focus` names a region rather than an element id, so a destination that outlived the card
  // it meant cannot reach for a component that has since been renamed.
  const applyAlertDestination = useCallback((destination: AlertDestination) => {
    setSurface(destination.surface);
    if (destination.focus === null) return;
    // One frame later, because the surface above has not rendered yet: on a click that
    // switched surfaces, the region being scrolled to is not in the DOM at this point.
    //
    // Looked up by data attribute rather than by ref, so a destination cannot hold a component
    // that has since been renamed or unmounted. This runs inside a host event listener, where
    // a thrown exception has no user-visible failure mode — the click would just silently do
    // nothing — so every step here is optional-chained rather than assumed.
    requestAnimationFrame(() => {
      document
        .querySelector(`[data-alert-region="${destination.focus}"]`)
        ?.scrollIntoView({ behavior: "smooth", block: "center" });
    });
  }, []);

  // Roadmap 2.12. Skipping and finishing are the same durable state: the guide is done.
  const [onboardingComplete, setOnboardingComplete] = useState(() => readOnboardingComplete());
  // Latched, because "has read the recap" is a thing that happened, not a thing that is true
  // right now — navigating away from Review must not un-finish the journey.
  const [recapSeen, setRecapSeen] = useState(false);
  const skipOnboarding = useCallback(() => {
    writeOnboardingComplete();
    setOnboardingComplete(true);
  }, []);
  const restartOnboarding = useCallback(() => {
    clearOnboardingComplete();
    setRecapSeen(false);
    setOnboardingComplete(false);
    setSurface("now");
  }, []);
  const feedback = useFeedback();
  const autostart = useAutostart();
  const idleThreshold = useIdleThreshold();
  const alertDelivery = useAlertDelivery();
  const { mode: appearanceMode, setMode: setAppearanceMode } = useAppearance();

  const live = useLiveData();

  const {
    pomodoroStatus,
    pomodoroConfig,
    refreshPomodoroStatus,
    handlePomodoroEvent,
    handleStartPomodoro,
    handleStopPomodoro,
    handlePausePomodoro,
    handleResumePomodoro,
    handleSkipPomodoroPhase,
    handleRestartPomodoroPhase,
    handleAcknowledgePomodoroPhase,
    handleSavePomodoroConfig,
  } = usePomodoro({ setActionError: feedback.setActionError });

  const { attendedProgress, refreshAttendedProgress, handleSaveAttendedTargets } =
    useAttendedTargets({ setActionError: feedback.setActionError });

  const {
    recordingStatus,
    refreshRecordingStatus,
    handlePausePrivately,
    handleResumeRecording,
    handleResumeAlerts,
  } = useRecordingStatus({ setActionError: feedback.setActionError });

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
    classifierModelId,
    classifierModelPath,
    developerToolsEnabled,
    handleRefreshPermissions,
    handleRequestPermissions,
    healthStatus,
    modelDeploymentDegraded,
    overlayFailureReason,
    persistenceFailureReason,
    permissionCaptureAvailable,
    permissionMessage,
    permissionSteps,
    refreshHealth,
    setOverlayFailureReason,
    setPersistenceFailureReason,
  } = useHealth();

  // Roadmap 10.9's one exception to "Advanced stays put": a real, actionable failure may
  // reveal the section that can fix it. It fires at most once per app run — a user who has
  // been shown the problem and navigated away is not lost, and re-steering them would make
  // every other section unreachable for as long as the failure lasts.
  const failureSection = settingsSectionForFailure({
    permissionBlocked: !permissionCaptureAvailable && healthStatus !== "checking",
    captureFailed,
    modelFailed: modelDeploymentDegraded,
  });
  useEffect(() => {
    if (!failureSection || failureRevealed.current) return;
    failureRevealed.current = true;
    setSettingsSection(failureSection);
  }, [failureSection]);

  const captureReadiness = useMemo(
    () => ({
      captureRunning,
      captureFailed,
      permissionCaptureAvailable,
      activeWindowAvailable,
    }),
    [
      activeWindowAvailable,
      captureFailed,
      captureRunning,
      permissionCaptureAvailable,
    ],
  );

  const {
    clearActivitySession,
    focusMode,
    handleFocusModeChange,
    handleLabel,
    handleSaveReflection,
    handleSkipReflection,
    handleSkipSurvey,
    handleStartSession,
    handleStopSession,
    handleSwitchSession,
    hydrateActiveSession,
    recap,
    sessionGoal,
    sessionId,
    sessionPending,
    sessionRecord,
    setSessionGoal,
    reflectionPending,
    reflectionSaved,
    surveyPending,
  } = useSession({
    refreshContextTimeline: live.refreshContextTimeline,
    resetTimelineRefreshGate: live.resetTimelineRefreshGate,
    setActionError: feedback.setActionError,
    setLabelStatus: feedback.setLabelStatus,
    setLabelStatusWarning: feedback.setLabelStatusWarning,
    captureReadiness,
  });

  // Roadmap 7.23 / ADR-0005. A session is now running *or paused*, and the difference is
  // real: a paused session accrues no attended time. Derived here rather than in useSession
  // because the idle signal lives in useLiveData, and showing "active" for a session that
  // stopped counting twenty minutes ago is the confusion this whole item exists to remove.
  const liveSessionStatusLabel = useMemo(
    () => sessionStatusLabel(sessionRecord, live.userIdle),
    [live.userIdle, sessionRecord],
  );

  const {
    canTrainFromExport,
    copyStatus,
    deployMessage,
    deployMessageWarning,
    deployStatus,
    handleCopyTrainingCommand,
    handleExportTrainingData,
    handleReloadClassifierModel,
    handleRollbackClassifierModel,
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
    enabled: developerToolsEnabled,
    sessionId,
    setLabelStatus: feedback.setLabelStatus,
    setLabelStatusWarning: feedback.setLabelStatusWarning,
    onClassifierStatusChange: applyClassifierStatus,
  });

  const {
    appRules,
    handleAddAppRule,
    handleCreateQuickRule,
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


  // Deleting one session (Roadmap 7.6) invalidates less than deleting everything, but it is
  // not local to the Insights card: the aggregates on other surfaces counted that session's
  // predictions, and if it was the *running* session the native command already tore down the
  // live engine state, so the UI must stop showing a session that no longer exists.
  //
  const { refreshCockpitHistory, sessionHistory: cockpitHistory } = useCockpitHistory();

  const handleSessionDeleted = useCallback(
    async (deletedSessionId: string) => {
      if (deletedSessionId === sessionId) {
        clearActivitySession();
        live.clearActivityData();
      }
      await Promise.all([refreshCockpitHistory(), refreshHealth()]);
    },
    [
      clearActivitySession,
      live.clearActivityData,
      refreshCockpitHistory,
      refreshHealth,
      sessionId,
    ],
  );

  const {
    analytics,
    deleteError: sessionDeleteError,
    deleteSession: handleDeleteSession,
    deleteStatus: sessionDeleteStatus,
    deletingSessionId,
    error: reviewError,
    exportStatus,
    exportSummary,
    focusSummary,
    loading: reviewLoading,
    range: reviewRange,
    reflectionStatus,
    refreshReview,
    report: summaryReport,
    saveReflection: handleEditReflection,
    sessionHistory,
    setRange: setReviewRange,
  } = useReviewWorkflow(handleSessionDeleted);

  const reviewRangeLabelText = useMemo(() => reviewRangeLabel(reviewRange), [reviewRange]);

  // Roadmap 2.11. The cockpit's "recent goals" and Repeat last come from unfiltered history,
  // not the Review range — a user comparing last week should still be able to repeat yesterday.
  const cockpitRecentGoals = useMemo(() => recentGoals(cockpitHistory), [cockpitHistory]);

  const handleActivityDataDeleted = useCallback(async () => {
    clearActivitySession();
    live.clearActivityData();
    await Promise.all([
      refreshReview(),
      refreshCockpitHistory(),
      refreshHealth(),
      refreshPomodoroStatus(),
      refreshAttendedProgress(),
      refreshRecordingStatus(),
    ]);
  }, [
    clearActivitySession,
    live.clearActivityData,
    refreshCockpitHistory,
    refreshHealth,
    refreshReview,
    refreshAttendedProgress,
    refreshRecordingStatus,
    refreshPomodoroStatus,
  ]);
  const privacy = usePrivacy(handleActivityDataDeleted);
  const dataImport = useDataImport();

  // Roadmap 2.12. Every input is state the app already tracks for its own reasons — the guide
  // reads them and issues nothing. `feedbackGiven` uses `labelStatus` because it is set by both
  // submitting a correction and deliberately skipping the survey, and the step is "you have been
  // offered the correction and dealt with it", not "you disagreed with the classifier".
  const onboardingState = useMemo(
    () => ({
      captureReady: captureIsReady(captureRunning, captureProbeConfirmed),
      goalEntered: sessionGoal.trim().length > 0,
      sessionActive: sessionRecord?.status === "ACTIVE",
      predictionSeen: live.prediction !== null,
      feedbackGiven: feedback.labelStatus !== null,
      sessionCompleted: recap !== null,
      recapSeen,
    }),
    [
      captureProbeConfirmed,
      captureRunning,
      feedback.labelStatus,
      live.prediction,
      recap,
      recapSeen,
      sessionGoal,
      sessionRecord,
    ],
  );
  const onboardingStep = currentOnboardingStep(onboardingState);
  const onboardingVisible = shouldShowOnboarding({
    captureReady: onboardingState.captureReady,
    completed: onboardingComplete,
    step: onboardingStep,
  });

  // Roadmap 9.14. Asked once at launch: an import staged in a previous run is still waiting,
  // and the card must say so rather than offering to stage a second one over it.
  useEffect(() => {
    void dataImport.refreshImportStatus();
  }, [dataImport.refreshImportStatus]);

  // The last step completes by being *read*, so it latches when Review is open with a recap
  // on it rather than when the user clicks anything.
  useEffect(() => {
    if (surface === "review" && recap !== null) setRecapSeen(true);
  }, [surface, recap]);

  // Finishing is remembered the same way skipping is: the guide has done its job either way.
  useEffect(() => {
    if (onboardingState.recapSeen && !onboardingComplete) {
      writeOnboardingComplete();
      setOnboardingComplete(true);
    }
  }, [onboardingState.recapSeen, onboardingComplete]);

  useAppEffects({
    refreshHealth,
    captureRunning,
    refreshReview,
    refreshPomodoroStatus,
    refreshAttendedProgress,
    refreshRecordingStatus,
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
    handleUntrackedWork: live.handleUntrackedWork,
    handleIdle: live.handleIdle,
    applyAlertDestination,
    handlePomodoroEvent,
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
        onRequestPermissions={handleRequestPermissions}
        focusMode={focusMode}
        onFocusModeChange={handleFocusModeChange}
      />

      <AppHeader
        activeWindowAvailable={activeWindowAvailable}
        captureFailed={captureFailed}
        captureProbeConfirmed={captureProbeConfirmed}
        captureRunning={captureRunning}
        healthStatus={healthStatus}
        modelDeploymentDegraded={modelDeploymentDegraded}
        permissionCaptureAvailable={permissionCaptureAvailable}
        permissionMessage={permissionMessage}
        permissionSteps={permissionSteps}
        onOpenTechnicalDetails={openSettingsSection}
      />

      <ActionErrorBanner
        error={feedback.actionError ?? overlayFailureReason ?? persistenceFailureReason}
        onDismiss={() => {
          feedback.setActionError(null);
          setOverlayFailureReason(null);
          setPersistenceFailureReason(null);
        }}
      />

      <SurfaceNav active={surface} onChange={setSurface} />

      {/* One panel element, swapped content — ADR-0003. Cards move between surfaces by
          composition; none of them were rewritten to get here. */}
      <main
        className="grid"
        role="tabpanel"
        id={surfacePanelId(surface)}
        aria-labelledby={surfaceTabId(surface)}
        tabIndex={-1}
      >
        {surface === "now" && (
          <>
        {/*
          Roadmap 2.12. Above the cockpit because every step but the last happens there, and
          only while the journey is unfinished. It observes and never acts, which is what lets
          it be replayed without manufacturing a session or a label.
        */}
        {onboardingVisible && onboardingStep && (
          <OnboardingGuide
            step={onboardingStep}
            failure={onboardingFailure({
              captureFailed,
              privateMode: privacy.settings?.privateMode ?? false,
            })}
            onSkip={skipOnboarding}
            onRecover={() => openSettingsSection("privacy")}
          />
        )}

        <FocusStateHero
          goal={sessionRecord?.goal ?? null}
          hyperfocusNote={live.hyperfocusNote}
          labelStatus={feedback.labelStatus}
          onConfirmVerdict={() => {
            const state = live.prediction?.focusState as FocusLabel | undefined;
            if (state) void handleLabel(state, "manual", `agreed:${state}`);
          }}
          onCorrectVerdict={(label) => {
            // Record what the classifier said alongside the correction: agreement rate is
            // only computable if we know what was being corrected. `notes` is free text
            // and already exists, so this needs no schema change — a dedicated
            // `predicted_state` column is the proper fix once 7.3 lands migrations.
            const predicted = live.prediction?.focusState ?? "unknown";
            void handleLabel(label, "manual", `corrected:${predicted}`);
          }}
          onDismissSnapback={live.handleDismissSnapback}
          onRestoreSnapbackTarget={live.handleRestoreSnapbackTarget}
          prediction={live.prediction}
          verdictClass={live.verdictClass}
          sessionActive={sessionRecord?.status === "ACTIVE"}
          snapbackNote={live.snapbackNote}
        />


        <div data-alert-region="session">
        <SessionControlCard
          focusMode={focusMode}
          handleFocusModeChange={handleFocusModeChange}
          handleStartSession={handleStartSession}
          handleStopSession={handleStopSession}
          handleSwitchSession={handleSwitchSession}
          sessionGoal={sessionGoal}
          sessionId={sessionId}
          sessionPending={sessionPending}
          sessionRecord={sessionRecord}
          sessionStatusLabel={liveSessionStatusLabel}
          setSessionGoal={setSessionGoal}
          recentGoals={cockpitRecentGoals}
          untrackedNote={live.untrackedNote}
          dismissUntrackedNote={live.clearUntrackedNote}
        />
        </div>

        <RecordingStatusCard
          status={recordingStatus}
          onPause={handlePausePrivately}
          onResume={handleResumeRecording}
          onResumeAlerts={handleResumeAlerts}
        />

        <AttendedTargetsCard
          progress={attendedProgress}
          onSave={handleSaveAttendedTargets}
        />

        <div data-alert-region="pomodoro">
        <PomodoroCard
          pomodoroStatus={pomodoroStatus}
          pomodoroConfig={pomodoroConfig}
          sessionActive={sessionRecord?.status === "ACTIVE"}
          onStart={handleStartPomodoro}
          onStop={handleStopPomodoro}
          onPause={handlePausePomodoro}
          onResume={handleResumePomodoro}
          onSkip={handleSkipPomodoroPhase}
          onRestart={handleRestartPomodoroPhase}
          onAcknowledge={handleAcknowledgePomodoroPhase}
          onSaveConfig={handleSavePomodoroConfig}
        />
        </div>
          </>
        )}

        {surface === "review" && (
          <>
        <ReviewRangeBar
          disabled={reviewLoading}
          loading={reviewLoading}
          range={reviewRange}
          onChange={setReviewRange}
        />
        {reviewError ? <p className="helper-text alert">{reviewError}</p> : null}

        <InsightsCard
          deleteError={sessionDeleteError}
          deleteStatus={sessionDeleteStatus}
          deletingSessionId={deletingSessionId}
          onDeleteSession={handleDeleteSession}
          onSaveReflection={handleEditReflection}
          reflectionStatus={reflectionStatus}
          rangeAvgFocusScore={
            summaryReport.sampleCount > 0 ? summaryReport.avgFocusScore : null
          }
          rangeLabel={reviewRangeLabelText}
          sessionHistory={sessionHistory}
        />

        <AnalyticsCard
          analytics={analytics}
          appRules={appRules}
          onCreateAppRule={handleCreateQuickRule}
          rangeLabel={reviewRangeLabelText}
        />

        <SummaryCard
          exportStatus={exportStatus}
          onExport={() => void exportSummary()}
          rangeLabel={reviewRangeLabelText}
          report={summaryReport}
        />

        <FocusSummaryCard focusSummary={focusSummary} rangeLabel={reviewRangeLabelText} />

        <SessionReviewCards
          handleLabel={handleLabel}
          handleSkipSurvey={handleSkipSurvey}
          recap={recap}
          surveyPending={surveyPending}
          reflectionPending={reflectionPending}
          reflectionSaved={reflectionSaved}
          handleSaveReflection={handleSaveReflection}
          handleSkipReflection={handleSkipReflection}
        />

        <ActivityCards
          appRules={appRules}
          contextTimeline={live.contextTimeline}
          historyLimit={HISTORY_LIMIT}
          onCreateAppRule={handleCreateQuickRule}
          predictionHistory={live.predictionHistory}
          refreshContextTimeline={live.refreshContextTimeline}
          sessionId={sessionId}
        />


          </>
        )}

        {surface === "settings" && (
          <>
        {/*
          Roadmap 10.9. Settings is now four groups rather than one stream of eight cards.
          ADR-0003's three surfaces are untouched: this is a second level *inside* Settings.
          Only the active group renders, which is what makes the common settings reachable
          without scrolling past developer controls at the default 1100×760 window.
        */}
        <SettingsNav active={settingsSection} onChange={setSettingsSection} />
        <div
          className="settings-section"
          role="tabpanel"
          id={settingsPanelId(settingsSection)}
          aria-labelledby={settingsTabId(settingsSection)}
        >
          <h2 className="settings-section-title">
            {SETTINGS_SECTION_LABELS[settingsSection]}
          </h2>
          <p className="helper-text">{SETTINGS_SECTION_BLURBS[settingsSection]}</p>
        </div>

        {settingsSection === "general" && (
          <>
          {/*
            Roadmap 2.12's "resumable from Help". Safe to offer unconditionally because
            replaying the guide creates nothing — it reads state and points at controls.
          */}
          <section className="card">
            <div className="card-header">
              <h2>Getting started</h2>
            </div>
            <p className="helper-text">
              A short walkthrough of one real session, from naming a goal to reading the recap.
              It only points at the controls — it never starts or records anything for you.
            </p>
            <button type="button" className="secondary-button" onClick={restartOnboarding}>
              Replay the walkthrough
            </button>
          </section>

          <SettingsCard
            appearanceMode={appearanceMode}
            onAppearanceChange={setAppearanceMode}
            busy={autostart.busy}
            error={autostart.error}
            onAutostartChange={autostart.setEnabled}
            status={autostart.status}
            idleThresholdSecs={idleThreshold.seconds}
            idleThresholdBusy={idleThreshold.busy}
            idleThresholdError={idleThreshold.error}
            onIdleThresholdChange={idleThreshold.update}
            alerts={alertDelivery.alerts}
            alertsBusy={alertDelivery.busy}
            alertsError={alertDelivery.error}
            onAlertsChange={alertDelivery.update}
          />
          </>
        )}

        {settingsSection === "focus" && (
          <>
            <FocusFeedbackCard
              handleLabel={handleLabel}
              labelStatus={feedback.labelStatus}
              labelStatusWarning={feedback.labelStatusWarning}
            />

            <GoalCategoriesCard />

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
          </>
        )}

        {settingsSection === "privacy" && (
          <>
            <PrivacyCard
              busy={privacy.busy}
              dataFolderStatus={privacy.dataFolderStatus}
              error={privacy.error}
              exclusionWarning={privacy.exclusionWarning}
              exclusionInput={privacy.exclusionInput}
              exportStatus={privacy.exportStatus}
              deletionStatus={privacy.deletionStatus}
              deletionWarning={privacy.deletionWarning}
              deletionRetained={privacy.deletionRetained}
              onAddExclusion={privacy.addExclusion}
              onDeleteAllActivityData={privacy.deleteAllActivityData}
              onExportMyData={privacy.exportMyData}
              onOpenDataFolder={privacy.openDataFolder}
              onPrivateModeChange={privacy.setPrivateMode}
              onRemoveExclusion={privacy.removeExclusion}
              setExclusionInput={privacy.setExclusionInput}
              settings={privacy.settings}
            />

            <DataImportCard
              busy={dataImport.busy}
              candidate={dataImport.candidate}
              path={dataImport.path}
              pending={dataImport.pending}
              status={dataImport.status}
              warning={dataImport.warning}
              setPath={dataImport.setPath}
              onBrowse={dataImport.browseAndInspect}
              onInspect={dataImport.inspect}
              onConfirm={dataImport.confirm}
              onCancel={dataImport.cancel}
              onDismissCandidate={dataImport.dismissCandidate}
            />


            <PermissionsCard
              captureEventsDropped={captureEventsDropped}
              captureFailed={captureFailed}
              captureFailureReason={captureFailureReason}
              captureProbeConfirmed={captureProbeConfirmed}
              captureRunning={captureRunning}
              captureStalled={captureStalled}
              onRefreshPermissions={handleRefreshPermissions}
              onRequestPermissions={handleRequestPermissions}
              permissionMessage={permissionMessage}
              permissionSteps={permissionSteps}
            />
          </>
        )}

        {/*
          Advanced. Training, raw signals, and logs are collapsed by default — the item's
          explicit requirement, and the reason Settings no longer opens on model tooling.
          They stay one click away rather than moving somewhere else.
        */}
        {settingsSection === "advanced" && (
          <>
            {developerToolsEnabled ? (
              <details className="settings-disclosure">
                <summary>Model training</summary>
                <TrainingDeployCard
            canTrainFromExport={canTrainFromExport}
            classifierBackend={classifierBackend}
            classifierModelId={classifierModelId}
            classifierModelPath={classifierModelPath}
            copyStatus={copyStatus}
            deployMessage={deployMessage}
            deployMessageWarning={deployMessageWarning}
            deployStatus={deployStatus}
            handleCopyTrainingCommand={handleCopyTrainingCommand}
            handleExportTrainingData={handleExportTrainingData}
            handleReloadClassifierModel={handleReloadClassifierModel}
            handleRollbackClassifierModel={handleRollbackClassifierModel}
            handleSaveRepoPath={handleSaveRepoPath}
            handleTrainFromExport={handleTrainFromExport}
            modelReloadStatus={modelReloadStatus}
            repoPathInput={repoPathInput}
            setRepoPathInput={setRepoPathInput}
            setShowAdvancedCommand={setShowAdvancedCommand}
            showAdvancedCommand={showAdvancedCommand}
            trainFromExportHint={trainFromExportHint}
                  trainingCommand={trainingCommand}
                  trainingInProgress={trainingInProgress}
                />
              </details>
            ) : null}

            <details className="settings-disclosure">
              <summary>Logs and diagnostics</summary>
              <DiagnosticsCard />
            </details>

            <details className="settings-disclosure">
              <summary>Raw signals</summary>
              <SignalsCard signals={live.signals} />
            </details>
          </>
        )}
          </>
        )}
      </main>
    </div>
  );
}
