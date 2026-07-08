import { useEffect } from "react";

import {
  api,
  type CaptureFailurePayload,
  type LabelHotkeyPayload,
  type PredictionRecord,
  type SnapbackPayload,
} from "./api";
import { TIMELINE_POLL_MS } from "./useLiveData";

type UseAppEffectsArgs = {
  refreshHealth: () => void | Promise<void>;
  refreshLatest: () => void | Promise<void>;
  refreshAppRules: () => void | Promise<void>;
  refreshDeployStatus: () => void | Promise<void>;
  hydrateActiveSession: () => void | Promise<void>;

  sessionId: string | null;
  sessionStatus: string | null;

  refreshContextTimeline: (sid?: string | null) => void | Promise<void>;

  applyCaptureFailure: (payload: CaptureFailurePayload) => void;

  handlePrediction: (record: PredictionRecord | null) => void;
  handleSnapback: (payload: SnapbackPayload) => void;
  handleHyperfocus: (payload: { message: string }) => void;
  refreshTimelineFromEvent: (sid?: string | null) => void;

  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
};

export const useAppEffects = ({
  refreshHealth,
  refreshLatest,
  refreshAppRules,
  refreshDeployStatus,
  hydrateActiveSession,
  sessionId,
  sessionStatus,
  refreshContextTimeline,
  applyCaptureFailure,
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
    void hydrateActiveSession();
  }, [hydrateActiveSession, refreshHealth, refreshLatest, refreshAppRules, refreshDeployStatus]);

  useEffect(() => {
    if (!sessionId || sessionStatus !== "ACTIVE") {
      return;
    }

    const timer = window.setInterval(() => {
      void refreshContextTimeline(sessionId);
    }, TIMELINE_POLL_MS);

    return () => window.clearInterval(timer);
  }, [sessionId, sessionStatus, refreshContextTimeline]);

  useEffect(() => {
    const unsubs: Array<Promise<() => void>> = [];
    unsubs.push(
      api.onCaptureFailed((payload) => {
        applyCaptureFailure(payload);
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
