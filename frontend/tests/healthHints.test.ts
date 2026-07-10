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

// summarizeAppHealth: the "online" default (nothing wrong) and the explicit
// "capture_failed" status string were both uncovered.
assert.equal(
  summarizeAppHealth({ status: "online", captureFailed: false }),
  "online",
);
assert.equal(
  summarizeAppHealth({ status: "capture_failed", captureFailed: false }),
  "offline",
);

// summarizePermissions: the remaining decision-tree branches, each of which
// is a distinct status the user can see in the header.

// Listener running but no window access -> partial "listener running only".
assert.deepEqual(
  summarizePermissions({
    captureAvailable: false,
    captureProbeConfirmed: false,
    captureFailed: false,
    captureRunning: true,
    activeWindowAvailable: false,
    message: "Partial",
    setupSteps: [],
  }),
  { label: "partial", detail: "listener running only" },
);

// Capture available but unconfirmed and no window access -> "capture unverified".
assert.deepEqual(
  summarizePermissions({
    captureAvailable: true,
    captureProbeConfirmed: false,
    captureFailed: false,
    captureRunning: false,
    activeWindowAvailable: false,
    message: "Partial",
    setupSteps: [],
  }),
  { label: "partial", detail: "capture unverified" },
);

// Only active-window access -> partial "active window access only".
assert.deepEqual(
  summarizePermissions({
    captureAvailable: false,
    captureProbeConfirmed: false,
    captureFailed: false,
    captureRunning: false,
    activeWindowAvailable: true,
    message: "Partial",
    setupSteps: [],
  }),
  { label: "partial", detail: "active window access only" },
);

// Nothing available -> blocked "permissions required" (the fallback branch).
assert.deepEqual(
  summarizePermissions({
    captureAvailable: false,
    captureProbeConfirmed: false,
    captureFailed: false,
    captureRunning: false,
    activeWindowAvailable: false,
    message: "Blocked",
    setupSteps: [],
  }),
  { label: "blocked", detail: "permissions required" },
);

console.log("healthHints.test.ts passed");
