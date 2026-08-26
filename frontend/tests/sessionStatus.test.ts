import assert from "node:assert/strict";

import { sessionStatusLabel } from "../src/sessionStatus";
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
assert.equal(sessionStatusLabel(null, false), "no session");
assert.equal(sessionStatusLabel(null, true), "no session");

// The distinction 7.23 exists for: a live session that is not accruing attended time says so.
assert.equal(sessionStatusLabel(session("ACTIVE"), false), "running");
assert.equal(sessionStatusLabel(session("ACTIVE"), true), "paused");

// A finished session cannot be "paused" — idle state is irrelevant once it has stopped, and
// reporting otherwise would make a completed session look resumable.
assert.equal(sessionStatusLabel(session("COMPLETED"), true), "completed");
assert.equal(sessionStatusLabel(session("COMPLETED"), false), "completed");

console.log("sessionStatus.test.ts passed");
