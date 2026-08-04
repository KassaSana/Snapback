import assert from "node:assert/strict";

import {
  buildSignals,
  clamp,
  explainPrediction,
  focusStateLabel,
  formatPercent,
  formatPercentCoarse,
  formatPomodoroRemaining,
  formatScore,
  formatScoreCoarse,
  formatTime,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
  verdictLevel,
} from "../src/utils";

assert.equal(clamp(1.5, 0, 1), 1);
assert.equal(clamp(-1, 0, 1), 0);

assert.equal(formatPercent(0.5), "50.0%");
assert.equal(formatScore(105), "100.0");

// Coarse variants back the Now surface (ADR-0003): whole numbers only, because a decimal
// place claims precision the score's undecided scale does not have.
assert.equal(formatScoreCoarse(71.2), "71");
assert.equal(formatScoreCoarse(71.6), "72");
assert.equal(formatScoreCoarse(105), "100");
assert.equal(formatScoreCoarse(null), "--");
assert.equal(formatPercentCoarse(0.214), "21%");
assert.equal(formatPercentCoarse(1.4), "100%");
assert.equal(formatPercentCoarse(undefined), "--");

// Defensive branches: malformed backend values must degrade to "--"
// rather than rendering "NaN%" or "Invalid Date" in the UI.
assert.equal(formatPercent(null), "--");
assert.equal(formatPercent(undefined), "--");
assert.equal(formatPercent(NaN), "--");
assert.equal(formatScore(null), "--");
assert.equal(formatScore(undefined), "--");
assert.equal(formatScore(NaN), "--");

// formatTime: assert the deterministic guard contract only. We do NOT
// assert the formatted time for a valid date, because toLocaleTimeString
// is timezone/locale-dependent and would make this test flaky in CI.
assert.equal(formatTime(null), "--");
assert.equal(formatTime(undefined), "--");
assert.equal(formatTime(""), "--");
assert.equal(formatTime("not-a-date"), "--");
assert.notEqual(formatTime("2026-07-08T00:00:00Z"), "--");

assert.equal(riskLevel(0.8), "high");
assert.equal(riskLevel(0.5), "medium");
assert.equal(riskLevel(0.1), "low");

// Boundary values: riskLevel flips at exactly 0.7 and 0.4 (>=), and NaN
// must fall through to "unknown". Bugs hide on the boundary, so pin it.
assert.equal(riskLevel(0.7), "high");
assert.equal(riskLevel(0.4), "medium");
assert.equal(riskLevel(NaN), "unknown");
assert.equal(riskLevel(null), "unknown");

// verdictLevel colours by the policy verdict (ADR-0004), one class per channel: the hero's
// word and dot must agree with each other even when the verdict disagrees with the risk.
assert.equal(verdictLevel("DISTRACTED"), "high");
assert.equal(verdictLevel("PSEUDO_PRODUCTIVE"), "medium");
assert.equal(verdictLevel("PRODUCTIVE"), "low");
assert.equal(verdictLevel("DEEP_FOCUS"), "low");
assert.equal(verdictLevel("SOMETHING_ELSE"), "unknown");
assert.equal(verdictLevel(null), "unknown");

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

assert.equal(formatPomodoroRemaining(0), "0:00");
assert.equal(formatPomodoroRemaining(59_000), "0:59");
assert.equal(formatPomodoroRemaining(60_000), "1:00");
assert.equal(formatPomodoroRemaining(90_500), "1:31"); // rounds to the nearest second
assert.equal(formatPomodoroRemaining(-500), "0:00"); // clamps negative drift to zero

console.log("utils.test.ts passed");

// explainPrediction names what the classifier measured, and admits what it cannot see.
const quietProductive = {
  focusState: "PRODUCTIVE",
  thrashScore: 0.05,
  driftScore: 0.02,
  goalAlignment: 0.5,
} as any;
const quiet = explainPrediction(quietProductive);
assert.deepEqual(quiet.reasons, ["no app switching", "settled in one window"]);
assert.ok(quiet.caveat, "a quiet screen with no goal signal must carry the caveat");

// A corroborating goal removes the caveat: the verdict no longer rests on absence alone.
const withGoal = explainPrediction({ ...quietProductive, goalAlignment: 0.9 }, "ship the overlay");
assert.equal(withGoal.caveat, null);
assert.ok(withGoal.reasons.some((r) => r.includes("matches")));

// A neutral goalAlignment (0.5 = nothing matched) must not be reported as a match.
assert.ok(!explainPrediction(quietProductive, "ship the overlay").reasons.some((r) => r.includes("matches")));

// Negative verdicts get no caveat — the caveat is about unearned confidence, not noise.
assert.equal(
  explainPrediction({ ...quietProductive, focusState: "DISTRACTED" } as any).caveat,
  null,
);

const busy = explainPrediction({ ...quietProductive, thrashScore: 0.8, driftScore: 0.7 } as any);
assert.deepEqual(busy.reasons, ["switching apps often", "tab and title churn"]);

// A policy override is the headline evidence, and the calm low-signal phrases are
// suppressed with it — "Distracted because no app switching" was a contradiction (ADR-0004).
const blocked = explainPrediction({
  ...quietProductive,
  focusState: "DISTRACTED",
  stateSource: "block",
} as any);
assert.deepEqual(blocked.reasons, ["a blocked app is open"]);

const overRisk = explainPrediction({
  ...quietProductive,
  focusState: "DISTRACTED",
  stateSource: "risk",
} as any);
assert.deepEqual(overRisk.reasons, ["distraction risk over the mode's bar"]);

// Rows from before verdicts carried provenance (stateSource null) read exactly as before.
const legacy = explainPrediction({ ...quietProductive, stateSource: null } as any);
assert.deepEqual(legacy.reasons, ["no app switching", "settled in one window"]);

assert.deepEqual(explainPrediction(null), { reasons: [], caveat: null });
