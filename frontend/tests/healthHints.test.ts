import assert from "node:assert/strict";

import { summarizePermissions } from "../src/healthHints";

assert.deepEqual(
  summarizePermissions({
    captureAvailable: true,
    activeWindowAvailable: true,
    message: "OK",
    setupSteps: [],
  }),
  {
    label: "ready",
    detail: "capture + active window access",
  },
);

assert.deepEqual(
  summarizePermissions({
    captureAvailable: true,
    activeWindowAvailable: false,
    message: "Partial",
    setupSteps: [],
  }),
  {
    label: "partial",
    detail: "capture access only",
  },
);

assert.deepEqual(
  summarizePermissions({
    captureAvailable: false,
    activeWindowAvailable: true,
    message: "Partial",
    setupSteps: [],
  }),
  {
    label: "partial",
    detail: "active window access only",
  },
);

assert.deepEqual(
  summarizePermissions({
    captureAvailable: false,
    activeWindowAvailable: false,
    message: "Blocked",
    setupSteps: [],
  }),
  {
    label: "blocked",
    detail: "permissions required",
  },
);

console.log("healthHints.test.ts passed");
