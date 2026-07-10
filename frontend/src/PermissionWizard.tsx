import { useEffect, useState } from "react";

import {
  captureIsReady,
  readFirstRunAck,
  shouldShowPermissionWizard,
  writeFirstRunAck,
} from "./permissionWizardState";

type PermissionWizardProps = {
  healthChecked: boolean;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  permissionMessage: string | null;
  permissionSteps: string[];
  onRefreshPermissions: () => void;
};

export function PermissionWizard({
  healthChecked,
  captureProbeConfirmed,
  captureRunning,
  permissionMessage,
  permissionSteps,
  onRefreshPermissions,
}: PermissionWizardProps) {
  const [acknowledged, setAcknowledged] = useState(() => readFirstRunAck());
  const ready = captureIsReady(captureRunning, captureProbeConfirmed);

  // Once capture works, remember it so the wizard never returns on later launches.
  useEffect(() => {
    if (ready && !acknowledged) {
      writeFirstRunAck();
      setAcknowledged(true);
    }
  }, [ready, acknowledged]);

  const visible = shouldShowPermissionWizard({
    healthChecked,
    firstRunAcknowledged: acknowledged,
    captureReady: ready,
  });
  if (!visible) {
    return null;
  }

  const dismiss = () => {
    writeFirstRunAck();
    setAcknowledged(true);
  };

  return (
    <div className="wizard-backdrop" role="dialog" aria-modal="true" aria-labelledby="wizard-title">
      <div className="wizard card">
        <h2 id="wizard-title">Welcome to Snapback</h2>
        <p className="helper-text">
          Snapback watches your activity locally to tell focus from distraction. Before it can
          track a session it needs permission to see input and the active window.
        </p>
        <p className="helper-text">
          {permissionMessage ||
            "Grant Accessibility + Input Monitoring (macOS), then check again."}
        </p>
        {permissionSteps.length > 0 ? (
          <ol className="permission-steps">
            {permissionSteps.map((step) => (
              <li key={step}>{step}</li>
            ))}
          </ol>
        ) : null}
        <div className="wizard-actions">
          <button className="primary-button" onClick={() => void onRefreshPermissions()}>
            Check again
          </button>
          <button className="secondary-button" onClick={dismiss}>
            Skip for now
          </button>
        </div>
        <p className="helper-text wizard-footnote">
          You can grant permissions later from the Permissions card.
        </p>
      </div>
    </div>
  );
}
