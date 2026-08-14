// Roadmap 9.14. The import half of "your data is yours".
//
// Two-step by design, and the steps are not decoration. `inspect` is read-only and answers
// "what is this file?"; only after the user has seen that answer does `stage` park it. A
// destructive whole-database replace behind one button is the wrong shape for an action whose
// mistake case is "all of my history is gone".
//
// Nothing here applies anything. The running app holds the database open (9.8), so the swap is
// performed at the next launch — which also means a staged import is cancellable right up until
// the restart, and that cancel is the undo this feature would otherwise lack.

import { useCallback, useState } from "react";

import { api, type DataImportCandidate } from "./api";

export const useDataImport = () => {
  const [path, setPath] = useState("");
  const [candidate, setCandidate] = useState<DataImportCandidate | null>(null);
  const [status, setStatus] = useState<string | null>(null);
  const [warning, setWarning] = useState(false);
  const [pending, setPending] = useState(false);
  const [busy, setBusy] = useState(false);

  const refreshImportStatus = useCallback(async () => {
    try {
      const result = await api.getDataImportStatus();
      setPending(result.pending);
    } catch {
      // Non-critical: the section still works, it just cannot show a prior staged import.
    }
  }, []);

  const inspect = useCallback(async () => {
    const trimmed = path.trim();
    if (!trimmed || busy) return;
    setBusy(true);
    setStatus(null);
    try {
      const result = await api.inspectDataImport(trimmed);
      setCandidate(result);
      // A refusal is the answer, not an error — the native side has already phrased it for the
      // user, so it is shown verbatim rather than replaced with something vaguer.
      if (!result.acceptable) {
        setStatus(result.message);
        setWarning(true);
      } else {
        setWarning(false);
      }
    } catch {
      setCandidate(null);
      setStatus("Could not read that file.");
      setWarning(true);
    } finally {
      setBusy(false);
    }
  }, [busy, path]);

  const confirm = useCallback(async () => {
    const trimmed = path.trim();
    if (!trimmed || busy || !candidate?.acceptable) return;
    setBusy(true);
    try {
      const result = await api.stageDataImport(trimmed);
      setStatus(result.message);
      setWarning(!result.ok);
      setPending(result.ok);
      if (result.ok) setCandidate(null);
    } catch {
      setStatus("Could not prepare that import.");
      setWarning(true);
    } finally {
      setBusy(false);
    }
  }, [busy, candidate, path]);

  const cancel = useCallback(async () => {
    if (busy) return;
    setBusy(true);
    try {
      const result = await api.cancelDataImport();
      setPending(result.pending);
      setStatus(
        result.cancelled
          ? "Cancelled. Your current data will be kept."
          : "There was no import waiting.",
      );
      setWarning(false);
    } catch {
      setStatus("Could not cancel that import.");
      setWarning(true);
    } finally {
      setBusy(false);
    }
  }, [busy]);

  // Backing out of the confirmation without staging anything.
  const dismissCandidate = useCallback(() => {
    setCandidate(null);
    setStatus(null);
    setWarning(false);
  }, []);

  const browseAndInspect = useCallback(async () => {
    if (busy) return;
    try {
      const picked = await api.pickOpenFile({
        title: "Select Snapback Database to Import",
        filters: [
          { name: "SQLite Database (*.db)", pattern: "*.db" },
          { name: "All Files (*.*)", pattern: "*.*" },
        ],
      });
      if (picked && picked.ok && picked.path) {
        setPath(picked.path);
        setBusy(true);
        setStatus(null);
        try {
          const result = await api.inspectDataImport(picked.path);
          setCandidate(result);
          if (!result.acceptable) {
            setStatus(result.message);
            setWarning(true);
          } else {
            setWarning(false);
          }
        } catch {
          setCandidate(null);
          setStatus("Could not read that file.");
          setWarning(true);
        } finally {
          setBusy(false);
        }
      }
    } catch {
      // Best-effort; user cancellation or unhandled dialog error
    }
  }, [busy]);

  return {
    browseAndInspect,
    busy,
    cancel,
    candidate,
    confirm,
    dismissCandidate,
    inspect,
    path,
    pending,
    refreshImportStatus,
    setPath,
    status,
    warning,
  };
};

