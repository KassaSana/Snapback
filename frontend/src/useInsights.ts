import { useCallback, useState } from "react";

import { api, type SessionSummary } from "./api";

export const useInsights = () => {
  const [sessionHistory, setSessionHistory] = useState<SessionSummary[]>([]);

  const refreshInsights = useCallback(async () => {
    try {
      const history = await api.getSessionHistory();
      setSessionHistory(history);
    } catch {
      // Insights are non-critical; leave the last good data in place.
    }
  }, []);

  return { sessionHistory, refreshInsights };
};
