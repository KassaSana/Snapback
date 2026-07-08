type PermissionsCardProps = {
  captureFailed: boolean;
  captureFailureReason: string | null;
  onRefreshPermissions: () => void;
  permissionMessage: string | null;
  permissionSteps: string[];
};

export function PermissionsCard({
  captureFailed,
  captureFailureReason,
  onRefreshPermissions,
  permissionMessage,
  permissionSteps,
}: PermissionsCardProps) {
  return (
    <section className={`card config-card${captureFailed ? " config-card-alert" : ""}`}>
      <div className="card-header">
        <h2>Permissions</h2>
        <span className={`pill${captureFailed ? " pill-alert" : ""}`}>
          {captureFailed ? "capture failed" : "local desktop"}
        </span>
      </div>
      {captureFailed ? (
        <p className="helper-text alert">
          Capture listener stopped
          {captureFailureReason ? `: ${captureFailureReason}` : "."}
        </p>
      ) : null}
      <p className="helper-text">
        {permissionMessage ||
          "Snapback runs locally. Grant Accessibility + Input Monitoring on macOS."}
      </p>
      {permissionSteps.length > 0 ? (
        <ol className="permission-steps">
          {permissionSteps.map((step) => (
            <li key={step}>{step}</li>
          ))}
        </ol>
      ) : null}
      <button className="secondary-button" onClick={() => void onRefreshPermissions()}>
        Refresh permissions
      </button>
    </section>
  );
}
