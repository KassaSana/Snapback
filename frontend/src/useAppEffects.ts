import { useEffect } from "react";

import {
  api,
  type CaptureFailurePayload,
  type OverlayFailurePayload,
  type PomodoroStatus,
  type PredictionRecord,
  type SnapbackPayload,
} from "./api";
import type { AlertDestination } from "./alertDestination";
import { CAPTURE_STALL_RECHECK_MS, HEALTH_POLL_MS, shouldPollHealth } from "./healthPoll";
import { TIMELINE_POLL_MS } from "./useLiveData";

type UseAppEffectsArgs = {
  refreshHealth: () => void | Promise<void>;
  captureRunning: boolean;
  refreshReview: () => void | Promise<void>;
  refreshPomodoroStatus: () => void | Promise<void>;
  // Roadmap 2.19. Refreshed alongside the timer: both describe the session that just changed.
  refreshAttendedProgress: () => void | Promise<void>;
  // Roadmap 2.10. Polled with the rest: a timed pause lapses on the backend's clock, so the
  // header learns it has ended the same way it learns anything else.
  refreshRecordingStatus: () => void | Promise<void>;
  refreshLatest: () => void | Promise<void>;
  refreshAppRules: () => void | Promise<void>;
  refreshDeployStatus: () => void | Promise<void>;
  hydrateActiveSession: () => void | Promise<void>;

  sessionId: string | null;
  sessionStatus: string | null;

  refreshContextTimeline: (sid?: string | null) => void | Promise<void>;

  applyCaptureFailure: (payload: CaptureFailurePayload) => void;
  applyOverlayFailure: (payload: OverlayFailurePayload) => void;
  applyPersistenceFailure: (payload: { reason: string; message: string }) => void;

  handlePrediction: (record: PredictionRecord | null) => void;
  handleSnapback: (payload: SnapbackPayload) => void;
  handleHyperfocus: (payload: { message: string }) => void;
  handleUntrackedWork: (payload: { message: string }) => void;
  handleIdle: (payload: { idle: boolean }) => void;
  /**
   * Roadmap 2.16. A native alert was clicked and named a destination this side owns. The
   * window has already been raised natively by the time this runs — all that is left is to
   * put the right thing in front of the user.
   */
  applyAlertDestination: (destination: AlertDestination) => void;
  handlePomodoroEvent: (status: PomodoroStatus) => void;
  refreshTimelineFromEvent: (sid?: string | null) => void;

  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
};

export const useAppEffects = ({
  refreshHealth,
  captureRunning,
  refreshReview,
  refreshPomodoroStatus,
  refreshAttendedProgress,
  refreshRecordingStatus,
  refreshLatest,
  refreshAppRules,
  refreshDeployStatus,
  hydrateActiveSession,
  sessionId,
  sessionStatus,
  refreshContextTimeline,
  applyCaptureFailure,
  applyOverlayFailure,
  applyPersistenceFailure,
  handlePrediction,
  handleSnapback,
  handleHyperfocus,
  handleUntrackedWork,
  handleIdle,
  applyAlertDestination,
  handlePomodoroEvent,
  refreshTimelineFromEvent,
  setLabelStatus,
  setLabelStatusWarning,
}: UseAppEffectsArgs) => {
  useEffect(() => {
    void refreshHealth();
    void refreshLatest();
    void refreshAppRules();
    void refreshDeployStatus();
    void refreshReview();
    void hydrateActiveSession();
  }, [
    hydrateActiveSession,
    refreshHealth,
    refreshLatest,
    refreshAppRules,
    refreshDeployStatus,
    refreshReview,
  ]);

  useEffect(() => {
    if (sessionStatus === "COMPLETED") {
      void refreshReview();
    }
  }, [sessionStatus, refreshReview]);

  // Starting or stopping a session resets the Pomodoro timer server-side
  // (AppState::start_session / stop_session both call pomodoro_.reset()), so
  // refetch whenever the session identity *or status* changes — Stop keeps the
  // same id, and the header's recording line would otherwise stay on "Recording".
  useEffect(() => {
    void refreshPomodoroStatus();
    void refreshAttendedProgress();
    void refreshRecordingStatus();
  }, [sessionId, sessionStatus, refreshPomodoroStatus, refreshAttendedProgress, refreshRecordingStatus]);

  useEffect(() => {
    if (!sessionId || sessionStatus !== "ACTIVE") {
      return;
    }

    const timer = window.setInterval(() => {
      void refreshContextTimeline(sessionId);
    }, TIMELINE_POLL_MS);

    return () => window.clearInterval(timer);
  }, [sessionId, sessionStatus, refreshContextTimeline]);

  // Keep re-checking health until capture is confirmed up, so the app recovers
  // on its own when permissions are granted after launch.
  useEffect(() => {
    if (!shouldPollHealth(captureRunning)) {
      return;
    }

    const timer = window.setInterval(() => {
      void refreshHealth();
    }, HEALTH_POLL_MS);

    return () => window.clearInterval(timer);
  }, [captureRunning, refreshHealth]);

  // When capture comes up, re-check once past the stall grace window so a
  // running-but-silent listener surfaces without a manual refresh.
  useEffect(() => {
    if (!captureRunning) {
      return;
    }
    const timer = window.setTimeout(() => {
      void refreshHealth();
    }, CAPTURE_STALL_RECHECK_MS);

    return () => window.clearTimeout(timer);
  }, [captureRunning, refreshHealth]);

  useEffect(() => {
    const unsubs: Array<Promise<() => void>> = [];
    unsubs.push(
      api.onCaptureFailed((payload) => {
        applyCaptureFailure(payload);
      }),
    );
    unsubs.push(
      api.onOverlayFailed((payload) => {
        applyOverlayFailure(payload);
      }),
    );
    unsubs.push(
      api.onPersistenceFailed((payload) => {
        applyPersistenceFailure(payload);
      }),
    );
    unsubs.push(
      api.onPrediction((record) => {
        handlePrediction(record);
        if (record.sessionId === sessionId && sessionStatus === "ACTIVE") {
          refreshTimelineFromEvent(record.sessionId);
        }
      }),
    );
    unsubs.push(
      api.onSnapback((payload, inApp) => {
        // Roadmap 2.16. The alert is gated; the timeline refresh is not. Quiet hours silence
        // an interruption, they do not stop the app knowing what happened.
        if (inApp) {
          handleSnapback(payload);
        }
        if (sessionStatus === "ACTIVE") {
          refreshTimelineFromEvent(sessionId);
        }
      }),
    );
    unsubs.push(
      api.onHyperfocus((payload, inApp) => {
        if (inApp) {
          handleHyperfocus(payload);
        }
      }),
    );
    unsubs.push(
      api.onUntrackedWork((payload, inApp) => {
        if (inApp) {
          handleUntrackedWork(payload);
        }
      }),
    );
    unsubs.push(
      api.onIdle((payload) => {
        handleIdle(payload);
      }),
    );
    unsubs.push(
      api.onAlertAction((destination) => {
        applyAlertDestination(destination);
      }),
    );
    unsubs.push(
      api.onPomodoro((status) => {
        handlePomodoroEvent(status);
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
    applyOverlayFailure,
    applyPersistenceFailure,
    handleHyperfocus,
    handleUntrackedWork,
    handleIdle,
    applyAlertDestination,
    handlePomodoroEvent,
    handlePrediction,
    handleSnapback,
    refreshTimelineFromEvent,
    sessionId,
    sessionStatus,
    setLabelStatus,
    setLabelStatusWarning,
  ]);
};
