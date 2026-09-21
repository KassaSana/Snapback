import { useCallback, useEffect, useRef, useState } from "react";

import { api, type RecordingStatus } from "./api";

// Roadmap 2.10. "blocked" is the safe initial value: before the first answer arrives, saying
// nothing is being captured is the claim that cannot mislead.
const INITIAL_STATUS: RecordingStatus = {
  state: "blocked",
  privatePauseRemainingMs: 0,
  alertSnoozeRemainingMs: 0,
};

// How long after a native deadline to ask again. The backend lapses a timed pause on the read
// that finds the deadline passed, so one refresh just after it converges; the slack covers
// clock skew between the two sides' notions of "now".
export const DEADLINE_SLACK_MS = 250;

/**
 * The next instant the answer changes on its own, or null when nothing is counting down.
 * A timed pause ends and recording resumes; a snooze ends and alerts come back. Either way
 * the header would otherwise keep the countdown it was last told about.
 */
export const nextRecordingDeadlineMs = (status: RecordingStatus): number | null => {
  const deadlines = [status.privatePauseRemainingMs, status.alertSnoozeRemainingMs].filter(
    (ms) => ms > 0,
  );
  return deadlines.length === 0 ? null : Math.min(...deadlines) + DEADLINE_SLACK_MS;
};

type UseRecordingStatusArgs = {
  setActionError: (value: string | null) => void;
};

export const useRecordingStatus = ({ setActionError }: UseRecordingStatusArgs) => {
  const [recordingStatus, setRecordingStatus] = useState<RecordingStatus>(INITIAL_STATUS);
  // True when the last attempt to confirm the state with the app failed. The displayed state
  // is then the last answer we had, which is worth saying: "Recording" on a header that has
  // not been able to ask for a while is a claim, not an observation.
  const [unconfirmed, setUnconfirmed] = useState(false);

  // Every answer is stamped by when it was *asked for*, and only the newest asked-for answer
  // is applied. A pause click and a routine refresh can be in flight together; if the
  // refresh was issued first but lands second, its "recording" must not overwrite the
  // "pausedPrivate" the click already showed. Native events count as newest: they describe
  // a change that happened after anything currently in flight was asked.
  const askedSeq = useRef(0);
  const appliedSeq = useRef(0);
  const apply = useCallback((seq: number, status: RecordingStatus) => {
    if (seq < appliedSeq.current) return false;
    appliedSeq.current = seq;
    setRecordingStatus(status);
    setUnconfirmed(false);
    return true;
  }, []);
  const ask = useCallback(
    async (request: () => Promise<RecordingStatus>) => {
      const seq = ++askedSeq.current;
      apply(seq, await request());
    },
    [apply],
  );

  const refreshRecordingStatus = useCallback(async () => {
    try {
      await ask(api.getRecordingStatus);
    } catch {
      // Leave the last good answer rather than flipping the headline state on one bad poll,
      // but stop presenting it as confirmed.
      setUnconfirmed(true);
    }
  }, [ask]);

  // Roadmap 2.10 / 2.16. The tray and the Settings toggle change the answer without a command
  // from this side. The native side announces those; this is where the announcement lands.
  const applyRecordingStatusEvent = useCallback(
    (status: RecordingStatus) => {
      apply(++askedSeq.current, status);
    },
    [apply],
  );

  // A timed pause or a snooze lapses on the backend's clock. Nothing else on this side runs at
  // that moment, so schedule one refresh for just after the deadline; the answer it returns
  // either carries the next deadline or none.
  useEffect(() => {
    const delay = nextRecordingDeadlineMs(recordingStatus);
    if (delay === null) return;
    const timer = window.setTimeout(() => {
      void refreshRecordingStatus();
    }, delay);
    return () => window.clearTimeout(timer);
  }, [recordingStatus, refreshRecordingStatus]);

  const handlePausePrivately = useCallback(
    async (minutes: number) => {
      try {
        await ask(() => api.pauseRecordingPrivately(minutes));
        setActionError(null);
      } catch {
        setActionError("Could not pause recording.");
      }
    },
    [ask, setActionError],
  );

  const handleResumeRecording = useCallback(async () => {
    try {
      await ask(api.resumeRecording);
      setActionError(null);
    } catch {
      setActionError("Could not resume recording.");
    }
  }, [ask, setActionError]);

  // Roadmap 2.16. Ends a snooze started from the tray. It returns the same RecordingStatus the
  // pause commands do, so the card updates from one source rather than polling afterwards.
  const handleResumeAlerts = useCallback(async () => {
    try {
      await ask(api.resumeAlerts);
      setActionError(null);
    } catch {
      setActionError("Could not resume alerts.");
    }
  }, [ask, setActionError]);

  return {
    applyRecordingStatusEvent,
    handleResumeAlerts,
    recordingStatus,
    recordingStatusUnconfirmed: unconfirmed,
    refreshRecordingStatus,
    handlePausePrivately,
    handleResumeRecording,
  };
};
