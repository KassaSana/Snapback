// Roadmap 2.16. Reading the delivery route the engine attaches to an alert event.
import assert from "node:assert/strict";

import { deliversInApp, parseAlertRoute } from "../src/alertDelivery";

// A route the engine actually emits: overlay only, detailed copy.
const overlayOnly = { delivery: { inApp: false, overlay: true, native: false, preview: "detailed" } };

{
  const route = parseAlertRoute(overlayOnly);
  assert.equal(route.inApp, false);
  assert.equal(route.overlay, true);
  assert.equal(route.native, false);
  assert.equal(route.preview, "detailed");
}

{
  // "Both", which the item requires to stay expressible.
  const route = parseAlertRoute({
    delivery: { inApp: true, overlay: true, native: true, preview: "generic" },
  });
  assert.equal(route.inApp, true);
  assert.equal(route.overlay, true);
  assert.equal(route.native, true);
  assert.equal(route.preview, "generic");
}

{
  // A missing delivery block means a bug, not an old payload -- the tick that emits these runs
  // in the same binary that renders them. It therefore fails *open*: an all-false default
  // would look exactly like a user who asked for silence, and the app would quietly stop
  // interrupting while appearing to work.
  assert.equal(deliversInApp({}), true);
  assert.equal(deliversInApp({ delivery: null }), true);
  assert.equal(deliversInApp(undefined), true);
  assert.equal(deliversInApp({ delivery: "nonsense" }), true);
}

{
  // A present-but-silent route is honoured. This is the case that must NOT be confused with
  // the fallback above.
  assert.equal(deliversInApp({ delivery: { inApp: false, overlay: false, native: false } }), false);
}

{
  // Unknown preview values fall back to detailed, matching the native enum's fallback
  // direction: an unreadable preference leaves the product saying what it always said.
  assert.equal(parseAlertRoute({ delivery: { preview: "holograph" } }).preview, "detailed");
  assert.equal(parseAlertRoute({ delivery: {} }).preview, "detailed");
}

{
  // Truthy-but-not-true values are not channels. The engine emits real booleans; anything else
  // arriving here is corruption, and guessing at it is how a silenced alert starts firing.
  const route = parseAlertRoute({ delivery: { inApp: 1, overlay: "yes", native: {} } });
  assert.equal(route.inApp, false);
  assert.equal(route.overlay, false);
  assert.equal(route.native, false);
}

console.log("alertDelivery.test.ts passed");
