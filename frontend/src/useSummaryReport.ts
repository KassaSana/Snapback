import { useCallback, useEffect, useState } from "react";

import { api, type SummaryReport, type SummaryWindow } from "./api";

const EMPTY_REPORT: SummaryReport = {
  window: "day",
  generatedAt: "",
  sessionCount: 0,
  focusSeconds: 0,
  sampleCount: 0,
  avgFocusScore: 0,
  distractedFraction: 0,
  longestFocusStreak: 0,
  topContextApp: "",
};

export const useSummaryReport = () => {
  const [window, setWindow] = useState<SummaryWindow>("day");
  const [report, setReport] = useState<SummaryReport>(EMPTY_REPORT);
  const [status, setStatus] = useState<string | null>(null);

  const refreshSummary = useCallback(async () => {
    try {
      setReport(await api.getSummaryReport(window));
      setStatus(null);
    } catch {
      setStatus("Could not load the summary report.");
    }
  }, [window]);

  useEffect(() => {
    void refreshSummary();
  }, [refreshSummary]);

  const exportSummary = useCallback(async () => {
    try {
      const result = await api.exportSummaryReport(window);
      setStatus(`Exported ${result.window} summary.`);
    } catch {
      setStatus("Could not export the summary report.");
    }
  }, [window]);

  return { exportSummary, report, refreshSummary, setWindow, status, window };
};
