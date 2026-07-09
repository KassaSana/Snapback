type PermissionsCardProps = {
  captureEventsDropped: number;
  captureFailed: boolean;
  captureFailureReason: string | null;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  onRefreshPermissions: () => void;
  permissionMessage: string | null;
  permissionSteps: string[];
};

export function PermissionsCard({
  captureEventsDropped,
  captureFailed,
  captureFailureReason,
  captureProbeConfirmed,
  captureRunning,
  onRefreshPermissions,
  permissionMessage,
  permissionSteps,
}: PermissionsCardProps) {
  return (
    <section className={`card config-card${captureFailed ? " config-card-alert" : ""}`}>
      <div className="card-header">
        <h2>Permissions</h2>
        <span className={`pill${captureFailed ? " pill-alert" : ""}`}>
          {captureFailed
            ? "capture failed"
            : captureRunning
              ? "listener running"
              : captureProbeConfirmed
                ? "permissions ready"
                : "listener pending"}
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
      {captureEventsDropped > 0 ? (
        <p className="helper-text alert">
          {captureEventsDropped} capture event{captureEventsDropped === 1 ? "" : "s"} dropped —
          the engine loop fell behind. If this keeps growing, restart Snapback.
        </p>
      ) : null}
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
