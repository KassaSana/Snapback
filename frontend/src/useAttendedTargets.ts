import { useCallback, useState } from "react";

import { api, type AttendedProgress } from "./api";

// Off until opted in; a failed load leaves this in place rather than an error banner.
const EMPTY_PROGRESS: AttendedProgress = {
  dailyTargetMins: 0,
  dailyActualMins: 0,
  weeklyTargetMins: 0,
  weeklyActualMins: 0,
};

type UseAttendedTargetsArgs = {
  setActionError: (value: string | null) => void;
};

export const useAttendedTargets = ({ setActionError }: UseAttendedTargetsArgs) => {
  const [attendedProgress, setAttendedProgress] = useState<AttendedProgress>(EMPTY_PROGRESS);

  const refreshAttendedProgress = useCallback(async () => {
    try {
      setAttendedProgress(await api.getAttendedProgress());
    } catch {
      // Non-critical: leave the last good numbers rather than blanking a card mid-session.
    }
  }, []);

  const handleSaveAttendedTargets = useCallback(
    async (dailyMins: number, weeklyMins: number) => {
      try {
        // The backend returns the progress recomputed against the new plan, so the card never
        // renders a target the app did not accept.
        setAttendedProgress(await api.setAttendedTargets(dailyMins, weeklyMins));
        setActionError(null);
      } catch {
        setActionError("Could not save the attended-time targets.");
      }
    },
    [setActionError],
  );

  return { attendedProgress, refreshAttendedProgress, handleSaveAttendedTargets };
};
