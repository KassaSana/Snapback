import { useCallback, useMemo, useRef, useState } from "react";

import {
  api,
  buildSignals,
  riskLabel,
  verdictLevel,
  type ContextSnapshot,
  type PredictionRecord,
} from "./api";
import { shouldRefreshTimelineFromEvent } from "./timelineRefresh";

export const HISTORY_LIMIT = 8;
export const TIMELINE_LIMIT = 20;
export const TIMELINE_POLL_MS = 30_000;

export const useLiveData = () => {
  const [prediction, setPrediction] = useState<PredictionRecord | null>(null);
  const [predictionHistory, setPredictionHistory] = useState<PredictionRecord[]>([]);
  const [hyperfocusNote, setHyperfocusNote] = useState<string | null>(null);
  const [untrackedNote, setUntrackedNote] = useState<string | null>(null);
  const [userIdle, setUserIdle] = useState(false);
  const [snapbackNote, setSnapbackNote] = useState<string | null>(null);
  const [contextTimeline, setContextTimeline] = useState<ContextSnapshot[]>([]);
  const lastTimelineRefreshAtRef = useRef<number | null>(null);

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

  const refreshTimelineFromEvent = useCallback((sid?: string | null) => {
    if (!sid) {
      return;
    }

    const now = Date.now();
    if (!shouldRefreshTimelineFromEvent(lastTimelineRefreshAtRef.current, now)) {
      return;
    }

    lastTimelineRefreshAtRef.current = now;
    void refreshContextTimeline(sid);
  }, [refreshContextTimeline]);

  const handlePrediction = useCallback(
    (record: PredictionRecord | null) => {
      pushPrediction(record);
    },
    [pushPrediction],
  );

  const handleSnapback = useCallback((payload: { summary: string }) => {
    setSnapbackNote(`Snapback: ${payload.summary}`);
  }, []);

  const handleDismissSnapback = useCallback(async () => {
    // Clearing the note is what the user asked for regardless of the backend call's
    // outcome; the command's real job is unsticking ContextTracker's Recovering state
    // (its only exit), not the note itself, so a failure here shouldn't trap the UI.
    setSnapbackNote(null);
    try {
      await api.dismissSnapback();
    } catch {
      // Best-effort; nothing actionable to show the user for this.
    }
  }, []);

  const handleHyperfocus = useCallback((payload: { message: string }) => {
    setHyperfocusNote(payload.message);
  }, []);

  const handleUntrackedWork = useCallback((payload: { message: string }) => {
    setUntrackedNote(payload.message);
  }, []);

  const handleIdle = useCallback((payload: { idle: boolean }) => {
    setUserIdle(payload.idle);
  }, []);

  // Starting a session is the answer to the nudge, so it clears immediately rather than
  // waiting for the next tick to notice a session now exists.
  const clearUntrackedNote = useCallback(() => setUntrackedNote(null), []);

  const clearActivityData = useCallback(() => {
    setPrediction(null);
    setPredictionHistory([]);
    setHyperfocusNote(null);
    setUntrackedNote(null);
    setSnapbackNote(null);
    setContextTimeline([]);
    lastTimelineRefreshAtRef.current = null;
  }, []);

  const signals = useMemo(() => buildSignals(prediction), [prediction]);
  const riskValue = prediction?.distractionRisk ?? null;
  const riskBadgeLabel = prediction ? riskLabel(riskValue) : "No data";
  // The hero colours by the verdict, not the risk — the two channels can disagree
  // (ADR-0004) and the risk badge already speaks for the opinion side.
  const verdictClass = verdictLevel(prediction?.focusState ?? null);

  return {
    contextTimeline,
    clearActivityData,
    handleDismissSnapback,
    handleHyperfocus,
    handlePrediction,
    handleSnapback,
    hyperfocusNote,
    untrackedNote,
    userIdle,
    handleIdle,
    handleUntrackedWork,
    clearUntrackedNote,
    prediction,
    predictionHistory,
    pushPrediction,
    refreshContextTimeline,
    refreshLatest,
    refreshTimelineFromEvent,
    resetTimelineRefreshGate,
    riskBadgeLabel,
    signals,
    snapbackNote,
    verdictClass,
  };
};
