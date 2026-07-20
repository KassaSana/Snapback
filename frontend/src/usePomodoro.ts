import { useCallback, useState } from "react";

import { api, type PomodoroStatus } from "./api";

const EMPTY_POMODORO_STATUS: PomodoroStatus = {
  running: false,
  phase: "work",
  completedWorkIntervals: 0,
  remainingMs: 0,
};

type UsePomodoroArgs = {
  setActionError: (value: string | null) => void;
};

export const usePomodoro = ({ setActionError }: UsePomodoroArgs) => {
  const [pomodoroStatus, setPomodoroStatus] = useState<PomodoroStatus>(EMPTY_POMODORO_STATUS);

  const refreshPomodoroStatus = useCallback(async () => {
    try {
      const status = await api.getPomodoroStatus();
      setPomodoroStatus(status);
    } catch {
      // Non-critical; leave the last good status in place.
    }
  }, []);

  const handlePomodoroEvent = useCallback((status: PomodoroStatus) => {
    setPomodoroStatus(status);
  }, []);

  const handleStartPomodoro = useCallback(async () => {
    try {
      const status = await api.startPomodoro();
      setPomodoroStatus(status);
      setActionError(null);
    } catch {
      setActionError("Could not start the Pomodoro timer. Start a session first.");
    }
  }, [setActionError]);

  const handleStopPomodoro = useCallback(async () => {
    try {
      const status = await api.stopPomodoro();
      setPomodoroStatus(status);
    } catch {
      setActionError("Could not stop the Pomodoro timer.");
    }
  }, [setActionError]);

  return {
    pomodoroStatus,
    refreshPomodoroStatus,
    handlePomodoroEvent,
    handleStartPomodoro,
    handleStopPomodoro,
  };
};
