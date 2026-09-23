import { memo, useEffect, useRef, useState } from "react";

import {
  captureIsReady,
  readFirstRunAck,
  shouldShowPermissionWizard,
  writeFirstRunAck,
} from "./permissionWizardState";
import { FOCUS_MODE_LABELS, FOCUS_MODES, type FocusMode } from "./sessionCockpit";

type PermissionWizardProps = {
  healthChecked: boolean;
  captureProbeConfirmed: boolean;
  captureRunning: boolean;
  permissionMessage: string | null;
  permissionSteps: string[];
  onRefreshPermissions: () => void;
  onRequestPermissions: () => void;
  focusMode: FocusMode;
  onFocusModeChange: (mode: FocusMode) => void;
};

export const PermissionWizard = memo(function PermissionWizard({
  healthChecked,
  captureProbeConfirmed,
  captureRunning,
  permissionMessage,
  permissionSteps,
  onRefreshPermissions,
  onRequestPermissions,
  focusMode,
  onFocusModeChange,
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
  const backdropRef = useRef<HTMLDivElement>(null);
  const primaryRef = useRef<HTMLButtonElement>(null);
  const checkAgainRef = useRef<HTMLButtonElement>(null);

  // Roadmap 10.3. `aria-modal` promises four things and none were implemented: focus moves in
  // on open, the page behind stops taking input, Tab stays inside, and focus goes back on
  // close. The first two and the last live here; Tab is `keepTabInside` below.
  //
  // Escape is deliberately not bound. The only way out is "Skip for now", which records the
  // first-run acknowledgement permanently, and a stray Escape should not end onboarding for
  // good.
  useEffect(() => {
    if (!visible) return;
    const opener = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const backdrop = backdropRef.current;
    const background = backdrop?.parentElement
      ? Array.from(backdrop.parentElement.children).filter(
          (element): element is HTMLElement =>
            element !== backdrop && element instanceof HTMLElement && !element.inert,
        )
      : [];
    for (const element of background) element.inert = true;
    (primaryRef.current ?? checkAgainRef.current)?.focus();
    return () => {
      for (const element of background) element.inert = false;
      if (opener && opener !== document.body && opener.isConnected) opener.focus();
    };
  }, [visible]);

  if (!visible) {
    return null;
  }

  // `inert` already keeps Tab out of the page in a real webview. This wrap is the part a test
  // can see, and the fallback wherever `inert` is not honoured.
  const keepTabInside = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (event.key !== "Tab") return;
    const focusable = Array.from(
      event.currentTarget.querySelectorAll<HTMLElement>(
        'button:not([disabled]), select:not([disabled]), input:not([disabled]), [href], [tabindex]:not([tabindex="-1"])',
      ),
    );
    if (focusable.length === 0) return;
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  };

  const dismiss = () => {
    writeFirstRunAck();
    setAcknowledged(true);
  };

  return (
    <div
      ref={backdropRef}
      className="wizard-backdrop"
      role="dialog"
      aria-modal="true"
      aria-labelledby="wizard-title"
      onKeyDown={keepTabInside}
    >
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
        <label className="field">
          <span>Default focus mode</span>
          <select
            value={focusMode}
            onChange={(event) => onFocusModeChange(event.target.value as FocusMode)}
          >
            {FOCUS_MODES.map((mode) => (
              <option key={mode} value={mode}>
                {FOCUS_MODE_LABELS[mode]}
              </option>
            ))}
          </select>
        </label>
        <div className="wizard-actions">
          {/* The whole point of onboarding: actually raise the OS dialog rather than
              telling the user to go find it in System Settings. Only shown while access
              is still missing — once granted there is nothing left to ask for. */}
          {permissionSteps.length > 0 ? (
            <button
              ref={primaryRef}
              className="primary-button"
              onClick={() => void onRequestPermissions()}
            >
              Grant access
            </button>
          ) : null}
          <button
            ref={checkAgainRef}
            className="secondary-button"
            onClick={() => void onRefreshPermissions()}
          >
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
});
