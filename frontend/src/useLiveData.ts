import { useCallback, useMemo, useRef, useState } from "react";

import {
  api,
  buildSignals,
  riskLabel,
  riskLevel,
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

  const handleHyperfocus = useCallback((payload: { message: string }) => {
    setHyperfocusNote(payload.message);
  }, []);

  const signals = useMemo(() => buildSignals(prediction), [prediction]);
  const riskValue = prediction?.distractionRisk ?? null;
  const riskBadgeLabel = prediction ? riskLabel(riskValue) : "No data";
  const riskClass = riskLevel(riskValue);

  return {
    contextTimeline,
    handleHyperfocus,
    handlePrediction,
    handleSnapback,
    hyperfocusNote,
    prediction,
    predictionHistory,
    pushPrediction,
    refreshContextTimeline,
    refreshLatest,
    refreshTimelineFromEvent,
    resetTimelineRefreshGate,
    riskBadgeLabel,
    riskClass,
    signals,
    snapbackNote,
  };
};
