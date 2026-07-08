import { useCallback, useMemo, useState } from "react";

import {
  api,
  focusStateLabel,
  type FocusLabel,
  type LabelSource,
  type SessionRecap,
  type SessionRecord,
} from "./api";

export const FOCUS_MODES = ["deep", "normal", "recovery"] as const;
export type FocusMode = (typeof FOCUS_MODES)[number];

type UseSessionArgs = {
  refreshContextTimeline: (sid?: string | null) => void | Promise<void>;
  resetTimelineRefreshGate: () => void;
  setActionError: (value: string | null) => void;
  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
};

export const useSession = ({
  refreshContextTimeline,
  resetTimelineRefreshGate,
  setActionError,
  setLabelStatus,
  setLabelStatusWarning,
}: UseSessionArgs) => {
  const [sessionGoal, setSessionGoal] = useState("");
  const [sessionRecord, setSessionRecord] = useState<SessionRecord | null>(null);
  const [sessionId, setSessionId] = useState<string | null>(null);
  const [focusMode, setFocusMode] = useState<FocusMode>("normal");
  const [recap, setRecap] = useState<SessionRecap | null>(null);
  const [surveyPending, setSurveyPending] = useState(false);

  const hydrateActiveSession = useCallback(async () => {
    const active = await api.getActiveSession();
    if (!active) {
      return;
    }

    setSessionRecord(active);
    setSessionId(active.sessionId);
    setSessionGoal(active.goal);
    setFocusMode((active.focusMode as FocusMode) || "normal");
    void refreshContextTimeline(active.sessionId);
  }, [refreshContextTimeline]);

  const handleLabel = useCallback(
    async (label: FocusLabel, source: LabelSource = "manual") => {
      if (!sessionId) {
        setLabelStatus("Start a session to save feedback.");
        return;
      }

      try {
        await api.submitLabel(sessionId, label, undefined, source);
        const prefix =
          source === "hotkey"
            ? "Hotkey saved"
            : source === "survey"
              ? "Session rating saved"
              : "Saved";
        setLabelStatus(`${prefix}: ${focusStateLabel(label)}`);
        setLabelStatusWarning(false);
        if (source === "survey") {
          setSurveyPending(false);
        }
      } catch {
        setLabelStatus("Could not save feedback.");
        setLabelStatusWarning(true);
      }
    },
    [sessionId, setLabelStatus, setLabelStatusWarning],
  );

  const handleStartSession = useCallback(async () => {
    const goal = sessionGoal.trim();
    if (!goal) {
      return;
    }

    try {
      const record = await api.startSession(goal, focusMode);
      setSessionRecord(record);
      setSessionId(record.sessionId);
      setSessionGoal(record.goal);
      setRecap(null);
      setSurveyPending(false);
      setActionError(null);
      resetTimelineRefreshGate();
      void refreshContextTimeline(record.sessionId);
    } catch {
      setActionError("Could not start session. Check capture permissions and try again.");
    }
  }, [
    focusMode,
    refreshContextTimeline,
    resetTimelineRefreshGate,
    sessionGoal,
    setActionError,
  ]);

  const handleStopSession = useCallback(async () => {
    if (!sessionId) {
      return;
    }

    try {
      const record = await api.stopSession(sessionId);
      setSessionRecord(record);
      const sessionRecap = await api.getSessionRecap(sessionId);
      setRecap(sessionRecap);
      setSurveyPending(true);
      setLabelStatus("Automatic session label saved. How did this session feel overall?");
      setLabelStatusWarning(false);
      setActionError(null);
      resetTimelineRefreshGate();
      void refreshContextTimeline(sessionId);
    } catch {
      setActionError("Could not stop session or load recap.");
    }
  }, [
    refreshContextTimeline,
    resetTimelineRefreshGate,
    sessionId,
    setActionError,
    setLabelStatus,
    setLabelStatusWarning,
  ]);

  const handleFocusModeChange = useCallback(async (mode: FocusMode) => {
    setFocusMode(mode);
    try {
      await api.setFocusMode(mode);
    } catch {
      // Keep local selection even if backend update fails.
    }
  }, []);

  const handleSkipSurvey = useCallback(() => {
    setSurveyPending(false);
    setLabelStatus("Kept automatic session label.");
    setLabelStatusWarning(false);
  }, [setLabelStatus, setLabelStatusWarning]);

  const sessionStatusLabel = useMemo(
    () => (sessionRecord ? sessionRecord.status.toLowerCase() : "idle"),
    [sessionRecord],
  );

  return {
    focusMode,
    handleFocusModeChange,
    handleLabel,
    handleSkipSurvey,
    handleStartSession,
    handleStopSession,
    hydrateActiveSession,
    recap,
    sessionGoal,
    sessionId,
    sessionRecord,
    sessionStatusLabel,
    setSessionGoal,
    surveyPending,
  };
};
