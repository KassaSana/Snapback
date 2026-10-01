import { useCallback, useEffect, useState } from "react";

import { api, type AttendedProgress } from "./api";
import { ATTENDED_REBASELINE_MS, liveAttendedMins } from "./liveAttended";

// Off until opted in; a failed load leaves this in place rather than an error banner.
const EMPTY_PROGRESS: AttendedProgress = {
  dailyTargetMins: 0,
  dailyActualMins: 0,
  weeklyTargetMins: 0,
  weeklyActualMins: 0,
};

type UseAttendedTargetsArgs = {
  setActionError: (value: string | null) => void;
  /**
   * True while a live session is counting attended time (not idle / private pause).
   * Drives the local tick between authoritative `get_attended_progress` baselines.
   */
  accruing: boolean;
};

export const useAttendedTargets = ({ setActionError, accruing }: UseAttendedTargetsArgs) => {
  const [baseline, setBaseline] = useState<AttendedProgress>(EMPTY_PROGRESS);
  const [baselineAtMs, setBaselineAtMs] = useState(() => Date.now());
  const [nowMs, setNowMs] = useState(() => Date.now());

  const applyProgress = useCallback((next: AttendedProgress) => {
    setBaseline(next);
    setBaselineAtMs(Date.now());
    setNowMs(Date.now());
  }, []);

  const refreshAttendedProgress = useCallback(async () => {
    try {
      applyProgress(await api.getAttendedProgress());
    } catch {
      // Non-critical: leave the last good numbers rather than blanking a card mid-session.
    }
  }, [applyProgress]);

  const handleSaveAttendedTargets = useCallback(
    async (dailyMins: number, weeklyMins: number) => {
      try {
        // The backend returns the progress recomputed against the new plan, so the card never
        // renders a target the app did not accept.
        applyProgress(await api.setAttendedTargets(dailyMins, weeklyMins));
        setActionError(null);
      } catch {
        setActionError("Could not save the attended-time targets.");
      }
    },
    [applyProgress, setActionError],
  );

  // 1 Hz local tick while accruing — display only; IPC rebaselines on a slower cadence.
  useEffect(() => {
    if (!accruing) return;
    setNowMs(Date.now());
    const timer = window.setInterval(() => setNowMs(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, [accruing]);

  // Periodic authoritative rebaseline while a session is accruing.
  useEffect(() => {
    if (!accruing) return;
    const timer = window.setInterval(() => {
      void refreshAttendedProgress();
    }, ATTENDED_REBASELINE_MS);
    return () => window.clearInterval(timer);
  }, [accruing, refreshAttendedProgress]);

  const attendedProgress: AttendedProgress = {
    dailyTargetMins: baseline.dailyTargetMins,
    weeklyTargetMins: baseline.weeklyTargetMins,
    dailyActualMins: liveAttendedMins(baseline.dailyActualMins, baselineAtMs, nowMs, accruing),
    weeklyActualMins: liveAttendedMins(baseline.weeklyActualMins, baselineAtMs, nowMs, accruing),
  };

  return {
    attendedProgress,
    attendedAccruing: accruing,
    refreshAttendedProgress,
    handleSaveAttendedTargets,
  };
};
