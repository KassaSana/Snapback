import { memo } from "react";

import type { AutostartStatus } from "./api";

type SettingsCardProps = {
  busy: boolean;
  error: string | null;
  onAutostartChange: (enabled: boolean) => void;
  status: AutostartStatus | null;
};

export const SettingsCard = memo(function SettingsCard({
  busy,
  error,
  onAutostartChange,
  status,
}: SettingsCardProps) {
  const supported = status?.supported ?? false;

  return (
    <section className="card config-card">
      <div className="card-header">
        <h2>Settings</h2>
        <span className="pill">{supported ? "available" : "platform default"}</span>
      </div>
      <label className={`toggle-row${!supported ? " toggle-row-disabled" : ""}`}>
        <span>
          <strong>Start on login</strong>
          <span className="helper-text">Launch Snapback when you sign in.</span>
        </span>
        <input
          type="checkbox"
          checked={status?.enabled ?? false}
          disabled={!supported || busy || status === null}
          onChange={(event) => void onAutostartChange(event.target.checked)}
        />
      </label>
      {error ? <p className="helper-text alert">{error}</p> : null}
      {!supported ? (
        // Roadmap 3.0 added the macOS launchd backend, so this line named the wrong platforms
        // the moment that landed. It is driven by `supported` from the native side rather than
        // by a hardcoded OS check, so the only thing to keep true is the sentence itself.
        <p className="helper-text">Start-on-login is available on Windows and macOS.</p>
      ) : null}
    </section>
  );
});
