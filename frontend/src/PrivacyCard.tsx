import { memo, useState } from "react";

import type { PrivacySettings } from "./api";

type PrivacyCardProps = {
  busy: boolean;
  dataFolderStatus?: string | null;
  error: string | null;
  exclusionWarning: string | null;
  exclusionInput: string;
  exportStatus?: string | null;
  onAddExclusion: () => void | Promise<void>;
  onDeleteAllActivityData: () => void | Promise<void>;
  onExportMyData?: () => void | Promise<void>;
  onOpenDataFolder?: () => void | Promise<void>;
  onPrivateModeChange: (enabled: boolean) => void | Promise<void>;
  onRemoveExclusion: (app: string) => void | Promise<void>;
  setExclusionInput: (value: string) => void;
  settings: PrivacySettings | null;
  deletionStatus: string | null;
  /** Roadmap 8.12. True when the erasure was partial, so it is not styled as success. */
  deletionWarning?: boolean;
  /** What "delete activity" deliberately kept, always shown alongside the outcome. */
  deletionRetained?: string | null;
};

export const PrivacyCard = memo(function PrivacyCard({
  busy,
  dataFolderStatus = null,
  error,
  exclusionWarning,
  exclusionInput,
  exportStatus = null,
  onAddExclusion,
  onDeleteAllActivityData,
  onExportMyData,
  onOpenDataFolder,
  onPrivateModeChange,
  onRemoveExclusion,
  setExclusionInput,
  settings,
  deletionStatus,
  deletionWarning = false,
  deletionRetained = null,
}: PrivacyCardProps) {
  const [confirmingDelete, setConfirmingDelete] = useState(false);

  return (
    <section className="card config-card">
      <div className="card-header">
        <h2>Privacy</h2>
        <span className="pill">local only</span>
      </div>
      <p className="helper-text">
        Snapback stores activity on this device. Private mode pauses capture processing, and
        excluded app names never write titles or predictions. Exclusions match whole words,
        case-insensitively.
      </p>
      <label className="toggle-row">
        <span>
          <strong>Private mode</strong>
          <span className="helper-text">Pause recording everywhere.</span>
        </span>
        <input
          type="checkbox"
          aria-label="Private mode"
          checked={settings?.privateMode ?? false}
          disabled={busy || settings === null}
          onChange={(event) => void onPrivateModeChange(event.target.checked)}
        />
      </label>
      <label className="field">
        <span>Excluded app name or word</span>
        <input
          type="text"
          placeholder="Banking, 1Password"
          value={exclusionInput}
          disabled={busy || settings === null}
          onChange={(event) => setExclusionInput(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") void onAddExclusion();
          }}
        />
      </label>
      <button className="secondary-button" disabled={busy || !exclusionInput.trim()} onClick={() => void onAddExclusion()}>
        Add exclusion
      </button>
      {exclusionWarning ? <p className="helper-text alert">{exclusionWarning}</p> : null}
      <ul className="rules-list">
        {(settings?.excludedApps ?? []).length === 0 ? (
          <li className="rules-empty">No excluded apps.</li>
        ) : (
          settings?.excludedApps.map((app) => (
            <li key={app} className="rules-item">
              <span className="rules-pattern">{app}</span>
              <button className="secondary-button rules-delete" disabled={busy} onClick={() => void onRemoveExclusion(app)}>
                Remove
              </button>
            </li>
          ))
        )}
      </ul>
      {/* Roadmap 7.6. Inspecting comes before destroying, so this sits above the danger zone:
          the user should be able to look at what was collected before deciding to delete it. */}
      {onOpenDataFolder ? (
        <div className="privacy-inspect-zone">
          <h3>Your data on this device</h3>
          <p className="helper-text">
            Everything Snapback records lives in one folder — the database, exported CSVs, and
            logs. Nothing leaves this device.
          </p>
          <div className="button-row">
            <button
              className="secondary-button"
              disabled={busy}
              onClick={() => void onOpenDataFolder()}
            >
              Show data folder
            </button>
            {onExportMyData ? (
              <button
                className="secondary-button"
                disabled={busy}
                onClick={() => void onExportMyData()}
              >
                Export my data
              </button>
            ) : null}
          </div>
          {/* Named for what it is, not how it is stored: this is the readable record, as
              opposed to "Export training data", which produces a feature matrix. */}
          {onExportMyData ? (
            <p className="helper-text">
              “Export my data” writes one readable Markdown file of your sessions and the
              windows captured during them.
            </p>
          ) : null}
          {exportStatus ? <p className="helper-text data-folder-path">{exportStatus}</p> : null}
          {/* The path is rendered as selectable text, not just spoken by the file manager:
              if opening failed, this line is the whole answer. */}
          {dataFolderStatus ? <p className="helper-text data-folder-path">{dataFolderStatus}</p> : null}
        </div>
      ) : null}
      <div className="privacy-danger-zone">
        <h3>Delete activity data</h3>
        <p className="helper-text">
          Permanently delete every session, prediction, label, and captured context stored
          on this device, including training CSVs and exported summaries. Privacy exclusions,
          app rules, support bundles, and deployed models are kept.
        </p>
        {confirmingDelete ? (
          <div className="button-row">
            <button
              className="danger-button"
              disabled={busy}
              onClick={() => {
                setConfirmingDelete(false);
                void onDeleteAllActivityData();
              }}
            >
              Confirm permanent deletion
            </button>
            <button
              className="secondary-button"
              disabled={busy}
              onClick={() => setConfirmingDelete(false)}
            >
              Cancel
            </button>
          </div>
        ) : (
          <button
            className="danger-button"
            disabled={busy}
            onClick={() => setConfirmingDelete(true)}
          >
            Delete all activity data
          </button>
        )}
      </div>
      {deletionStatus ? (
        <p className={`helper-text ${deletionWarning ? "alert" : "success"}`}>{deletionStatus}</p>
      ) : null}
      {deletionRetained ? <p className="helper-text">{deletionRetained}</p> : null}
      {error ? <p className="helper-text alert">{error}</p> : null}
    </section>
  );
});
