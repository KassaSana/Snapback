import assert from "node:assert/strict";

import {
  FIRST_RUN_ACK_KEY,
  captureIsReady,
  readFirstRunAck,
  shouldShowPermissionWizard,
  writeFirstRunAck,
} from "../src/permissionWizardState";

// --- shouldShowPermissionWizard ---

// Brand-new user, health loaded, capture not ready: show it.
assert.equal(
  shouldShowPermissionWizard({
    healthChecked: true,
    firstRunAcknowledged: false,
    captureReady: false,
  }),
  true,
);

// Don't flash before the first health fetch resolves.
assert.equal(
  shouldShowPermissionWizard({
    healthChecked: false,
    firstRunAcknowledged: false,
    captureReady: false,
  }),
  false,
);

// Already acknowledged: never show again, even if capture is broken.
assert.equal(
  shouldShowPermissionWizard({
    healthChecked: true,
    firstRunAcknowledged: true,
    captureReady: false,
  }),
  false,
);

// Capture already works: nothing to guide.
assert.equal(
  shouldShowPermissionWizard({
    healthChecked: true,
    firstRunAcknowledged: false,
    captureReady: true,
  }),
  false,
);

// --- captureIsReady ---

assert.equal(captureIsReady(true, false), true); // listener running
assert.equal(captureIsReady(false, true), true); // probe confirmed
assert.equal(captureIsReady(false, false), false); // neither

// --- storage helpers (fake Storage) ---

const makeStorage = () => {
  const map = new Map<string, string>();
  return {
    getItem: (key: string) => map.get(key) ?? null,
    setItem: (key: string, value: string) => void map.set(key, value),
  };
};

const storage = makeStorage();
assert.equal(readFirstRunAck(storage), false);
writeFirstRunAck(storage);
assert.equal(storage.getItem(FIRST_RUN_ACK_KEY), "true");
assert.equal(readFirstRunAck(storage), true);

// Missing storage must never throw and reads as "not acknowledged".
assert.equal(readFirstRunAck(null), false);
assert.doesNotThrow(() => writeFirstRunAck(null));

console.log("permissionWizardState.test.ts passed");
