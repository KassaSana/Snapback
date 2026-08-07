import { useCallback, useEffect, useState } from "react";

import { api, type PrivacySettings } from "./api";
import { myDataExportMessage } from "./myDataExport";
import {
  activityDeletionIsWarning,
  activityDeletionMessage,
  activityDeletionRetainedNote,
} from "./activityDeletion";

export const privacyExclusionWarning = (value: string): string | null => {
  const length = value.trim().length;
  if (length === 1) return "A one-character exclusion can hide many unrelated apps.";
  if (length === 2) return "Short exclusions can hide many unrelated apps; use a distinctive app name.";
  return null;
};

export const usePrivacy = (onActivityDataDeleted?: () => void | Promise<void>) => {
  const [settings, setSettings] = useState<PrivacySettings | null>(null);
  const [exclusionInput, setExclusionInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [deletionStatus, setDeletionStatus] = useState<string | null>(null);
  // Roadmap 8.12. A partial erasure is not good news and must not be styled as if it were.
  const [deletionWarning, setDeletionWarning] = useState(false);
  const [deletionRetained, setDeletionRetained] = useState<string | null>(null);
  const [dataFolderStatus, setDataFolderStatus] = useState<string | null>(null);
  const [exportStatus, setExportStatus] = useState<string | null>(null);

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

  // Roadmap 7.6. Deliberately not a toggle or a stored setting — one shot, and the outcome is
  // reported as text rather than by opening a window the user may not see (on macOS Finder can
  // come forward behind Snapback's own window, which looks like nothing happened).
  const openDataFolder = useCallback(async () => {
    setError(null);
    setDataFolderStatus(null);
    try {
      const result = await api.openDataFolder();
      if (!result.supported) {
        setDataFolderStatus(`This build cannot open a file manager. Your data is in ${result.path}`);
      } else if (result.opened) {
        setDataFolderStatus(`Opened ${result.path}`);
      } else {
        setDataFolderStatus(`Could not open the folder. Your data is in ${result.path}`);
      }
    } catch {
      setError("Could not locate the data folder.");
    }
  }, []);

  // Roadmap 7.6. Reports the counts, not just "done": an export that wrote nothing because the
  // history is empty looks identical to a broken one unless the numbers are on screen.
  const exportMyData = useCallback(async () => {
    setBusy(true);
    setError(null);
    setExportStatus(null);
    try {
      const result = await api.exportMyData();
      // Roadmap 9.16. The export is complete now, so the message says so rather than staying
      // silent about it -- "we wrote a file" and "we wrote all of it" are different claims,
      // and this one is the reason the feature exists.
      setExportStatus(myDataExportMessage(result));
    } catch {
      setError("Could not export your data.");
    } finally {
      setBusy(false);
    }
  }, []);

  const deleteAllActivityData = useCallback(async () => {
    setBusy(true);
    setError(null);
    setDeletionStatus(null);
    setDeletionWarning(false);
    setDeletionRetained(null);
    try {
      const result = await api.deleteAllActivityData();
      try {
        await onActivityDataDeleted?.();
      } catch {
        // The backend deletion has already succeeded. A failed best-effort UI refresh
        // must not tell the user that their stored activity still exists.
      }
      // Roadmap 8.12. The message is derived from what the native side reported rather than
      // fixed: the operation can legitimately half-succeed, and a flat "deleted" over a
      // partial result is the specific claim this item exists to stop.
      setDeletionStatus(activityDeletionMessage(result));
      setDeletionWarning(activityDeletionIsWarning(result));
      setDeletionRetained(activityDeletionRetainedNote(result));
    } catch {
      setError("Could not delete activity data.");
    } finally {
      setBusy(false);
    }
  }, [onActivityDataDeleted]);

  return {
    addExclusion,
    busy,
    dataFolderStatus,
    deleteAllActivityData,
    deletionStatus,
    deletionWarning,
    deletionRetained,
    error,
    exclusionWarning: privacyExclusionWarning(exclusionInput),
    exclusionInput,
    exportMyData,
    exportStatus,
    openDataFolder,
    removeExclusion,
    setExclusionInput,
    setPrivateMode,
    settings,
  };
};
