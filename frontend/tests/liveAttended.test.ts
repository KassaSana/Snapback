import assert from "node:assert/strict";

import { formatLiveTodayMins, liveAttendedMins } from "../src/liveAttended";

const t0 = 1_000_000;

assert.equal(liveAttendedMins(5, t0, t0 + 59_999, true), 5);
assert.equal(liveAttendedMins(5, t0, t0 + 60_000, true), 6);
assert.equal(liveAttendedMins(5, t0, t0 + 120_000, true), 7);
// Frozen while idle/private: elapsed wall time does not invent attendance.
assert.equal(liveAttendedMins(5, t0, t0 + 120_000, false), 5);

assert.equal(formatLiveTodayMins(0, true), "<1m");
assert.equal(formatLiveTodayMins(0, false), "0m");
assert.equal(formatLiveTodayMins(1, true), "1m");
assert.equal(formatLiveTodayMins(95, false), "1h 35m");

console.log("liveAttended.test.ts passed");
