type PermissionsCardProps = {
  captureEventsDropped: number;
  captureFailed: boolean;
  captureFailureReason: string | null;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  captureStalled: boolean;
  onRefreshPermissions: () => void;
  onRequestPermissions: () => void;
  permissionMessage: string | null;
  permissionSteps: string[];
};

export function PermissionsCard({
  captureEventsDropped,
  captureFailed,
  captureFailureReason,
  captureProbeConfirmed,
  captureRunning,
  captureStalled,
  onRefreshPermissions,
  onRequestPermissions,
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
      {captureStalled ? (
        <p className="helper-text alert">
          Capture is running but hasn't received any input events — the listener may be blocked.
          Try Refresh permissions, or restart Snapback.
        </p>
      ) : null}
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
      <div className="button-row">
        {/* Setup steps are only populated when the OS hasn't granted access, so this is
            exactly when there is something to prompt for. Once granted, the button would
            be a no-op that re-opens nothing — macOS only shows its dialog once. */}
        {permissionSteps.length > 0 ? (
          <button className="primary-button" onClick={() => void onRequestPermissions()}>
            Grant access
          </button>
        ) : null}
        <button className="secondary-button" onClick={() => void onRefreshPermissions()}>
          Refresh permissions
        </button>
      </div>
    </section>
  );
}
