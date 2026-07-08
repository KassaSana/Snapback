import assert from "node:assert/strict";

import { summarizePermissions } from "../src/healthHints";

assert.deepEqual(
  summarizePermissions({
    captureAvailable: true,
    captureProbeConfirmed: false,
    captureFailed: false,
    captureRunning: false,
    activeWindowAvailable: true,
    message: "OK",
    setupSteps: [],
  }),
  {
    label: "partial",
    detail: "probe only until listener starts",
  },
);

assert.deepEqual(
  summarizePermissions({
    captureAvailable: true,
    captureProbeConfirmed: true,
    captureFailed: false,
    captureRunning: false,
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
    captureProbeConfirmed: false,
    captureFailed: false,
    captureRunning: true,
    activeWindowAvailable: true,
    message: "Partial",
    setupSteps: [],
  }),
  {
    label: "ready",
    detail: "listener running + window access",
  },
);

assert.deepEqual(
  summarizePermissions({
    captureAvailable: false,
    captureProbeConfirmed: false,
    captureFailed: true,
    captureRunning: false,
    activeWindowAvailable: false,
    message: "Blocked",
    setupSteps: [],
  }),
  {
    label: "blocked",
    detail: "capture listener failed",
  },
);

console.log("healthHints.test.ts passed");
