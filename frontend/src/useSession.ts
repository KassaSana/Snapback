import { useCallback, useRef, useState } from "react";

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
import { isSessionFragment } from "./sessionFragment";

// Re-exported from the pure module; this is where importers expect them.
export { FOCUS_MODES, normalizeFocusMode } from "./sessionCockpit";
export type { FocusMode } from "./sessionCockpit";

type PendingSwitch = { goal: string; focusMode: FocusMode };

type UseSessionArgs = {
  refreshContextTimeline: (sid?: string | null) => void | Promise<void>;
  resetTimelineRefreshGate: () => void;
  clearSessionLiveSignals: () => void;
  setActionError: (value: string | null) => void;
  setLabelStatus: (value: string | null) => void;
  setLabelStatusWarning: (value: boolean) => void;
  /** Current capture health, used to warn if a session starts while capture is down. */
  captureReadiness?: SessionCaptureReadiness | null;
};

export const useSession = ({
  refreshContextTimeline,
  resetTimelineRefreshGate,
  clearSessionLiveSignals,
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
  const [autoLabel, setAutoLabel] = useState<FocusLabel | null>(null);
  const [savedSessionRating, setSavedSessionRating] = useState<FocusLabel | null>(null);
  const [surveyPending, setSurveyPending] = useState(false);
  // Whether this end-of-session prompt is still open.
  const [reflectionPending, setReflectionPending] = useState(false);
  const [reflectionSaved, setReflectionSaved] = useState(false);
  // `sessionPending` drives the disabled state; the `inFlight` ref enforces it synchronously,
  // so Enter or a double click cannot start two sessions.
  const [sessionPending, setSessionPending] = useState(false);
  const inFlight = useRef(false);
  const labelInFlight = useRef(false);
  const sessionGeneration = useRef(0);
  /** After a switch stop: start this goal/mode once check-in (or discard) finishes. */
  const pendingSwitch = useRef<PendingSwitch | null>(null);
  const [labelPending, setLabelPending] = useState(false);

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

  // Best-effort and silent: by the time this runs `start_session` has already made the mode
  // the live policy, so the only thing a failure loses is the *default* for next time.
  const commitDefaultFocusMode = useCallback(async (mode: FocusMode) => {
    try {
      await api.setFocusMode(mode);
    } catch {
      // The running session is unaffected; hydrate reads the default again next launch.
    }
  }, []);

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
        sessionGeneration.current += 1;
        setSessionRecord(record);
        setSessionId(record.sessionId);
        setLabelStatus(null);
        setLabelStatusWarning(false);
        setReflectionPending(false);
        setReflectionSaved(false);
        setSessionGoal(record.goal);
        setRecap(null);
        setAutoLabel(null);
        setSavedSessionRating(null);
        // Drop live cards from the previous generation immediately; the native epoch bump
        // rejects most stale dispatches, and this clears anything already painted.
        clearSessionLiveSignals();
        setSurveyPending(false);
        // Warn (but don't block) if capture is compromised at start — the session
        // record exists, but it may not record activity. `null` clears the banner.
        setActionError(
          captureReadiness ? sessionStartCaptureWarning(captureReadiness) : null,
        );
        resetTimelineRefreshGate();
        void refreshContextTimeline(record.sessionId);
        // Start commits the draft mode as the new default. Best-effort.
        void commitDefaultFocusMode(mode);
      } catch {
        setActionError("Could not start session. Check capture permissions and try again.");
      } finally {
        // Cleared in `finally`, or a failed start would wedge the button.
        inFlight.current = false;
        setSessionPending(false);
      }
    },
    [
      captureReadiness,
      clearSessionLiveSignals,
      commitDefaultFocusMode,
      refreshContextTimeline,
      resetTimelineRefreshGate,
      setActionError,
      setLabelStatus,
      setLabelStatusWarning,
    ],
  );

  const continuePendingSwitch = useCallback(async () => {
    const pending = pendingSwitch.current;
    if (!pending) return;
    pendingSwitch.current = null;
    await handleStartNamedSession(pending.goal, pending.focusMode);
  }, [handleStartNamedSession]);

  const handleLabel = useCallback(
    async (label: FocusLabel, source: LabelSource = "manual", notes?: string) => {
      if (!sessionId || (source !== "survey" && sessionRecord?.endedAtMs !== null)) {
        setLabelStatus("Start a session to save live feedback.");
        return;
      }

      if (labelInFlight.current) return;
      labelInFlight.current = true;
      setLabelPending(true);
      const generation = sessionGeneration.current;
      try {
        await api.submitLabel(sessionId, label, notes, source);
        if (generation !== sessionGeneration.current) return;
        const prefix =
          source === "hotkey"
            ? "Hotkey saved"
            : source === "survey"
              ? "Session rating saved"
              : "Saved";
        setLabelStatus(`${prefix}: ${focusStateLabel(label)}`);
        setLabelStatusWarning(false);
        if (source === "survey") {
          setSavedSessionRating(label);
          setSurveyPending(false);
          void continuePendingSwitch();
        }
      } catch {
        if (generation !== sessionGeneration.current) return;
        setLabelStatus("Could not save feedback. Try again.");
        setLabelStatusWarning(true);
      } finally {
        labelInFlight.current = false;
        setLabelPending(false);
      }
    },
    [continuePendingSwitch, sessionId, sessionRecord, setLabelStatus, setLabelStatusWarning],
  );

  const handleStartSession = useCallback(async () => {
    await handleStartNamedSession(sessionGoal, focusMode);
  }, [focusMode, handleStartNamedSession, sessionGoal]);

  // The stopped state, from the record the backend returned (shared by Stop and a switch).
  const applyStoppedSession = useCallback(
    async (record: SessionRecord) => {
      setSessionRecord(record);
      clearSessionLiveSignals();
      const [sessionRecap, savedLabel] = await Promise.all([
        api.getSessionRecap(record.sessionId),
        api.getSessionAutoLabel(record.sessionId).catch(() => null),
      ]);
      setRecap(sessionRecap);
      setAutoLabel(savedLabel);
      setSavedSessionRating(null);
      setSurveyPending(true);
      setReflectionPending(true);
      setReflectionSaved(false);
      setLabelStatus(savedLabel ? `Automatic label: ${focusStateLabel(savedLabel)}.` : null);
      setLabelStatusWarning(false);
      resetTimelineRefreshGate();
      void refreshContextTimeline(record.sessionId);
    },
    [
      clearSessionLiveSignals,
      refreshContextTimeline,
      resetTimelineRefreshGate,
      setLabelStatus,
      setLabelStatusWarning,
    ],
  );

  const handleStopSession = useCallback(async () => {
    if (!sessionId || inFlight.current) {
      return;
    }

    inFlight.current = true;
    setSessionPending(true);
    try {
      await applyStoppedSession(await api.stopSession(sessionId));
      setActionError(null);
    } catch {
      setActionError("Could not stop session or load recap.");
    } finally {
      inFlight.current = false;
      setSessionPending(false);
    }
  }, [applyStoppedSession, sessionId, setActionError]);

  /**
   * Two-phase switch: stop the current session honestly, keep check-in/recap open, then start
   * the drafted goal only after the user rates, skips, or discards a fragment.
   */
  const handleSwitchSession = useCallback(async (): Promise<boolean> => {
    const goal = sessionGoal.trim();
    if (!goal || !sessionId || inFlight.current) {
      return false;
    }

    const next: PendingSwitch = { goal, focusMode };
    inFlight.current = true;
    setSessionPending(true);
    try {
      const stopped = await api.stopSession(sessionId);
      pendingSwitch.current = next;
      await applyStoppedSession(stopped);
      // Keep the drafted goal visible so the pending switch is obvious after check-in.
      setSessionGoal(next.goal);
      setFocusMode(next.focusMode);
      setActionError(null);
      return true;
    } catch {
      pendingSwitch.current = null;
      setActionError("Could not stop the current session, so it is still running.");
      return false;
    } finally {
      inFlight.current = false;
      setSessionPending(false);
    }
  }, [applyStoppedSession, focusMode, sessionGoal, sessionId, setActionError]);

  // "Keep this session": nothing native was touched, so just reset the draft.
  const cancelSwitch = useCallback(() => {
    if (!sessionRecord) return;
    setSessionGoal(sessionRecord.goal);
    setFocusMode(normalizeFocusMode(sessionRecord.focusMode, focusMode));
  }, [focusMode, sessionRecord]);

  const handleDiscardFragment = useCallback(async () => {
    if (!sessionId || !recap || !isSessionFragment(recap)) return;
    try {
      await api.deleteSession(sessionId);
      setRecap(null);
      setAutoLabel(null);
      setSavedSessionRating(null);
      setSurveyPending(false);
      setReflectionPending(false);
      setReflectionSaved(false);
      setSessionRecord(null);
      setSessionId(null);
      setLabelStatus("Discarded the short session.");
      setLabelStatusWarning(false);
      setActionError(null);
      await continuePendingSwitch();
    } catch {
      setActionError("Could not discard the short session.");
    }
  }, [
    continuePendingSwitch,
    recap,
    sessionId,
    setActionError,
    setLabelStatus,
    setLabelStatusWarning,
  ]);

  // Saves against the session that just ended (`sessionId` still names it).
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

  // Form state only: the mode the next session starts with. Start commits it.
  const setDraftFocusMode = useCallback((mode: FocusMode) => {
    setFocusMode(mode);
  }, []);

  // The explicit default editor (the permission wizard); persists immediately and moves the
  // draft. Failures are reported.
  const handleFocusModeChange = useCallback(
    async (mode: FocusMode) => {
      setFocusMode(mode);
      try {
        await api.setFocusMode(mode);
      } catch {
        setActionError("Could not save the default focus mode.");
      }
    },
    [setActionError],
  );

  const handleSkipSurvey = useCallback(async () => {
    if (autoLabel) {
      await handleLabel(autoLabel, "survey", "confirmed automatic label");
      return;
    }
    setSurveyPending(false);
    setSavedSessionRating(null);
    setLabelStatus("Skipped check-in.");
    setLabelStatusWarning(false);
    await continuePendingSwitch();
  }, [autoLabel, continuePendingSwitch, handleLabel, setLabelStatus, setLabelStatusWarning]);

  const handleChangeSessionRating = useCallback(() => {
    setSurveyPending(true);
  }, []);

  const clearActivitySession = useCallback(() => {
    sessionGeneration.current += 1;
    pendingSwitch.current = null;
    setLabelStatus(null);
    setLabelStatusWarning(false);
    setReflectionPending(false);
    setReflectionSaved(false);
    setSessionGoal("");
    setSessionRecord(null);
    setSessionId(null);
    setRecap(null);
    setAutoLabel(null);
    setSavedSessionRating(null);
    setSurveyPending(false);
    resetTimelineRefreshGate();
    void refreshContextTimeline(null);
  }, [refreshContextTimeline, resetTimelineRefreshGate, setLabelStatus, setLabelStatusWarning]);

  return {
    autoLabel,
    cancelSwitch,
    clearActivitySession,
    focusMode,
    handleDiscardFragment,
    handleFocusModeChange,
    setDraftFocusMode,
    handleLabel,
    handleSaveReflection,
    handleSkipReflection,
    handleSkipSurvey,
    handleChangeSessionRating,
    handleStartNamedSession,
    handleStartSession,
    handleStopSession,
    handleSwitchSession,
    hydrateActiveSession,
    sessionPending,
    labelPending,
    recap,
    sessionGoal,
    sessionId,
    sessionRecord,
    setSessionGoal,
    reflectionPending,
    reflectionSaved,
    savedSessionRating,
    surveyPending,
  };
};
