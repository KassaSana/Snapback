import type { PrivacySettings } from "./api";

type PrivacyCardProps = {
  busy: boolean;
  error: string | null;
  exclusionInput: string;
  onAddExclusion: () => void | Promise<void>;
  onPrivateModeChange: (enabled: boolean) => void | Promise<void>;
  onRemoveExclusion: (app: string) => void | Promise<void>;
  setExclusionInput: (value: string) => void;
  settings: PrivacySettings | null;
};

export function PrivacyCard({
  busy,
  error,
  exclusionInput,
  onAddExclusion,
  onPrivateModeChange,
  onRemoveExclusion,
  setExclusionInput,
  settings,
}: PrivacyCardProps) {
  return (
    <section className="card config-card">
      <div className="card-header">
        <h2>Privacy</h2>
        <span className="pill">local only</span>
      </div>
      <p className="helper-text">
        Snapback stores activity on this device. Private mode pauses capture processing, and
        excluded apps never write titles or predictions.
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
        <span>Excluded app or pattern</span>
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
      {error ? <p className="helper-text alert">{error}</p> : null}
    </section>
  );
}
