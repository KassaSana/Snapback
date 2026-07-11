import assert from "node:assert/strict";

import type { SessionSummary } from "../src/api";
import {
  computeInsightsAggregates,
  focusBarHeightPct,
  toChronological,
} from "../src/insightsMetrics";

const summary = (
  id: string,
  focus: number,
  deep: number,
  snap: number,
): SessionSummary => ({
  record: {
    sessionId: id,
    goal: "g",
    status: "COMPLETED",
    focusMode: "normal",
    startedAt: null,
    endedAt: null,
  },
  recap: {
    sessionId: id,
    goal: "g",
    durationSecs: 0,
    avgFocusScore: focus,
    avgDistractionRisk: 0,
    snapbackCount: snap,
    thrashSpikes: 0,
    deepFocusPct: deep,
  },
});

// Empty → all zeros (no division by zero).
const empty = computeInsightsAggregates([]);
assert.equal(empty.sessionCount, 0);
assert.equal(empty.avgFocusScore, 0);
assert.equal(empty.avgDeepFocusPct, 0);
assert.equal(empty.totalSnapbacks, 0);

// Averages the rates, sums the counts.
const agg = computeInsightsAggregates([summary("a", 60, 40, 2), summary("b", 80, 20, 3)]);
assert.equal(agg.sessionCount, 2);
assert.equal(agg.avgFocusScore, 70);
assert.equal(agg.avgDeepFocusPct, 30);
assert.equal(agg.totalSnapbacks, 5);

// Reverses newest-first into oldest→newest, without mutating the input.
const list = [summary("a", 1, 0, 0), summary("b", 2, 0, 0)];
const chrono = toChronological(list);
assert.equal(chrono[0].record.sessionId, "b");
assert.equal(list[0].record.sessionId, "a");

// Bar height clamps to the 0–100 domain.
assert.equal(focusBarHeightPct(150), 100);
assert.equal(focusBarHeightPct(-5), 0);
assert.equal(focusBarHeightPct(42), 42);

console.log("insightsMetrics.test.ts passed");
