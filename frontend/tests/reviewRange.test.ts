import assert from "node:assert/strict";

import {
  customRangeSinceInstant,
  readStoredReviewRange,
  REVIEW_RANGE_STORAGE_KEY,
  toReviewWindowRequest,
  writeStoredReviewRange,
} from "../src/reviewRange";

const storage = new Map<string, string>();
(globalThis as { localStorage?: Storage }).localStorage = {
  getItem: (key) => storage.get(key) ?? null,
  setItem: (key, value) => {
    storage.set(key, value);
  },
  removeItem: (key) => {
    storage.delete(key);
  },
  clear: () => storage.clear(),
  key: () => null,
  length: 0,
};

assert.deepEqual(toReviewWindowRequest({ preset: "today" }), { window: "day" });
assert.deepEqual(toReviewWindowRequest({ preset: "7d" }), { window: "7d" });
assert.deepEqual(toReviewWindowRequest({ preset: "custom", since: "2026-08-01" }), {
  window: "custom",
  // Still a string, deliberately. `since` is the one instant that crosses the bridge as text:
  // it is the range the user picked, and ADR-0007 parses it once at the C++ edge in
  // review_window_cutoff rather than making the UI compute epoch milliseconds.
  since: customRangeSinceInstant("2026-08-01"),
});
// The picked day starts at *local* midnight. It used to be sent as UTC midnight, which is a
// different instant everywhere but Greenwich. Asserted against Date's own local parse so the
// test holds in any zone the runner is in.
assert.equal(
  Date.parse(customRangeSinceInstant("2026-08-01")),
  new Date(2026, 7, 1, 0, 0, 0).getTime(),
);
assert.match(customRangeSinceInstant("2026-08-01"), /Z$/);
// Garbage falls back to the old literal rather than sending "Invalid Date" over the bridge;
// the C++ edge then rejects it with a clear error.
assert.equal(customRangeSinceInstant("not-a-date"), "not-a-dateT00:00:00Z");

writeStoredReviewRange({ preset: "30d" });
assert.equal(storage.get(REVIEW_RANGE_STORAGE_KEY), JSON.stringify({ preset: "30d" }));
assert.deepEqual(readStoredReviewRange(), { preset: "30d" });

console.log("reviewRange.test.ts — ok");
