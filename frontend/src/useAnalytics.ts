import { useCallback, useEffect, useState } from "react";

import { api, type AnalyticsSummary } from "./api";

const EMPTY_ANALYTICS: AnalyticsSummary = {
  sampleCount: 0,
  avgFocusScore: 0,
  productiveSessionStreak: 0,
  hourly: [],
  topApps: [],
};

export const useAnalytics = () => {
  const [analytics, setAnalytics] = useState<AnalyticsSummary>(EMPTY_ANALYTICS);

  const refreshAnalytics = useCallback(async () => {
    try {
      setAnalytics(await api.getAnalytics());
    } catch {
      // Analytics are non-critical; retain the last good result.
    }
  }, []);

  useEffect(() => {
    void refreshAnalytics();
  }, [refreshAnalytics]);

  return { analytics, refreshAnalytics };
};
