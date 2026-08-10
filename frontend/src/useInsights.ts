import { useCallback, useState } from "react";

import { api, type SessionSummary } from "./api";

export const useInsights = (
  onSessionDeleted?: (sessionId: string) => void | Promise<void>,
) => {
  const [sessionHistory, setSessionHistory] = useState<SessionSummary[]>([]);
  const [deletingSessionId, setDeletingSessionId] = useState<string | null>(null);
  const [deleteStatus, setDeleteStatus] = useState<string | null>(null);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const [reflectionStatus, setReflectionStatus] = useState<string | null>(null);

  const refreshInsights = useCallback(async () => {
    try {
      const history = await api.getSessionHistory();
      setSessionHistory(history);
    } catch {
      // Insights are non-critical; leave the last good data in place.
    }
  }, []);

  // Roadmap 7.6. The native command is authoritative: once it returns, the row is gone from
  // SQLite whether or not anything else here succeeds. So the local list is pruned first and
  // unconditionally, before the refetch — `refreshInsights` deliberately swallows its errors
  // and keeps the last good data, which would otherwise leave a deleted session on screen.
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
        await refreshInsights();
        try {
          await onSessionDeleted?.(sessionId);
        } catch {
          // The row is already deleted natively. A failed best-effort refresh of the other
          // surfaces must not tell the user their session still exists (same rule as
          // usePrivacy's deleteAllActivityData).
        }
        // `delete_session` returns whether a row was actually removed, which distinguishes a
        // real delete from a stale list entry. Both end with the row absent, so both prune —
        // but calling the second one "deleted" would credit us with work SQLite didn't do.
        setDeleteStatus(removed ? "Session deleted." : "That session was already gone.");
      } catch {
        setDeleteError("Could not delete that session.");
      } finally {
        setDeletingSessionId(null);
      }
    },
    [onSessionDeleted, refreshInsights],
  );

  const saveReflection = useCallback(async (
    sessionId: string, done: string | null, nextStep: string | null,
  ) => {
    try {
      const saved = await api.saveSessionReflection(sessionId, done, nextStep);
      setSessionHistory((current) => current.map((summary) =>
        summary.record.sessionId === sessionId ? { ...summary, record: saved } : summary));
      setReflectionStatus("Reflection updated.");
      return true;
    } catch {
      setReflectionStatus("Could not update that reflection.");
      return false;
    }
  }, []);

  return {
    deleteError,
    deleteSession,
    deleteStatus,
    deletingSessionId,
    refreshInsights,
    reflectionStatus,
    saveReflection,
    sessionHistory,
  };
};
