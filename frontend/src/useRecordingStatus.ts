import { useCallback, useEffect, useRef, useState } from "react";

import { api, type RecordingStatus } from "./api";

// "blocked" until the first answer: the claim that cannot mislead.
const INITIAL_STATUS: RecordingStatus = {
  state: "blocked",
  privatePauseRemainingMs: 0,
  alertSnoozeRemainingMs: 0,
};

// How long after a native deadline to ask again (covers clock skew).
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
  // True when the last confirmation failed; the shown state is then the last known answer.
  const [unconfirmed, setUnconfirmed] = useState(false);

  // Answers are stamped by when they were asked; only the newest is applied. Native events
  // count as newest.
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

  // Tray and Settings changes are announced natively and land here.
  const applyRecordingStatusEvent = useCallback(
    (status: RecordingStatus) => {
      apply(++askedSeq.current, status);
    },
    [apply],
  );

  // Refresh once just after a pause or snooze deadline.
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

  // Ends a tray snooze; returns the same RecordingStatus as the pause commands.
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
