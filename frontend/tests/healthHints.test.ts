import assert from "node:assert/strict";

import { summarizeAppHealth, summarizePermissions } from "../src/healthHints";

assert.equal(
  summarizeAppHealth({
    status: "offline",
    captureFailed: false,
  }),
  "offline",
);

assert.equal(
  summarizeAppHealth({
    status: "degraded",
    captureFailed: false,
  }),
  "degraded",
);

assert.equal(
  summarizeAppHealth({
    status: "online",
    captureFailed: true,
  }),
  "offline",
);

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
    detail: "listener not confirmed yet",
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
    detail: "permissions ready, listener idle",
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
