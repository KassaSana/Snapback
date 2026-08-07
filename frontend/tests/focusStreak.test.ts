import assert from "node:assert/strict";

import {
  FOCUS_STRETCH_LABEL,
  PRODUCTIVE_SESSIONS_LABEL,
  focusStretchHelperText,
  formatFocusStretch,
  productiveSessionsHelperText,
} from "../src/focusStreak";

// THE RULE. Three nearly identical labels sat over three incompatible quantities, two of them
// prediction-row counts wearing time-like copy. Whatever else changes, no label may say
// "streak" over a row count.
assert.ok(!FOCUS_STRETCH_LABEL.toLowerCase().includes("streak"));
assert.ok(!PRODUCTIVE_SESSIONS_LABEL.toLowerCase().includes("streak"));
// And the session metric must name its unit in the label itself, not only in a footnote.
assert.ok(PRODUCTIVE_SESSIONS_LABEL.toLowerCase().includes("session"));

// Durations read in the largest unit that does not round the answer away. A 45-second stretch
// shown as "0m" is the same failure in a new costume.
assert.equal(formatFocusStretch(0), "0s");
assert.equal(formatFocusStretch(45), "45s");
assert.equal(formatFocusStretch(59), "59s");
assert.equal(formatFocusStretch(60), "1m");
assert.equal(formatFocusStretch(1500), "25m");
assert.equal(formatFocusStretch(3599), "59m");
assert.equal(formatFocusStretch(3600), "1h");
assert.equal(formatFocusStretch(3660), "1h 1m");
assert.equal(formatFocusStretch(7845), "2h 10m");

// Junk and negatives cannot become "NaNs" in a tile.
assert.equal(formatFocusStretch(-30), "0s");
assert.equal(formatFocusStretch(Number.NaN), "0s");
assert.equal(formatFocusStretch(90.7), "1m");

// The helper text states the unit *and* the break rule. A duration with no stated boundary
// invites the reader to assume it means "time in the app", which it does not.
{
  const text = focusStretchHelperText(1500);
  assert.ok(text.includes("25m"));
  assert.ok(text.includes("unbroken"));
  assert.ok(text.includes("distraction"));
  assert.ok(text.includes("break in recording"));
  assert.ok(text.includes("end of a session"));
}

// Zero is a real state, not a missing one, and says so without a number.
{
  const text = focusStretchHelperText(0);
  assert.ok(text.includes("No unbroken focused stretch"));
  assert.ok(text.includes("distraction"));
}

// The session metric says "sessions, not time" outright, because it sat under the same words
// as the duration on another card for the whole life of the feature.
{
  const text = productiveSessionsHelperText(4);
  assert.ok(text.includes("4 completed sessions"));
  assert.ok(text.includes("70"));
  assert.ok(text.includes("counts sessions, not time"));
  assert.ok(productiveSessionsHelperText(1).includes("1 completed session in a row"));
  assert.ok(productiveSessionsHelperText(0).includes("No completed sessions"));
}

console.log("focusStreak.test.ts passed");
