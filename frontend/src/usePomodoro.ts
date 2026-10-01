import { useCallback, useEffect, useRef, useState } from "react";

import { api, type PomodoroConfig, type PomodoroStatus } from "./api";

const EMPTY_POMODORO_STATUS: PomodoroStatus = {
  running: false,
  paused: false,
  awaitingAcknowledgement: false,
  phase: "work",
  completedWorkIntervals: 0,
  remainingMs: 0,
};

// The native snapshot remains authoritative for phase changes. Only interpolate its
// remaining time locally, with a monotonic clock so delayed renders do not slow the timer.
export function usePomodoroCountdown(status: PomodoroStatus): PomodoroStatus {
  const [display, setDisplay] = useState({
    remainingMs: status.remainingMs,
    phase: status.phase,
    completedWorkIntervals: status.completedWorkIntervals,
    paused: status.paused,
    awaitingAcknowledgement: status.awaitingAcknowledgement,
    running: status.running,
  });
  const anchor = useRef({ remainingMs: status.remainingMs, at: 0 });

  useEffect(() => {
    anchor.current = { remainingMs: status.remainingMs, at: performance.now() };
    setDisplay({
      remainingMs: status.remainingMs,
      phase: status.phase,
      completedWorkIntervals: status.completedWorkIntervals,
      paused: status.paused,
      awaitingAcknowledgement: status.awaitingAcknowledgement,
      running: status.running,
    });
    if (!status.running || status.paused || status.awaitingAcknowledgement || status.remainingMs <= 0) {
      return;
    }
    const timer = window.setInterval(() => {
      const remainingMs = Math.max(
        0,
        Math.round(anchor.current.remainingMs - (performance.now() - anchor.current.at)),
      );
      setDisplay((prev) => ({ ...prev, remainingMs }));
      if (remainingMs === 0) window.clearInterval(timer);
    }, 1000);
    return () => window.clearInterval(timer);
  }, [
    status.running,
    status.paused,
    status.awaitingAcknowledgement,
    status.remainingMs,
    status.phase,
    status.completedWorkIntervals,
  ]);

  // Prefer the live countdown only while it still describes this snapshot's phase.
  const synced =
    display.phase === status.phase &&
    display.completedWorkIntervals === status.completedWorkIntervals &&
    display.paused === status.paused &&
    display.awaitingAcknowledgement === status.awaitingAcknowledgement &&
    display.running === status.running;
  return { ...status, remainingMs: synced ? display.remainingMs : status.remainingMs };
}

type UsePomodoroArgs = {
  setActionError: (value: string | null) => void;
};

export const usePomodoro = ({ setActionError }: UsePomodoroArgs) => {
  const [pomodoroStatus, setPomodoroStatus] = useState<PomodoroStatus>(EMPTY_POMODORO_STATUS);
  const statusRevision = useRef(0);
  const displayedStatus = usePomodoroCountdown(pomodoroStatus);
  const [pomodoroConfig, setPomodoroConfig] = useState<PomodoroConfig>({
    workMs: 25 * 60 * 1000,
    shortBreakMs: 5 * 60 * 1000,
    longBreakMs: 15 * 60 * 1000,
    intervalsBeforeLongBreak: 4,
    autoStartNextPhase: true,
  });

  const refreshPomodoroStatus = useCallback(async () => {
    const revision = ++statusRevision.current;
    try {
      const status = await api.getPomodoroStatus();
      if (revision === statusRevision.current) setPomodoroStatus(status);
    } catch {
      // Non-critical; leave the last good status in place.
    }
    try {
      const settings = await api.getSettings();
      if (revision === statusRevision.current) setPomodoroConfig(settings.pomodoro);
    } catch {
      // The timer status is useful even when settings cannot be read.
    }
  }, []);

  const handleSavePomodoroConfig = useCallback(async (config: PomodoroConfig) => {
    const revision = ++statusRevision.current;
    try {
      const status = await api.setPomodoroConfig(config);
      if (revision !== statusRevision.current) return;
      setPomodoroConfig(config);
      setPomodoroStatus(status);
      setActionError(null);
    } catch {
      setActionError("Could not save the Pomodoro rhythm.");
    }
  }, [setActionError]);

  const handlePomodoroEvent = useCallback((status: PomodoroStatus) => {
    ++statusRevision.current;
    setPomodoroStatus(status);
  }, []);

  const handleStartPomodoro = useCallback(async () => {
    const revision = ++statusRevision.current;
    try {
      const status = await api.startPomodoro();
      if (revision !== statusRevision.current) return;
      setPomodoroStatus(status);
      setActionError(null);
    } catch {
      setActionError("Could not start the Pomodoro timer. Start a session first.");
    }
  }, [setActionError]);

  const handleStopPomodoro = useCallback(async () => {
    const revision = ++statusRevision.current;
    try {
      const status = await api.stopPomodoro();
      if (revision !== statusRevision.current) return;
      setPomodoroStatus(status);
    } catch {
      setActionError("Could not stop the Pomodoro timer.");
    }
  }, [setActionError]);

  // Call, take the status the backend reached, report failures in the user's terms.
  const runPomodoroAction = useCallback(
    async (action: () => Promise<PomodoroStatus>, failure: string) => {
      const revision = ++statusRevision.current;
      try {
        const status = await action();
        if (revision !== statusRevision.current) return;
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
    pomodoroStatus: displayedStatus,
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
