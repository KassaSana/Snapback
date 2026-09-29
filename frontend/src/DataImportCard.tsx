// The import UI, beside Privacy's exports. States the scope plainly -- replace, not merge --
// since that cannot be undone after the restart.

import { memo } from "react";

import type { DataImportCandidate } from "./api";

type Props = {
  busy: boolean;
  candidate: DataImportCandidate | null;
  path: string;
  pending: boolean;
  status: string | null;
  warning: boolean;
  setPath: (value: string) => void;
  onBrowse?: () => void;
  onInspect: () => void;
  onConfirm: () => void;
  onCancel: () => void;
  onDismissCandidate: () => void;
};

export const DataImportCard = memo(function DataImportCard({
  busy,
  candidate,
  path,
  pending,
  status,
  warning,
  setPath,
  onBrowse,
  onInspect,
  onConfirm,
  onCancel,
  onDismissCandidate,
}: Props) {
  return (
    <section className="card">
      <div className="card-header">
        <h2>Import data</h2>
      </div>

      <p className="helper-text">
        Restore a <code>focoflow.db</code> from another machine, a backup, or a reinstall.
        Snapback keeps everything on this machine, which also means nothing else is holding a
        copy of your history.
      </p>

      {pending ? (
        // A staged import is the one state where doing nothing is destructive, so it gets the
        // loudest treatment on the card and keeps its undo in reach.
        <div className="notice notice-untracked" role="status">
          <p>
            An import is waiting. Your current data will be replaced the next time Snapback
            starts.
          </p>
          <button
            type="button"
            className="secondary-button"
            onClick={onCancel}
            disabled={busy}
          >
            Cancel the import
          </button>
        </div>
      ) : (
        <>
          <label className="field">
            <span>Database file</span>
            <input
              type="text"
              placeholder="C:\Users\you\Downloads\focoflow.db"
              value={path}
              onChange={(event) => setPath(event.target.value)}
              disabled={busy}
            />
          </label>

          {!candidate?.acceptable && (
            <div className="button-row">
              {onBrowse && (
                <button
                  type="button"
                  className="secondary-button"
                  onClick={onBrowse}
                  disabled={busy}
                >
                  Browse...
                </button>
              )}
              <button
                type="button"
                className="secondary-button"
                onClick={onInspect}
                disabled={busy || path.trim().length === 0}
              >
                Check this file
              </button>
            </div>
          )}


          {candidate?.acceptable && (
            <div className="notice notice-untracked" role="status">
              <p>
                That file holds <strong>{candidate.sessionCount}</strong>{" "}
                {candidate.sessionCount === 1 ? "session" : "sessions"}.
              </p>
              {/* Stated plainly: importing discards the current history. */}
              <p>
                Importing <strong>replaces</strong> everything Snapback currently holds; the two
                histories are not merged. Your current data is saved to a backup file beside the
                database first.
              </p>
              <div className="button-row">
                <button
                  type="button"
                  className="primary-button"
                  onClick={onConfirm}
                  disabled={busy}
                >
                  Replace my data with this
                </button>
                <button
                  type="button"
                  className="link-button"
                  onClick={onDismissCandidate}
                  disabled={busy}
                >
                  Keep my current data
                </button>
              </div>
            </div>
          )}
        </>
      )}

      {status && (
        <p className={warning ? "helper-text helper-error" : "helper-text"} role="status">
          {status}
        </p>
      )}
    </section>
  );
});
