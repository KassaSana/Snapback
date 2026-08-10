import { useCallback, useMemo, useState } from "react";

import {
  api,
  focusStateLabel,
  type FocusLabel,
  type LabelSource,
  type SessionRecap,
  type SessionRecord,
} from "./api";
import { sessionStartCaptureWarning, type SessionCaptureReadiness } from "./healthHints";

export const FOCUS_MODES = ["deep", "normal", "recovery"] as const;
export type FocusMode = (typeof FOCUS_MODES)[number];

const normalizeFocusMode = (
  value: string | null | undefined,
  fallback: FocusMode = "normal",
): FocusMode => {
  const mode = String(value ?? "").toLowerCase();
  return FOCUS_MODES.includes(mode as FocusMode) ? (mode as FocusMode) : fallback;
};

type UseSessionArgs = {
  refreshContextTimeline: (sid?: string | null) => void | Promise<void>;
  resetTimelineRefreshGate: () => void;
  setActionError: (value: string | null) => void;
  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
  /** Current capture health, used to warn if a session starts while capture is down. */
  captureReadiness?: SessionCaptureReadiness | null;
};

export const useSession = ({
  refreshContextTimeline,
  resetTimelineRefreshGate,
  setActionError,
  setLabelStatus,
  setLabelStatusWarning,
  captureReadiness,
}: UseSessionArgs) => {
  const [sessionGoal, setSessionGoal] = useState("");
  const [sessionRecord, setSessionRecord] = useState<SessionRecord | null>(null);
  const [sessionId, setSessionId] = useState<string | null>(null);
  const [focusMode, setFocusMode] = useState<FocusMode>("normal");
  const [recap, setRecap] = useState<SessionRecap | null>(null);
  const [surveyPending, setSurveyPending] = useState(false);
  // Roadmap 2.14. Tracks only whether *this* end-of-session prompt is still open. Skipping and
  // saving both close it; neither is remembered past the session it belongs to.
  const [reflectionPending, setReflectionPending] = useState(false);
  const [reflectionSaved, setReflectionSaved] = useState(false);

  const hydrateActiveSession = useCallback(async () => {
    const [settings, active] = await Promise.all([
      api.getSettings().catch(() => null),
      api.getActiveSession(),
    ]);
    const defaultFocusMode = normalizeFocusMode(settings?.defaultFocusMode);

    if (!active) {
      setFocusMode(defaultFocusMode);
      return;
    }

    setSessionRecord(active);
    setSessionId(active.sessionId);
    setSessionGoal(active.goal);
    setFocusMode(normalizeFocusMode(active.focusMode, defaultFocusMode));
    void refreshContextTimeline(active.sessionId);
  }, [refreshContextTimeline]);

  const handleLabel = useCallback(
    async (label: FocusLabel, source: LabelSource = "manual", notes?: string) => {
      if (!sessionId) {
        setLabelStatus("Start a session to save feedback.");
        return;
      }

      try {
        await api.submitLabel(sessionId, label, notes, source);
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
      // Warn (but don't block) if capture is compromised at start — the session
      // record exists, but it may not record activity. `null` clears the banner.
      setActionError(
        captureReadiness ? sessionStartCaptureWarning(captureReadiness) : null,
      );
      resetTimelineRefreshGate();
      void refreshContextTimeline(record.sessionId);
    } catch {
      setActionError("Could not start session. Check capture permissions and try again.");
    }
  }, [
    captureReadiness,
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
      setReflectionPending(true);
      setReflectionSaved(false);
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

  // Roadmap 2.14. Saves against the session that just ended, not a live one -- by the time
  // this prompt is on screen there is no active session, and `sessionId` still names the right
  // row because stopping does not clear it.
  const handleSaveReflection = useCallback(
    async (done: string | null, nextStep: string | null) => {
      if (!sessionId) {
        return;
      }
      try {
        await api.saveSessionReflection(sessionId, done, nextStep);
        setReflectionSaved(true);
        setActionError(null);
      } catch {
        setActionError("Could not save the reflection.");
      }
    },
    [sessionId, setActionError],
  );

  // Skip writes nothing at all: absent and skipped are the same state by design.
  const handleSkipReflection = useCallback(() => {
    setReflectionPending(false);
  }, []);

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

  const clearActivitySession = useCallback(() => {
    setSessionGoal("");
    setSessionRecord(null);
    setSessionId(null);
    setRecap(null);
    setSurveyPending(false);
    resetTimelineRefreshGate();
    void refreshContextTimeline(null);
  }, [refreshContextTimeline, resetTimelineRefreshGate]);

  const sessionStatusLabel = useMemo(
    () => (sessionRecord ? sessionRecord.status.toLowerCase() : "idle"),
    [sessionRecord],
  );

  return {
    clearActivitySession,
    focusMode,
    handleFocusModeChange,
    handleLabel,
    handleSaveReflection,
    handleSkipReflection,
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
    reflectionPending,
    reflectionSaved,
    surveyPending,
  };
};
