import assert from "node:assert/strict";

import {
  EVENT_TIMELINE_REFRESH_MS,
  shouldRefreshTimelineFromEvent,
} from "../src/timelineRefresh";

assert.equal(shouldRefreshTimelineFromEvent(null, 1_000), true);
assert.equal(
  shouldRefreshTimelineFromEvent(10_000, 10_000 + EVENT_TIMELINE_REFRESH_MS - 1),
  false,
);
assert.equal(
  shouldRefreshTimelineFromEvent(10_000, 10_000 + EVENT_TIMELINE_REFRESH_MS),
  true,
);
assert.equal(shouldRefreshTimelineFromEvent(10_000, 10_500, 250), true);

console.log("timelineRefresh.test.ts passed");
