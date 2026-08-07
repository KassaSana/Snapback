import { useCallback, useEffect, useState } from "react";

import { api } from "./api";
import { DEFAULT_IDLE_THRESHOLD_SECS } from "./idleThreshold";

// Roadmap 7.23. Same shape as useAutostart: read once, write on change, and keep the value
// the native side returned rather than the one that was sent. The command applies the change
// to the running detector before it answers, so its reply is the only value that is true.
export const useIdleThreshold = () => {
  const [seconds, setSeconds] = useState(DEFAULT_IDLE_THRESHOLD_SECS);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      const settings = await api.getSettings();
      setSeconds(settings.idleThresholdSecs);
      setError(null);
    } catch {
      setError("Could not read the away-time setting.");
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const update = useCallback(async (next: number) => {
    setBusy(true);
    setError(null);
    try {
      const settings = await api.setIdleThreshold(next);
      setSeconds(settings.idleThresholdSecs);
    } catch {
      // The previous value stays on screen. The native setter rejects before mutating
      // anything, so "the call failed" and "the setting is unchanged" are the same fact.
      setError("Could not update the away-time setting.");
    } finally {
      setBusy(false);
    }
  }, []);

  return { busy, error, refresh, seconds, update };
};
