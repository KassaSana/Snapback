import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { api } from "./api";
import { afterFirstPaint } from "./frontendReady";

import { useAppEffects } from "./useAppEffects";

import { PredictionHistoryCard } from "./ActivityCards";
import { AnalyticsCard } from "./AnalyticsCard";
import { DailyTrendCard } from "./DailyTrendCard";
import { DiagnosticsCard } from "./DiagnosticsCard";
import { GoalCategoriesCard } from "./GoalCategoriesCard";
import { ActionErrorBanner } from "./ActionErrorBanner";
import { AppHeader } from "./AppHeader";
import { InsightsCard, SessionManagementCard } from "./InsightsCard";
import { FocusStateHero } from "./FocusStateHero";
import { SignalsCard } from "./SignalsCard";
import { RulesCard } from "./RulesCard";
import { SessionExplorerCard } from "./SessionExplorerCard";
import { SettingsCard } from "./SettingsCard";
import { SummaryCard } from "./SummaryCard";
import { PermissionsCard } from "./PermissionsCard";
import { PrivacyCard } from "./PrivacyCard";
import { PermissionWizard } from "./PermissionWizard";
import { AttendedTargetsCard } from "./AttendedTargetsCard";
import { PomodoroCard } from "./PomodoroCard";
import { SessionControlCard } from "./SessionControlCard";
import { SessionTechnicalDetails } from "./SessionTechnicalDetails";
import { recentGoals } from "./sessionCockpit";
import { sessionStatusLabel } from "./sessionStatus";
import { nowSurfaceMode } from "./nowSurface";
import { SessionReviewCards } from "./SessionReviewCards";
import { FocusFeedbackCard } from "./FocusFeedbackCard";
import { TrainingDeployCard } from "./TrainingDeployCard";
import { useAppRules } from "./useAppRules";
import { useFeedback } from "./useFeedback";
import { useHealth, useCaptureWarmupExpired } from "./useHealth";
import { useLiveData } from "./useLiveData";
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
import { WorkAppTeachCard } from "./WorkAppTeachCard";
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
import { readWorkAppTeachComplete, writeWorkAppTeachComplete } from "./workAppTeach";
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

export default function App() {
  useEffect(() => afterFirstPaint(() => {
    void api.notifyFrontendReady().catch((error: unknown) => {
      console.warn("Could not acknowledge frontend readiness", error);
    });
  }), []);
  // Which surface is showing (ADR-0003); Now by default.
  const [surface, setSurface] = useState<Surface>("now");
  // Settings' second level, seeded from the URL hash (e.g. "#settings/privacy").
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

  // Where a clicked native alert lands. The native side already raised the window and chose the
  // destination (src/app/alert_routing.hpp); this maps it to a screen. `focus` names a region,
  // not an element id.
  const applyAlertDestination = useCallback((destination: AlertDestination) => {
    setSurface(destination.surface);
    if (destination.focus === null) return;
    // Next frame, so a surface switch has rendered. Looked up by data attribute and
    // optional-chained throughout: a failure here has no visible surface.
    requestAnimationFrame(() => {
      document
        .querySelector(`[data-alert-region="${destination.focus}"]`)
        ?.scrollIntoView({ behavior: "smooth", block: "center" });
    });
  }, []);

  // Skipping and finishing are the same durable state.
  const [onboardingComplete, setOnboardingComplete] = useState(() => readOnboardingComplete());
  const [workAppTeachDone, setWorkAppTeachDone] = useState(() => readWorkAppTeachComplete());
  // Latched, because "has read the recap" is a thing that happened, not a thing that is true
  // right now — navigating away from Review must not un-finish the journey.
  const [recapSeen, setRecapSeen] = useState(false);
  const skipOnboarding = useCallback(() => {
    writeOnboardingComplete();
    setOnboardingComplete(true);
  }, []);
  const [onboardingReplay, setOnboardingReplay] = useState(false);
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
    applyRecordingStatusEvent,
    recordingStatus,
    recordingStatusUnconfirmed,
    refreshRecordingStatus,
    handlePausePrivately,
    handleResumeRecording,
    handleResumeAlerts,
  } = useRecordingStatus({ setActionError: feedback.setActionError });

  const {
    activeWindowAvailable,
    applyClassifierStatus,
    applyPersistenceFailure,
    captureEventsDropped,
    captureFailed,
    captureFailureReason,
    captureProbeConfirmed,
    captureRunning,
    captureStalled,
    captureWarmupExpired,
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
  } = useHealth();

  // A real, actionable failure may reveal the Settings section that fixes it, once per run.
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
    autoLabel,
    focusMode,
    cancelSwitch,
    handleFocusModeChange,
    setDraftFocusMode,
    handleLabel,
    handleSaveReflection,
    handleSkipReflection,
    handleSkipSurvey,
    handleChangeSessionRating,
    handleStartSession,
    handleStopSession,
    handleSwitchSession,
    hydrateActiveSession,
    recap,
    sessionGoal,
    sessionId,
    sessionPending,
    labelPending,
    sessionRecord,
    setSessionGoal,
    reflectionPending,
    reflectionSaved,
    savedSessionRating,
    surveyPending,
  } = useSession({
    refreshContextTimeline: live.refreshContextTimeline,
    resetTimelineRefreshGate: live.resetTimelineRefreshGate,
    clearSessionLiveSignals: live.clearSessionLiveSignals,
    setActionError: feedback.setActionError,
    setLabelStatus: feedback.setLabelStatus,
    setLabelStatusWarning: feedback.setLabelStatusWarning,
    captureReadiness,
  });

  const restartOnboarding = useCallback(() => {
    clearOnboardingComplete();
    setRecapSeen(false);
    setOnboardingComplete(false);
    setOnboardingReplay(true);
    if (!sessionRecord || sessionRecord.endedAtMs !== null) setSessionGoal("");
    setSurface("now");
  }, [sessionRecord, setSessionGoal]);
  useEffect(() => { if (sessionRecord?.endedAtMs === null) setOnboardingReplay(false); }, [sessionRecord]);

  // Running or paused: a paused session accrues no attended time (ADR-0005). Derived here
  // because the idle signal lives in useLiveData.
  const liveSessionStatusLabel = useMemo(
    () => sessionStatusLabel(sessionRecord, live.userIdle, recordingStatus.state),
    [live.userIdle, sessionRecord, recordingStatus.state],
  );
  const sessionActive = sessionRecord?.status === "ACTIVE";
  const nowMode = nowSurfaceMode({ sessionActive, recap });

  const {
    canTrainFromExport,
    cancelRequested,
    copyStatus,
    deployMessage,
    deployMessageWarning,
    deployStatus,
    exportInProgress,
    handleCopyTrainingCommand,
    handleExportTrainingData,
    handleReloadClassifierModel,
    handleRollbackClassifierModel,
    handleCancelTraining,
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
    trainingProgress,
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


  // Deleting one session affects aggregates on other surfaces, and if it was the running one
  // the live engine state is gone too.
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
    dailySummary,
    deleteError: sessionDeleteError,
    deleteSession: handleDeleteSession,
    deleteStatus: sessionDeleteStatus,
    deletingSessionId,
    displayedRange: reviewDisplayedRange,
    error: reviewError,
    exportStatus,
    exportSummary,
    focusSummary,
    invalidateReview,
    loading: reviewLoading,
    range: reviewRange,
    refreshReview,
    reflectionStatus,
    report: summaryReport,
    saveReflection: handleEditReflection,
    sessionHistory,
    setRange: setReviewRange,
    staleInterval: reviewStaleInterval,
  } = useReviewWorkflow({
    active: surface === "review",
    onSessionDeleted: handleSessionDeleted,
  });

  // Cards are labelled with the interval their data came from, which lags the selected one
  // during a load or after a failure.
  const reviewRangeLabelText = useMemo(
    () => reviewRangeLabel(reviewDisplayedRange),
    [reviewDisplayedRange],
  );

  // Recent goals come from unfiltered history, not the Review range.
  const cockpitRecentGoals = useMemo(() => recentGoals(cockpitHistory), [cockpitHistory]);

  const handleActivityDataDeleted = useCallback(async () => {
    clearActivitySession();
    live.clearActivityData();
    invalidateReview();
    await Promise.all([
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
    invalidateReview,
    refreshAttendedProgress,
    refreshRecordingStatus,
    refreshPomodoroStatus,
  ]);
  const privacy = usePrivacy({
    onActivityDataDeleted: handleActivityDataDeleted,
    onPrivateModeChanged: refreshRecordingStatus,
  });

  // Re-read private mode whenever the recording state moves (header, tray, or lapsed deadline),
  // so the Settings toggle cannot contradict the header. The initial answer is skipped.
  const previousRecordingState = useRef<string | null>(null);
  const refreshPrivacySettings = privacy.refresh;
  useEffect(() => {
    const previous = previousRecordingState.current;
    previousRecordingState.current = recordingStatus.state;
    if (previous !== null && previous !== recordingStatus.state) {
      void refreshPrivacySettings();
    }
  }, [recordingStatus.state, refreshPrivacySettings]);
  const dataImport = useDataImport();

  // Every input is state the app already tracks; the guide issues nothing. `feedbackGiven` uses
  // labelStatus, set by both a correction and a skip.
  const readingWarmupExpired = useCaptureWarmupExpired(sessionActive && !live.prediction && recordingStatus.state === "recording", sessionId);
  const captureDiagnosis = captureFailed ? (captureFailureReason ?? "Input capture stopped. Check Privacy settings.")
    : captureWarmupExpired || readingWarmupExpired ? "No input reading has arrived after 90 seconds. Check Privacy settings and refresh permissions; capture may be unconfirmed." : null;
  const onboardingState = useMemo(
    () => ({
      captureReady: captureIsReady(captureRunning, captureProbeConfirmed),
      goalEntered: sessionGoal.trim().length > 0,
      sessionActive,
      predictionSeen: live.prediction !== null,
      feedbackGiven: feedback.labelStatus !== null,
      sessionCompleted: recap !== null && !onboardingReplay,
      recapSeen: recapSeen && !onboardingReplay,
    }),
    [
      captureProbeConfirmed,
      captureRunning,
      onboardingReplay,
      feedback.labelStatus,
      live.prediction,
      recap,
      recapSeen,
      sessionGoal,
      sessionActive,
    ],
  );
  const onboardingStep = currentOnboardingStep(onboardingState);
  const onboardingVisible = shouldShowOnboarding({
    captureReady: onboardingState.captureReady,
    completed: onboardingComplete,
    step: onboardingStep,
  });

  // Asked once: an import staged in a previous run may still be waiting.
  useEffect(() => {
    void dataImport.refreshImportStatus();
  }, [dataImport.refreshImportStatus]);

  // The last step completes when the recap is seen.
  useEffect(() => {
    if (recap !== null && surface === "review" && !onboardingReplay) setRecapSeen(true);
  }, [recap, surface, onboardingReplay]);

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
    captureProbeConfirmed,
    invalidateReview,
    refreshPomodoroStatus,
    refreshAttendedProgress,
    refreshRecordingStatus,
    applyRecordingStatusEvent,
    refreshLatest: live.refreshLatest,
    refreshAppRules,
    refreshDeployStatus,
    hydrateActiveSession,
    sessionId,
    sessionStatus: sessionRecord?.status ?? null,
    refreshContextTimeline: live.refreshContextTimeline,
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
        surface={surface}
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
        recordingStatus={recordingStatus}
        recordingStatusUnconfirmed={recordingStatusUnconfirmed}
        onPauseRecording={handlePausePrivately}
        onResumeRecording={handleResumeRecording}
        onResumeAlerts={handleResumeAlerts}
        sessionActive={sessionActive}
        activeGoal={sessionRecord?.goal ?? null}
      />

      <ActionErrorBanner
        error={feedback.actionError ?? overlayFailureReason}
        onDismiss={() => {
          feedback.setActionError(null);
          setOverlayFailureReason(null);
        }}
      />

      <ActionErrorBanner error={persistenceFailureReason} />

      <SurfaceNav active={surface} onChange={setSurface} />

      {/* One panel element, swapped content — ADR-0003. Cards move between surfaces by
          composition; none of them were rewritten to get here. */}
      <main
        className={
          surface === "now"
            ? "grid grid-now"
            : surface === "review"
              ? "grid grid-review"
              : "grid"
        }
        role="tabpanel"
        id={surfacePanelId(surface)}
        aria-labelledby={surfaceTabId(surface)}
        tabIndex={-1}
      >
        {surface === "now" && (
          <>
        {/* Above the cockpit, where every step but the last happens. */}
        {onboardingVisible && onboardingStep && (
          <OnboardingGuide
            step={onboardingStep}
            failure={onboardingFailure({
              captureFailed,
              captureUnverified: captureWarmupExpired || readingWarmupExpired,
              permissionBlocked: !permissionCaptureAvailable && healthStatus !== "checking",
              privateMode: privacy.settings?.privateMode ?? false,
            })}
            onSkip={skipOnboarding}
            onRecover={() => openSettingsSection("privacy")}
          />
        )}

        <div data-alert-region="session">
          <SessionControlCard
            focusMode={focusMode}
            setDraftFocusMode={setDraftFocusMode}
            cancelSwitch={cancelSwitch}
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

        {sessionActive && (
          <FocusStateHero
            captureDiagnosis={captureDiagnosis}
            recordingPaused={recordingStatus.state === "pausedPrivate" || recordingStatus.state === "pausedIdle"}
            onOpenPrivacy={() => openSettingsSection("privacy")}
            goal={sessionRecord?.goal ?? null}
            hyperfocusNote={live.hyperfocusNote}
            labelStatus={feedback.labelStatus}
            onCorrectVerdict={(label) => {
              // Record the classifier's verdict alongside the correction, in notes.
              const predicted = live.prediction?.focusState ?? "unknown";
              void handleLabel(label, "manual", `corrected:${predicted}`);
            }}
            onDismissSnapback={live.handleDismissSnapback}
            onRestoreSnapbackTarget={live.handleRestoreSnapbackTarget}
            prediction={live.prediction}
            sessionActive={sessionActive}
            snapbackNote={live.snapbackNote}
          />
        )}

        {nowMode === "running" ? (
          <WorkAppTeachCard
            appRules={appRules}
            contextTimeline={live.contextTimeline}
            dismissed={workAppTeachDone}
            onCreateAppRule={handleCreateQuickRule}
            onDismiss={() => {
              writeWorkAppTeachComplete();
              setWorkAppTeachDone(true);
            }}
          />
        ) : null}

        {nowMode === "stopped" ? (
          <SessionReviewCards
            autoLabel={autoLabel}
            handleLabel={handleLabel}
            handleSkipSurvey={handleSkipSurvey}
            handleChangeSessionRating={handleChangeSessionRating}
            labelPending={labelPending}
            labelStatus={feedback.labelStatus}
            labelStatusWarning={feedback.labelStatusWarning}
            savedSessionRating={savedSessionRating}
            recap={recap}
            surveyPending={surveyPending}
            reflectionPending={reflectionPending}
            reflectionSaved={reflectionSaved}
            handleSaveReflection={handleSaveReflection}
            handleSkipReflection={handleSkipReflection}
          />
        ) : null}

        {nowMode === "running" ? (
          <>
            <AttendedTargetsCard
              compact
              progress={attendedProgress}
              onSave={handleSaveAttendedTargets}
            />

            <div data-alert-region="pomodoro">
            <PomodoroCard
              pomodoroStatus={pomodoroStatus}
              pomodoroConfig={pomodoroConfig}
              sessionActive={sessionActive}
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
        ) : null}
          </>
        )}

        {surface === "review" && (
          <>
            <ReviewRangeBar
              disabled={reviewLoading}
              loading={reviewLoading}
              range={reviewRange}
              onChange={setReviewRange}
              showingLabel={reviewStaleInterval ? reviewRangeLabelText : null}
              error={reviewError}
              onRetry={() => void refreshReview()}
            />

            <SummaryCard
              focusSummary={focusSummary}
              exportStatus={exportStatus}
              onExport={() => void exportSummary()}
              rangeLabel={reviewRangeLabelText}
              report={summaryReport}
            />
            {/* "Start this again" fills the start form on Now and goes there; the
                session still begins only when the user presses Start (ADR-0005). */}
            <SessionExplorerCard
              sessionHistory={sessionHistory}
              rangeLabel={reviewRangeLabelText}
              sessionActive={sessionActive}
              appRules={appRules}
              onCreateAppRule={handleCreateQuickRule}
              onStartAgain={(goal, mode) => {
                setSessionGoal(goal);
                setDraftFocusMode(mode);
                setSurface("now");
              }}
              deletingSessionId={deletingSessionId}
              onDeleteSession={handleDeleteSession}
            />

            <InsightsCard
              rangeLabel={reviewRangeLabelText}
              sessionHistory={sessionHistory}
              truncationNote={
                summaryReport.sessionsTruncated
                  ? `latest ${summaryReport.sessionLimit} sessions only`
                  : null
              }
            />

            <AnalyticsCard
              analytics={analytics}
              appRules={appRules}
              onCreateAppRule={handleCreateQuickRule}
              rangeLabel={reviewRangeLabelText}
            />

            <DailyTrendCard
              dailySummary={dailySummary}
              rangePreset={reviewRange.preset}
              rangeLabel={reviewRangeLabelText}
            />

            <SessionManagementCard
              deleteError={sessionDeleteError}
              deleteStatus={sessionDeleteStatus}
              deletingSessionId={deletingSessionId}
              onDeleteSession={handleDeleteSession}
              onSaveReflection={handleEditReflection}
              reflectionStatus={reflectionStatus}
              sessionHistory={sessionHistory}
            />
          </>
        )}

        {surface === "settings" && (
          <>
        {/* Only the active Settings group renders (ADR-0003's surfaces are unchanged). */}
        <SettingsNav active={settingsSection} onChange={setSettingsSection} />
        <div
          className="settings-section"
          role="tabpanel"
          id={settingsPanelId(settingsSection)}
          aria-labelledby={settingsTabId(settingsSection)}
        >
        {/* The panel holds the section's controls; a subgrid keeps the surface's columns. */}
        <div className="settings-section-intro">
          <h2 className="settings-section-title">
            {SETTINGS_SECTION_LABELS[settingsSection]}
          </h2>
          <p className="helper-text">{SETTINGS_SECTION_BLURBS[settingsSection]}</p>
        </div>

        {settingsSection === "general" && (
          <>
          {/* Replaying the guide is always safe: it reads state and creates nothing. */}
          <section className="settings-help">
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
              sessionActive={sessionActive}
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
              captureWarmupExpired={captureWarmupExpired}
              onRefreshPermissions={handleRefreshPermissions}
              onRequestPermissions={handleRequestPermissions}
              permissionMessage={permissionMessage}
              permissionSteps={permissionSteps}
            />
          </>
        )}

        {/* Advanced: training, raw signals, and logs, collapsed by default. */}
        {settingsSection === "advanced" && (
          <>
            {developerToolsEnabled ? (
              <details className="settings-disclosure">
                <summary>Model training</summary>
                <TrainingDeployCard
            canTrainFromExport={canTrainFromExport}
            cancelRequested={cancelRequested}
            classifierBackend={classifierBackend}
            classifierModelId={classifierModelId}
            classifierModelPath={classifierModelPath}
            copyStatus={copyStatus}
            deployMessage={deployMessage}
            deployMessageWarning={deployMessageWarning}
            deployStatus={deployStatus}
            exportInProgress={exportInProgress}
            handleCopyTrainingCommand={handleCopyTrainingCommand}
            handleExportTrainingData={handleExportTrainingData}
            handleReloadClassifierModel={handleReloadClassifierModel}
            handleRollbackClassifierModel={handleRollbackClassifierModel}
            handleCancelTraining={handleCancelTraining}
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
                  trainingProgress={trainingProgress}
                />
              </details>
            ) : null}

            <details className="settings-disclosure">
              <summary>Logs and diagnostics</summary>
              <DiagnosticsCard />
              <SessionTechnicalDetails sessionId={sessionId} sessionRecord={sessionRecord} />
              <PredictionHistoryCard predictionHistory={live.predictionHistory} />
            </details>

            <details className="settings-disclosure">
              <summary>Raw signals</summary>
              <SignalsCard signals={live.signals} />
            </details>
          </>
        )}
        </div>
          </>
        )}
      </main>
    </div>
  );
}
