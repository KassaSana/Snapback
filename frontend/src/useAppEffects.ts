import { useEffect } from "react";

import {
  api,
  type CaptureFailurePayload,
  type LabelHotkeyPayload,
  type OverlayFailurePayload,
  type PredictionRecord,
  type SnapbackPayload,
} from "./api";
import { CAPTURE_STALL_RECHECK_MS, HEALTH_POLL_MS, shouldPollHealth } from "./healthPoll";
import { TIMELINE_POLL_MS } from "./useLiveData";

type UseAppEffectsArgs = {
  refreshHealth: () => void | Promise<void>;
  captureRunning: boolean;
  refreshInsights: () => void | Promise<void>;
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
  refreshTimelineFromEvent: (sid?: string | null) => void;

  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
};

export const useAppEffects = ({
  refreshHealth,
  captureRunning,
  refreshInsights,
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
  refreshTimelineFromEvent,
  setLabelStatus,
  setLabelStatusWarning,
}: UseAppEffectsArgs) => {
  useEffect(() => {
    void refreshHealth();
    void refreshLatest();
    void refreshAppRules();
    void refreshDeployStatus();
    void refreshInsights();
    void hydrateActiveSession();
  }, [
    hydrateActiveSession,
    refreshHealth,
    refreshLatest,
    refreshAppRules,
    refreshDeployStatus,
    refreshInsights,
  ]);

  // A completed session adds a new row to history — refresh insights so the
  // chart and tiles pick it up without a manual reload.
  useEffect(() => {
    if (sessionStatus === "COMPLETED") {
      void refreshInsights();
    }
  }, [sessionStatus, refreshInsights]);

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
      api.onSnapback((payload) => {
        handleSnapback(payload);
        if (sessionStatus === "ACTIVE") {
          refreshTimelineFromEvent(sessionId);
        }
      }),
    );
    unsubs.push(
      api.onHyperfocus((payload) => {
        handleHyperfocus(payload);
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
    handlePrediction,
    handleSnapback,
    refreshTimelineFromEvent,
    sessionId,
    sessionStatus,
    setLabelStatus,
    setLabelStatusWarning,
  ]);
};
