import { useCallback, useEffect, useRef, useState } from "react";

import {
  api,
  type AnalyticsSummary,
  type FocusSummary,
  type SessionSummary,
  type SummaryReport,
} from "./api";
import {
  readStoredReviewRange,
  toReviewWindowRequest,
  writeStoredReviewRange,
  type ReviewRange,
} from "./reviewRange";

const EMPTY_ANALYTICS: AnalyticsSummary = {
  sampleCount: 0,
  avgFocusScore: 0,
  productiveSessionStreak: 0,
  hourly: [],
  topApps: [],
};

const EMPTY_FOCUS: FocusSummary = {
  sampleCount: 0,
  avgFocusScore: 0,
  peakFocusScore: 0,
  distractedSamples: 0,
  distractedFraction: 0,
  longestFocusSecs: 0,
};

const EMPTY_REPORT: SummaryReport = {
  window: "7d",
  generatedAt: "",
  sessionCount: 0,
  completedSessionCount: 0,
  focusSeconds: 0,
  sampleCount: 0,
  avgFocusScore: 0,
  distractedFraction: 0,
  longestFocusSecs: 0,
  topContextApp: "",
};

export const useReviewWorkflow = (
  onSessionDeleted?: (sessionId: string) => void | Promise<void>,
) => {
  const [range, setRangeState] = useState<ReviewRange>(() => readStoredReviewRange());
  const [analytics, setAnalytics] = useState<AnalyticsSummary>(EMPTY_ANALYTICS);
  const [focusSummary, setFocusSummary] = useState<FocusSummary>(EMPTY_FOCUS);
  const [report, setReport] = useState<SummaryReport>(EMPTY_REPORT);
  const [sessionHistory, setSessionHistory] = useState<SessionSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [exportStatus, setExportStatus] = useState<string | null>(null);
  const [deletingSessionId, setDeletingSessionId] = useState<string | null>(null);
  const [deleteStatus, setDeleteStatus] = useState<string | null>(null);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const [reflectionStatus, setReflectionStatus] = useState<string | null>(null);
  const requestIdRef = useRef(0);

  const setRange = useCallback((next: ReviewRange) => {
    writeStoredReviewRange(next);
    setRangeState(next);
  }, []);

  const refreshReview = useCallback(async () => {
    const requestId = ++requestIdRef.current;
    setLoading(true);
    setError(null);
    const params = toReviewWindowRequest(range);
    try {
      const [nextAnalytics, nextReport, nextFocus, nextHistory] = await Promise.all([
        api.getAnalytics(params),
        api.getSummaryReport(params),
        api.getFocusSummary(params),
        api.getSessionHistory(params),
      ]);
      if (requestId !== requestIdRef.current) return;
      setAnalytics(nextAnalytics);
      setReport(nextReport);
      setFocusSummary(nextFocus);
      setSessionHistory(nextHistory);
      setLoading(false);
    } catch {
      if (requestId !== requestIdRef.current) return;
      setError("Could not load Review data for this time range.");
      setLoading(false);
    }
  }, [range]);

  useEffect(() => {
    void refreshReview();
  }, [refreshReview]);

  const exportSummary = useCallback(async () => {
    try {
      const result = await api.exportSummaryReport(toReviewWindowRequest(range));
      setExportStatus(`Exported ${result.window} summary.`);
    } catch {
      setExportStatus("Could not export the summary report.");
    }
  }, [range]);

  const deleteSession = useCallback(
    async (sessionId: string) => {
      if (!sessionId) return;
      setDeletingSessionId(sessionId);
      setDeleteError(null);
      setDeleteStatus(null);
      try {
        const removed = await api.deleteSession(sessionId);
        setSessionHistory((current) =>
          current.filter((summary) => summary.record.sessionId !== sessionId),
        );
        await refreshReview();
        try {
          await onSessionDeleted?.(sessionId);
        } catch {
          // Row is already gone natively.
        }
        setDeleteStatus(removed ? "Session deleted." : "That session was already gone.");
      } catch {
        setDeleteError("Could not delete that session.");
      } finally {
        setDeletingSessionId(null);
      }
    },
    [onSessionDeleted, refreshReview],
  );

  const saveReflection = useCallback(
    async (sessionId: string, done: string | null, nextStep: string | null) => {
      try {
        const saved = await api.saveSessionReflection(sessionId, done, nextStep);
        setSessionHistory((current) =>
          current.map((summary) =>
            summary.record.sessionId === sessionId ? { ...summary, record: saved } : summary,
          ),
        );
        setReflectionStatus("Reflection updated.");
        return true;
      } catch {
        setReflectionStatus("Could not update that reflection.");
        return false;
      }
    },
    [],
  );

  return {
    analytics,
    deleteError,
    deleteSession,
    deleteStatus,
    deletingSessionId,
    error,
    exportStatus,
    exportSummary,
    focusSummary,
    loading,
    range,
    reflectionStatus,
    refreshReview,
    report,
    saveReflection,
    sessionHistory,
    setRange,
  };
};
