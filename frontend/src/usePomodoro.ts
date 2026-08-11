import { useCallback, useState } from "react";

import { api, type PomodoroConfig, type PomodoroStatus } from "./api";

const EMPTY_POMODORO_STATUS: PomodoroStatus = {
  running: false,
  paused: false,
  awaitingAcknowledgement: false,
  phase: "work",
  completedWorkIntervals: 0,
  remainingMs: 0,
};

type UsePomodoroArgs = {
  setActionError: (value: string | null) => void;
};

export const usePomodoro = ({ setActionError }: UsePomodoroArgs) => {
  const [pomodoroStatus, setPomodoroStatus] = useState<PomodoroStatus>(EMPTY_POMODORO_STATUS);
  const [pomodoroConfig, setPomodoroConfig] = useState<PomodoroConfig>({
    workMs: 25 * 60 * 1000,
    shortBreakMs: 5 * 60 * 1000,
    longBreakMs: 15 * 60 * 1000,
    intervalsBeforeLongBreak: 4,
    autoStartNextPhase: true,
  });

  const refreshPomodoroStatus = useCallback(async () => {
    try {
      const status = await api.getPomodoroStatus();
      setPomodoroStatus(status);
    } catch {
      // Non-critical; leave the last good status in place.
    }
    try {
      const settings = await api.getSettings();
      setPomodoroConfig(settings.pomodoro);
    } catch {
      // The timer status is useful even when settings cannot be read.
    }
  }, []);

  const handleSavePomodoroConfig = useCallback(async (config: PomodoroConfig) => {
    try {
      const status = await api.setPomodoroConfig(config);
      setPomodoroConfig(config);
      setPomodoroStatus(status);
      setActionError(null);
    } catch {
      setActionError("Could not save the Pomodoro rhythm.");
    }
  }, [setActionError]);

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

  // Roadmap 2.13. The five controls share one shape: call, take the status the backend
  // actually reached, and report a failure in the user's terms. The backend treats a control
  // that does not apply as a no-op rather than an error, so there is nothing to guard here.
  const runPomodoroAction = useCallback(
    async (action: () => Promise<PomodoroStatus>, failure: string) => {
      try {
        const status = await action();
        setPomodoroStatus(status);
        setActionError(null);
      } catch {
        setActionError(failure);
      }
    },
    [setActionError],
  );

  const handlePausePomodoro = useCallback(
    () => runPomodoroAction(api.pausePomodoro, "Could not pause the Pomodoro timer."),
    [runPomodoroAction],
  );
  const handleResumePomodoro = useCallback(
    () => runPomodoroAction(api.resumePomodoro, "Could not resume the Pomodoro timer."),
    [runPomodoroAction],
  );
  const handleSkipPomodoroPhase = useCallback(
    () => runPomodoroAction(api.skipPomodoroPhase, "Could not skip this phase."),
    [runPomodoroAction],
  );
  const handleRestartPomodoroPhase = useCallback(
    () => runPomodoroAction(api.restartPomodoroPhase, "Could not restart this phase."),
    [runPomodoroAction],
  );
  const handleAcknowledgePomodoroPhase = useCallback(
    () =>
      runPomodoroAction(api.acknowledgePomodoroPhase, "Could not start the next phase."),
    [runPomodoroAction],
  );

  return {
    pomodoroStatus,
    pomodoroConfig,
    refreshPomodoroStatus,
    handlePomodoroEvent,
    handleStartPomodoro,
    handleStopPomodoro,
    handlePausePomodoro,
    handleResumePomodoro,
    handleSkipPomodoroPhase,
    handleRestartPomodoroPhase,
    handleAcknowledgePomodoroPhase,
    handleSavePomodoroConfig,
  };
};
