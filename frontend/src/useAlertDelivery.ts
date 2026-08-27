import { useCallback, useEffect, useState } from "react";

import { api } from "./api";
import type { AlertDeliverySettings } from "./alertDelivery";

// Roadmap 2.16. Same shape as useIdleThreshold: read once, write on change, and keep the value
// the native side returned rather than the one that was sent. The setter validates and rejects
// before mutating anything, so its reply is the only value that is true.
const DEFAULTS: AlertDeliverySettings = {
  snapback: ["overlay"],
  hyperfocus: ["native"],
  pomodoro: ["inApp"],
  preview: "detailed",
  quietHoursEnabled: false,
  quietHoursStartMin: 22 * 60,
  quietHoursEndMin: 7 * 60,
  snoozedUntilWallMs: 0,
};

export const useAlertDelivery = () => {
  const [alerts, setAlerts] = useState<AlertDeliverySettings>(DEFAULTS);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      const settings = await api.getSettings();
      setAlerts(settings.alerts);
      setError(null);
    } catch {
      setError("Could not read the interruption settings.");
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const update = useCallback(async (next: AlertDeliverySettings) => {
    setBusy(true);
    setError(null);
    try {
      const settings = await api.setAlertDelivery(next);
      setAlerts(settings.alerts);
    } catch {
      // The previous value stays on screen: the native setter rejects before mutating, so
      // "the call failed" and "the setting is unchanged" are the same fact.
      setError("Could not update the interruption settings.");
    } finally {
      setBusy(false);
    }
  }, []);

  return { alerts, busy, error, refresh, update };
};
