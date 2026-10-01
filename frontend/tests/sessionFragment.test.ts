import assert from "node:assert/strict";

import { isSessionFragment } from "../src/sessionFragment";

assert.equal(isSessionFragment({ durationSecs: 30, sampleCount: 0 }), true);
assert.equal(isSessionFragment({ durationSecs: 59, sampleCount: null }), true);
assert.equal(isSessionFragment({ durationSecs: 60, sampleCount: 0 }), false);
assert.equal(isSessionFragment({ durationSecs: 30, sampleCount: 1 }), false);
assert.equal(isSessionFragment({ durationSecs: 0, sampleCount: 0 }), true);

console.log("sessionFragment.test.ts passed");
