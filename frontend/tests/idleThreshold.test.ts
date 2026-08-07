import assert from "node:assert/strict";

import {
  DEFAULT_IDLE_THRESHOLD_SECS,
  MAX_IDLE_THRESHOLD_SECS,
  MIN_IDLE_THRESHOLD_SECS,
  formatIdleThreshold,
  idleThresholdHelperText,
  normalizeIdleThresholdSecs,
} from "../src/idleThreshold";
import { mapSettings } from "../src/apiMappers";

// A value inside the bounds is kept exactly as sent.
assert.equal(normalizeIdleThresholdSecs(60), 60);
assert.equal(normalizeIdleThresholdSecs(MIN_IDLE_THRESHOLD_SECS), MIN_IDLE_THRESHOLD_SECS);
assert.equal(normalizeIdleThresholdSecs(MAX_IDLE_THRESHOLD_SECS), MAX_IDLE_THRESHOLD_SECS);

// Out of range falls back to the default rather than clamping to the nearest bound. A clamped
// value is indistinguishable from one the user chose; the default at least matches what the
// native side will actually be running, since types.cpp rejects the same values.
assert.equal(normalizeIdleThresholdSecs(1), DEFAULT_IDLE_THRESHOLD_SECS);
assert.equal(normalizeIdleThresholdSecs(99999), DEFAULT_IDLE_THRESHOLD_SECS);
assert.equal(normalizeIdleThresholdSecs(-300), DEFAULT_IDLE_THRESHOLD_SECS);

// Junk from an older or hand-edited settings file must not become NaN in a <select value>.
assert.equal(normalizeIdleThresholdSecs(undefined), DEFAULT_IDLE_THRESHOLD_SECS);
assert.equal(normalizeIdleThresholdSecs(null), DEFAULT_IDLE_THRESHOLD_SECS);
assert.equal(normalizeIdleThresholdSecs("not a number"), DEFAULT_IDLE_THRESHOLD_SECS);
assert.equal(normalizeIdleThresholdSecs("120"), 120);
assert.equal(normalizeIdleThresholdSecs(120.9), 120);

// Units are chosen so the label never asks the reader to divide.
assert.equal(formatIdleThreshold(30), "30 seconds");
assert.equal(formatIdleThreshold(60), "1 minute");
assert.equal(formatIdleThreshold(300), "5 minutes");
assert.equal(formatIdleThreshold(3600), "1 hour");

// The helper text has to name the consequence exactly: attended time pauses, the session does
// not stop and elapsed time does not.
const helper = idleThresholdHelperText(300);
assert.ok(helper.includes("5 minutes"));
assert.ok(helper.includes("attended time"));
assert.ok(helper.includes("Elapsed time keeps running"));

// The mapper accepts both wire spellings, and a settings payload written before 7.23 -- with
// no such key at all -- still produces a usable value.
assert.equal(mapSettings({ idleThresholdSecs: 600 }).idleThresholdSecs, 600);
assert.equal(mapSettings({ idle_threshold_secs: 600 }).idleThresholdSecs, 600);
assert.equal(mapSettings({}).idleThresholdSecs, DEFAULT_IDLE_THRESHOLD_SECS);

console.log("idleThreshold.test.ts passed");
