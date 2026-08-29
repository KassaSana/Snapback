import { useCallback, useMemo, useRef, useState } from "react";

import {
  api,
  focusStateLabel,
  type FocusLabel,
  type LabelSource,
  type SessionRecap,
  type SessionRecord,
} from "./api";
import { sessionStartCaptureWarning, type SessionCaptureReadiness } from "./healthHints";
import { normalizeFocusMode, type FocusMode } from "./sessionCockpit";

// Roadmap 2.11 moved these to the pure module so the cockpit's rules could be unit-tested
// without React. Re-exported because they are the app's vocabulary for focus modes and every
// existing importer names this module.
export { FOCUS_MODES, normalizeFocusMode } from "./sessionCockpit";
export type { FocusMode } from "./sessionCockpit";

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
  // Roadmap 2.11. `sessionPending` drives the disabled state; `inFlight` enforces it.
  // A disabled button is a courtesy — Enter on a focused form, a synthetic click, or a second
  // click landing inside the await before React has re-rendered all reach the handler anyway.
  // The ref is checked and set synchronously, so the second caller returns before it can issue
  // a duplicate `start_session` and create a second row for one intent.
  const [sessionPending, setSessionPending] = useState(false);
  const inFlight = useRef(false);

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

  const handleStartNamedSession = useCallback(
    async (goalInput: string, mode: FocusMode) => {
      const goal = goalInput.trim();
      // Validation lives in the card, which can explain itself; this stays as the last line of
      // defence so a programmatic caller cannot open an unnamed session.
      if (!goal || inFlight.current) {
        return;
      }

      inFlight.current = true;
      setSessionPending(true);
      try {
        setFocusMode(mode);
        const record = await api.startSession(goal, mode);
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
      } finally {
        // Cleared in `finally`, never on the success path alone: a failed start that left the
        // guard set would wedge the button until reload, which is a worse bug than the duplicate
        // request it is here to prevent.
        inFlight.current = false;
        setSessionPending(false);
      }
    },
    [captureReadiness, refreshContextTimeline, resetTimelineRefreshGate, setActionError],
  );

  const handleStartSession = useCallback(async () => {
    await handleStartNamedSession(sessionGoal, focusMode);
  }, [focusMode, handleStartNamedSession, sessionGoal]);

  const handleStopSession = useCallback(async () => {
    if (!sessionId || inFlight.current) {
      return;
    }

    inFlight.current = true;
    setSessionPending(true);
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
    } finally {
      inFlight.current = false;
      setSessionPending(false);
    }
  }, [
    refreshContextTimeline,
    resetTimelineRefreshGate,
    sessionId,
    setActionError,
    setLabelStatus,
    setLabelStatusWarning,
  ]);

  /**
   * Roadmap 2.11's guarded "start a different session".
   *
   * Switching is stop-then-start rather than a single command because ADR-0005 makes a session
   * a declared, attended thing: the old one must end honestly, with its recap and label, before
   * a new one begins. If the stop fails the switch stops there — starting anyway would leave
   * two sessions the user believes are one.
   */
  const handleSwitchSession = useCallback(async () => {
    const goal = sessionGoal.trim();
    if (!goal || !sessionId || inFlight.current) {
      return;
    }

    inFlight.current = true;
    setSessionPending(true);
    try {
      await api.stopSession(sessionId);
    } catch {
      setActionError("Could not stop the current session, so it is still running.");
      inFlight.current = false;
      setSessionPending(false);
      return;
    }

    try {
      const record = await api.startSession(goal, focusMode);
      setSessionRecord(record);
      setSessionId(record.sessionId);
      setSessionGoal(record.goal);
      setRecap(null);
      setSurveyPending(false);
      setActionError(
        captureReadiness ? sessionStartCaptureWarning(captureReadiness) : null,
      );
      resetTimelineRefreshGate();
      void refreshContextTimeline(record.sessionId);
    } catch {
      // The old session really did stop, so say so rather than implying nothing happened.
      setActionError("Stopped the previous session, but could not start the new one.");
    } finally {
      inFlight.current = false;
      setSessionPending(false);
    }
  }, [
    captureReadiness,
    focusMode,
    refreshContextTimeline,
    resetTimelineRefreshGate,
    sessionGoal,
    sessionId,
    setActionError,
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
    handleStartNamedSession,
    handleStartSession,
    handleStopSession,
    handleSwitchSession,
    hydrateActiveSession,
    sessionPending,
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
