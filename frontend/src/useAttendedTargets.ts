import { useCallback, useState } from "react";

import { api, type AttendedProgress } from "./api";

// Roadmap 2.19. Off until someone opts in: zero targets and zero attendance is what a fresh
// install shows, and is also what a failed load leaves in place rather than an error banner
// over a card nobody asked for.
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
