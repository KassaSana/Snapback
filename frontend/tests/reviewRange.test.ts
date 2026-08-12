import assert from "node:assert/strict";

import {
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
  since: "2026-08-01T00:00:00Z",
});

writeStoredReviewRange({ preset: "30d" });
assert.equal(storage.get(REVIEW_RANGE_STORAGE_KEY), JSON.stringify({ preset: "30d" }));
assert.deepEqual(readStoredReviewRange(), { preset: "30d" });

console.log("reviewRange.test.ts — ok");
