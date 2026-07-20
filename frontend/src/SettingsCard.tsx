import type { AutostartStatus } from "./api";

type SettingsCardProps = {
  busy: boolean;
  error: string | null;
  onAutostartChange: (enabled: boolean) => void;
  status: AutostartStatus | null;
};

export function SettingsCard({
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
        <p className="helper-text">Start-on-login is currently available on Windows.</p>
      ) : null}
    </section>
  );
}
