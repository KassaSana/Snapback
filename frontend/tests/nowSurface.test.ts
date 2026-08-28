import assert from "node:assert/strict";

import { nowSurfaceMode } from "../src/nowSurface";

// A live session is the running cockpit, even if a previous recap is still in memory.
assert.equal(nowSurfaceMode({ sessionActive: true, recap: { goal: "x" } }), "running");
assert.equal(nowSurfaceMode({ sessionActive: true, recap: null }), "running");

// Stop's payoff: recap on Now, no live session.
assert.equal(nowSurfaceMode({ sessionActive: false, recap: { goal: "x" } }), "stopped");

// Cold start, or after the recap has been cleared by a later start.
assert.equal(nowSurfaceMode({ sessionActive: false, recap: null }), "idle");

console.log("nowSurface.test.ts passed");
