import { useCallback, useState } from "react";

import { api, type RecordingStatus } from "./api";

// Roadmap 2.10. "blocked" is the safe initial value: before the first answer arrives, saying
// nothing is being captured is the claim that cannot mislead.
const INITIAL_STATUS: RecordingStatus = { state: "blocked", privatePauseRemainingMs: 0 };

type UseRecordingStatusArgs = {
  setActionError: (value: string | null) => void;
};

export const useRecordingStatus = ({ setActionError }: UseRecordingStatusArgs) => {
  const [recordingStatus, setRecordingStatus] = useState<RecordingStatus>(INITIAL_STATUS);

  const refreshRecordingStatus = useCallback(async () => {
    try {
      setRecordingStatus(await api.getRecordingStatus());
    } catch {
      // Leave the last good answer rather than flipping the headline state on one bad poll.
    }
  }, []);

  const handlePausePrivately = useCallback(
    async (minutes: number) => {
      try {
        setRecordingStatus(await api.pauseRecordingPrivately(minutes));
        setActionError(null);
      } catch {
        setActionError("Could not pause recording.");
      }
    },
    [setActionError],
  );

  const handleResumeRecording = useCallback(async () => {
    try {
      setRecordingStatus(await api.resumeRecording());
      setActionError(null);
    } catch {
      setActionError("Could not resume recording.");
    }
  }, [setActionError]);

  return {
    recordingStatus,
    refreshRecordingStatus,
    handlePausePrivately,
    handleResumeRecording,
  };
};
