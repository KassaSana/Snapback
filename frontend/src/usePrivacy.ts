import { useCallback, useEffect, useState } from "react";

import { api, type PrivacySettings } from "./api";

export const usePrivacy = () => {
  const [settings, setSettings] = useState<PrivacySettings | null>(null);
  const [exclusionInput, setExclusionInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      setSettings(await api.getPrivacySettings());
      setError(null);
    } catch {
      setError("Could not load privacy settings.");
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const setPrivateMode = useCallback(async (enabled: boolean) => {
    setBusy(true);
    setError(null);
    try {
      setSettings(await api.setPrivateMode(enabled));
    } catch {
      setError("Could not update private mode.");
    } finally {
      setBusy(false);
    }
  }, []);

  const saveExclusions = useCallback(async (excludedApps: string[]) => {
    setBusy(true);
    setError(null);
    try {
      setSettings(await api.setPrivacyExclusions(excludedApps));
    } catch {
      setError("Could not update excluded apps.");
    } finally {
      setBusy(false);
    }
  }, []);

  const addExclusion = useCallback(async () => {
    const value = exclusionInput.trim();
    if (!value || !settings) return;
    if (settings.excludedApps.some((app) => app.toLowerCase() === value.toLowerCase())) {
      setExclusionInput("");
      return;
    }
    await saveExclusions([...settings.excludedApps, value]);
    setExclusionInput("");
  }, [exclusionInput, saveExclusions, settings]);

  const removeExclusion = useCallback(
    (app: string) => saveExclusions((settings?.excludedApps ?? []).filter((entry) => entry !== app)),
    [saveExclusions, settings],
  );

  return {
    addExclusion,
    busy,
    error,
    exclusionInput,
    removeExclusion,
    setExclusionInput,
    setPrivateMode,
    settings,
  };
};
