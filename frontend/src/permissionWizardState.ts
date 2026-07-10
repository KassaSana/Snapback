// First-run permission wizard: the decision logic, kept pure and separate from
// React/DOM so it can be unit-tested headlessly.

export const FIRST_RUN_ACK_KEY = "snapback.firstRunPermissionsAcknowledged";

export type PermissionWizardInput = {
  /** true once health has been fetched at least once (avoids a launch flash). */
  healthChecked: boolean;
  /** true once the user has gotten capture working or dismissed the wizard. */
  firstRunAcknowledged: boolean;
  /** true when capture is confirmed usable (listener running or probe passed). */
  captureReady: boolean;
};

/**
 * Whether to show the first-run wizard. It guides a brand-new user through
 * granting capture permissions and then gets out of the way for good: once the
 * user has capture working or has acknowledged it, the header + PermissionsCard
 * own any later permission problems, so the wizard never returns.
 */
export const shouldShowPermissionWizard = ({
  healthChecked,
  firstRunAcknowledged,
  captureReady,
}: PermissionWizardInput): boolean => {
  if (!healthChecked) return false;
  if (firstRunAcknowledged) return false;
  if (captureReady) return false;
  return true;
};

/** Capture is usable if the listener is running or the probe confirmed access. */
export const captureIsReady = (
  captureRunning: boolean,
  captureProbeConfirmed: boolean,
): boolean => captureRunning || captureProbeConfirmed;

type StorageLike = Pick<Storage, "getItem" | "setItem">;

const defaultStorage = (): StorageLike | null => {
  try {
    return globalThis.localStorage ?? null;
  } catch {
    // Accessing localStorage can throw (disabled storage, sandboxed frame).
    return null;
  }
};

/** Read the persisted "first run handled" flag; false if storage is unavailable. */
export const readFirstRunAck = (storage: StorageLike | null = defaultStorage()): boolean => {
  if (!storage) return false;
  try {
    return storage.getItem(FIRST_RUN_ACK_KEY) === "true";
  } catch {
    return false;
  }
};

/** Persist that the first run is handled so the wizard won't show again. */
export const writeFirstRunAck = (storage: StorageLike | null = defaultStorage()): void => {
  if (!storage) return;
  try {
    storage.setItem(FIRST_RUN_ACK_KEY, "true");
  } catch {
    // Ignore quota / disabled-storage errors: worst case the wizard reappears.
  }
};
