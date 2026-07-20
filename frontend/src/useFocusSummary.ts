import { useCallback, useState } from "react";

import { api, type FocusSummary } from "./api";

const EMPTY_FOCUS_SUMMARY: FocusSummary = {
  sampleCount: 0,
  avgFocusScore: 0,
  peakFocusScore: 0,
  distractedSamples: 0,
  distractedFraction: 0,
  longestFocusStreak: 0,
};

export const useFocusSummary = () => {
  const [focusSummary, setFocusSummary] = useState<FocusSummary>(EMPTY_FOCUS_SUMMARY);

  const refreshFocusSummary = useCallback(async () => {
    try {
      const summary = await api.getFocusSummary();
      setFocusSummary(summary);
    } catch {
      // Non-critical; leave the last good summary in place.
    }
  }, []);

  return { focusSummary, refreshFocusSummary };
};
