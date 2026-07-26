import { useState } from "react";

import type { PrivacySettings } from "./api";

type PrivacyCardProps = {
  busy: boolean;
  error: string | null;
  exclusionWarning: string | null;
  exclusionInput: string;
  onAddExclusion: () => void | Promise<void>;
  onDeleteAllActivityData: () => void | Promise<void>;
  onPrivateModeChange: (enabled: boolean) => void | Promise<void>;
  onRemoveExclusion: (app: string) => void | Promise<void>;
  setExclusionInput: (value: string) => void;
  settings: PrivacySettings | null;
  deletionStatus: string | null;
};

export function PrivacyCard({
  busy,
  error,
  exclusionWarning,
  exclusionInput,
  onAddExclusion,
  onDeleteAllActivityData,
  onPrivateModeChange,
  onRemoveExclusion,
  setExclusionInput,
  settings,
  deletionStatus,
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
      <div className="privacy-danger-zone">
        <h3>Delete activity data</h3>
        <p className="helper-text">
          Permanently delete every session, prediction, label, and captured context stored
          on this device. Privacy exclusions and app rules are kept.
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
      {deletionStatus ? <p className="helper-text success">{deletionStatus}</p> : null}
      {error ? <p className="helper-text alert">{error}</p> : null}
    </section>
  );
}
