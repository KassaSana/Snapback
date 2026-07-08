import assert from "node:assert/strict";

import {
  buildSignals,
  clamp,
  focusStateLabel,
  formatPercent,
  formatScore,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
} from "../src/utils";

assert.equal(clamp(1.5, 0, 1), 1);
assert.equal(clamp(-1, 0, 1), 0);

assert.equal(formatPercent(0.5), "50.0%");
assert.equal(formatScore(105), "100.0");

assert.equal(riskLevel(0.8), "high");
assert.equal(riskLevel(0.5), "medium");
assert.equal(riskLevel(0.1), "low");

assert.equal(riskLabel(0.8), "High risk");
assert.equal(riskLabel(0.5), "Medium risk");
assert.equal(riskLabel(0.1), "Low risk");

assert.equal(focusStateLabel("DEEP_FOCUS"), "Deep work");
assert.equal(focusStateLabel("PSEUDO_PRODUCTIVE"), "Drift");
assert.equal(focusStateLabel(null), "Unknown");

assert.equal(nextBackoffDelay(0), 500);
assert.equal(nextBackoffDelay(1), 1000);
assert.equal(nextBackoffDelay(4), 8000);
assert.equal(nextBackoffDelay(6), 10000);

const waiting = buildSignals(null);
assert.equal(waiting.length, 1);
assert.ok(waiting[0].includes("Waiting"));

const base = {
  sessionId: "s1",
  focusScore: 80,
  distractionRisk: 0.2,
  focusState: "PRODUCTIVE",
  thrashScore: 0.05,
  driftScore: 0.1,
  goalAlignment: 0.9,
  timestamp: "2026-07-08T00:00:00Z",
};
const sigs = buildSignals(base as any);
assert.ok(sigs.some((s) => s.includes("Focus state")));
assert.ok(sigs.some((s) => s.includes("Focus score")));

const pseudo = { ...base, focusState: "PSEUDO_PRODUCTIVE" };
const p = buildSignals(pseudo as any);
assert.ok(p.some((s) => s.includes("Drift detected")));

const thrashy = { ...base, thrashScore: 0.7 };
const t = buildSignals(thrashy as any);
assert.ok(t.some((s) => s.includes("thrash")));

const deep = { ...base, focusState: "DEEP_FOCUS" };
const d = buildSignals(deep as any);
assert.ok(d.some((s) => s.includes("Deep work")));

const low = { ...base, distractionRisk: 0.1 };
const l = buildSignals(low as any);
assert.ok(l.some((s) => s.includes("Keep momentum")));

console.log("utils.test.ts passed");
