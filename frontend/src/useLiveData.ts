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
  // Request generations, the pattern useReviewWorkflow already uses. A timeline or latest
  // read that resolves after a session switch or an activity clear used to land on top of
  // the newer state and restore rows from a session that is over (or was just deleted).
  // Every read takes a ticket; a result is applied only if its ticket is still current, and
  // anything that resets the state also retires every ticket in flight.
  const timelineRequestRef = useRef(0);
  const latestRequestRef = useRef(0);

  const refreshContextTimeline = useCallback(async (sid?: string | null) => {
    const requestId = ++timelineRequestRef.current;
    if (!sid) {
      setContextTimeline([]);
      return;
    }

    lastTimelineRefreshAtRef.current = Date.now();
    try {
      const rows = await api.getContextTimeline(sid, TIMELINE_LIMIT);
      if (requestId !== timelineRequestRef.current) return;
      setContextTimeline(rows);
    } catch {
      // A failed read is not an empty timeline. Leave what is on screen rather than replace
      // it with "nothing happened"; the health card is where the failure is reported.
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
        last.timestampMs === record.timestampMs &&
        last.focusScore === record.focusScore &&
        last.distractionRisk === record.distractionRisk;
      const next = isDuplicate ? current : [record, ...current];
      return next.slice(0, HISTORY_LIMIT);
    });
  }, []);

  const refreshLatest = useCallback(async () => {
    const requestId = ++latestRequestRef.current;
    try {
      const latest = await api.getLatestPrediction();
      if (requestId !== latestRequestRef.current) return;
      pushPrediction(latest);
      const history = await api.getPredictionHistory(HISTORY_LIMIT);
      if (requestId !== latestRequestRef.current) return;
      if (history.length > 0) {
        setPredictionHistory(history);
      }
    } catch {
      // Same rule as the timeline: a read that failed says nothing about the prediction, so
      // it must not be shown as "no prediction".
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
    (record: PredictionRecord | null, activeSessionId?: string | null) => {
      // Session start/stop bumps the native activity epoch so most stale dispatches die
      // before they reach here; this is the belt for anything that still arrives — a
      // prediction named for a previous session must not paint under the new one.
      if (
        record &&
        activeSessionId &&
        record.sessionId &&
        record.sessionId !== activeSessionId
      ) {
        return;
      }
      pushPrediction(record);
    },
    [pushPrediction],
  );

  // The summary is kept beside the note so a failed restore can rewrite the note around it
  // without stacking one failure reason on top of the last.
  const snapbackSummaryRef = useRef<string | null>(null);

  const clearSessionLiveSignals = useCallback(() => {
    ++latestRequestRef.current;
    setPrediction(null);
    setSnapbackNote(null);
    snapbackSummaryRef.current = null;
  }, []);

  const handleSnapback = useCallback((payload: { summary: string }) => {
    snapbackSummaryRef.current = payload.summary;
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

  const handleRestoreSnapbackTarget = useCallback(async () => {
    // Unlike dismiss, this note only clears once the native side says the window came back.
    // It used to clear first and ignore the result, so a failed activation -- the window
    // closed, a title that no longer matches, a platform without support -- looked exactly
    // like a successful one, and the retry button vanished with it. The native side now keeps
    // its target across a failure, so leaving the note (and its "Take me back") in place is a
    // real retry rather than a decoration.
    const summary = snapbackSummaryRef.current;
    const failed = (reason: string) => {
      setSnapbackNote(
        `Snapback: ${summary ?? ""} — couldn't bring it back (${reason}). Try again or dismiss.`,
      );
    };
    try {
      const result = await api.restoreSnapbackTarget();
      if (result.ok) {
        setSnapbackNote(null);
      } else {
        failed(result.message);
      }
    } catch (error) {
      failed(error instanceof Error ? error.message : String(error));
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
  const clearUntrackedNote = useCallback(async () => {
    try {
      await api.dismissUntrackedNudge(60);
      setUntrackedNote(null);
    } catch {
      // Keep the notice visible when persistence failed; hiding it would imply a dismissal
      // that the next launch cannot remember.
    }
  }, []);

  const clearActivityData = useCallback(() => {
    // Retire every read in flight first: a timeline fetched a moment before the user cleared
    // their activity must not resolve into the empty state and bring the rows back.
    ++timelineRequestRef.current;
    ++latestRequestRef.current;
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
    clearSessionLiveSignals,
    handleDismissSnapback,
    handleRestoreSnapbackTarget,
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
