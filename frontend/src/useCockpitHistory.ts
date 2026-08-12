import { useCallback, useEffect, useState } from "react";

import { api, type SessionSummary } from "./api";

/** Recent sessions for the Now-surface cockpit. Intentionally unfiltered by Review range. */
export const useCockpitHistory = () => {
  const [sessionHistory, setSessionHistory] = useState<SessionSummary[]>([]);

  const refreshCockpitHistory = useCallback(async () => {
    try {
      setSessionHistory(await api.getSessionHistory({ limit: 20 }));
    } catch {
      // Non-critical; keep the last good list.
    }
  }, []);

  useEffect(() => {
    void refreshCockpitHistory();
  }, [refreshCockpitHistory]);

  return { refreshCockpitHistory, sessionHistory };
};
