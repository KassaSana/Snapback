import assert from "node:assert/strict";

import { sessionStatusLabel, sessionStatusView } from "../src/sessionStatus";
import type { SessionRecord } from "../src/api";

const session = (status: string): SessionRecord => ({
  sessionId: "s1",
  goal: "ship it",
  status,
  focusMode: "normal",
  reflectionDone: null,
  reflectionNextStep: null,
  startedAtMs: null,
  endedAtMs: null,
});

// No session and "the user is away" are independent states, so the empty case must not
// borrow the word "idle" from the other one.
assert.equal(sessionStatusLabel(null, false), "No session");
assert.equal(sessionStatusLabel(null, true), "No session");

// A live session that is not accruing attended time says so.
assert.equal(sessionStatusLabel(session("ACTIVE"), false), "Running");
assert.equal(sessionStatusLabel(session("ACTIVE"), true), "Paused (no input)");
assert.match(sessionStatusView(session("ACTIVE"), true).reason ?? "", /Not counting toward attended time/);

// A finished session cannot be "paused" — idle state is irrelevant once it has stopped, and
// reporting otherwise would make a completed session look resumable.
assert.equal(sessionStatusLabel(session("COMPLETED"), true), "Completed");
assert.equal(sessionStatusLabel(session("COMPLETED"), false), "Completed");

assert.equal(sessionStatusLabel(session("ACTIVE"), false, "pausedPrivate"), "Paused (private)");
assert.equal(sessionStatusLabel(session("ACTIVE"), false, "pausedIdle"), "Paused (away)");
assert.equal(sessionStatusView(session("ACTIVE"), false, "pausedPrivate").reason,
  "Not counting toward attended time.");
assert.equal(sessionStatusLabel(session("COMPLETED"), false, "pausedPrivate"), "Completed");

console.log("sessionStatus.test.ts passed");
