import assert from "node:assert/strict";

import type { PredictionRecord } from "../src/api";
import { mergeReadings, ribbonKind, ribbonSummary } from "../src/ribbonSegments";

const reading = (over: Partial<PredictionRecord>): PredictionRecord => ({
  sessionId: "s1",
  focusScore: 0.8,
  distractionRisk: 0.1,
  focusState: "DEEP_FOCUS",
  thrashScore: 0.5,
  driftScore: 0.5,
  goalAlignment: 0.9,
  timestampMs: 1_000,
  modelId: "m",
  stateSource: "model",
  ...over,
});

// Kinds follow the verdict, including the display-only "Not sure" rule.
assert.equal(ribbonKind(reading({}), "auth.ts"), "deep");
assert.equal(ribbonKind(reading({ focusState: "PRODUCTIVE" }), "auth.ts"), "productive");
assert.equal(ribbonKind(reading({ focusState: "DISTRACTED" }), "auth.ts"), "drift");
assert.equal(ribbonKind(reading({ focusState: "PSEUDO_PRODUCTIVE" }), "auth.ts"), "drift");
assert.equal(ribbonKind(reading({ focusState: "UNKNOWN" }), "auth.ts"), "unknown");
// A quiet screen with no goal signal is a guess from absence, not deep work.
assert.equal(
  ribbonKind(reading({ thrashScore: 0, driftScore: 0, goalAlignment: 0.5 }), "auth.ts"),
  "unknown",
);

// Merge keeps this session only, dedupes by timestamp, and sorts oldest first.
const a = reading({ timestampMs: 3_000 });
const b = reading({ timestampMs: 1_000 });
const other = reading({ sessionId: "s0", timestampMs: 2_000 });
const merged = mergeReadings([], [a, b, other, null, a], "s1");
assert.deepEqual(
  merged.map((r) => r.timestampMs),
  [1_000, 3_000],
);

// Nothing new returns the same array, so React skips the re-render.
assert.equal(mergeReadings(merged, [a, b], "s1"), merged);

// A corrected reading at the same timestamp replaces the original.
const corrected = reading({ timestampMs: 3_000, focusState: "DISTRACTED" });
assert.equal(mergeReadings(merged, [corrected], "s1")[1], corrected);

// A new session drops the old one's readings.
assert.deepEqual(mergeReadings(merged, [], "s2"), []);
assert.deepEqual(mergeReadings(merged, [a], null), []);

// The cap keeps the most recent readings.
const many = Array.from({ length: 10 }, (_, i) => reading({ timestampMs: i }));
assert.deepEqual(
  mergeReadings([], many, "s1", 4).map((r) => r.timestampMs),
  [6, 7, 8, 9],
);

// The accessible summary says what the colours say.
assert.equal(ribbonSummary([]), "No recent readings for this session");
assert.equal(ribbonSummary(["deep"]), "1 recent reading for this session: deep work 1");
assert.equal(
  ribbonSummary(["deep", "drift", "deep", "unknown"]),
  "4 recent readings for this session: deep work 2, drifting 1, not sure 1",
);

console.log("ribbonSegments tests passed");
