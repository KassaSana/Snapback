import { useCallback, useEffect, useState } from "react";

import { api, type AutostartStatus } from "./api";

export const useAutostart = () => {
  const [status, setStatus] = useState<AutostartStatus | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      setStatus(await api.getAutostart());
      setError(null);
    } catch {
      setError("Could not read the start-on-login setting.");
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const setEnabled = useCallback(async (enabled: boolean) => {
    setBusy(true);
    setError(null);
    try {
      setStatus(await api.setAutostart(enabled));
    } catch {
      setError("Could not update the start-on-login setting.");
    } finally {
      setBusy(false);
    }
  }, []);

  return { busy, error, refresh, setEnabled, status };
};
