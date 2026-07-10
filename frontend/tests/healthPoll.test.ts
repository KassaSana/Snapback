import assert from "node:assert/strict";

import { HEALTH_POLL_MS, shouldPollHealth } from "../src/healthPoll";

// Poll while capture isn't confirmed up (so we notice permissions granted later).
assert.equal(shouldPollHealth(false), true);

// Stop once capture is running — push events carry state from here.
assert.equal(shouldPollHealth(true), false);

// Interval must be a sane positive cadence.
assert.equal(HEALTH_POLL_MS > 0, true);

console.log("healthPoll.test.ts passed");
