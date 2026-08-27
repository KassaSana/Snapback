// Roadmap 2.16. Reading the delivery route the engine attaches to an alert event.
import assert from "node:assert/strict";

import {
  deliversInApp,
  formatTimeOfDay,
  parseAlertRoute,
  parseTimeOfDay,
  quietHoursSummary,
  routingSummary,
  snoozeRemainingLabel,
} from "../src/alertDelivery";

// A route the engine actually emits: overlay only, detailed copy.
const overlayOnly = {
  delivery: { inApp: false, overlay: true, native: false, preview: "detailed" },
};

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

{
  // Time-of-day parsing rejects rather than coerces: every wrong answer is a quiet range the
  // user did not choose and cannot see.
  assert.equal(parseTimeOfDay("22:30"), 22 * 60 + 30);
  assert.equal(parseTimeOfDay("07:00"), 7 * 60);
  assert.equal(parseTimeOfDay(" 7:05 "), 7 * 60 + 5);
  assert.equal(parseTimeOfDay("00:00"), 0);
  assert.equal(parseTimeOfDay("25:00"), null);
  assert.equal(parseTimeOfDay("22:60"), null);
  assert.equal(parseTimeOfDay("7"), null);
  assert.equal(parseTimeOfDay(""), null);
  assert.equal(parseTimeOfDay("22:3"), null);
}

{
  assert.equal(formatTimeOfDay(22 * 60 + 30), "22:30");
  assert.equal(formatTimeOfDay(0), "00:00");
  assert.equal(formatTimeOfDay(7 * 60), "07:00");
  // Round-trips with the parser, which is the property that keeps an edit from drifting.
  for (const text of ["00:00", "07:00", "13:45", "22:30", "23:59"]) {
    assert.equal(formatTimeOfDay(parseTimeOfDay(text)!), text);
  }
}

{
  // The wrap has to be said out loud: "22:00 to 07:00" alone reads to plenty of people as
  // fifteen hours of silence starting in the morning.
  const wrapping = quietHoursSummary({
    quietHoursEnabled: true,
    quietHoursStartMin: 22 * 60,
    quietHoursEndMin: 7 * 60,
  });
  assert.ok(wrapping.includes("the next day"));
  assert.ok(wrapping.includes("22:00"));
  assert.ok(wrapping.includes("07:00"));

  const sameDay = quietHoursSummary({
    quietHoursEnabled: true,
    quietHoursStartMin: 13 * 60,
    quietHoursEndMin: 14 * 60,
  });
  assert.ok(!sameDay.includes("the next day"));

  // Mirrors minute_in_quiet_range: equal start and end is an empty range, not a full day.
  const empty = quietHoursSummary({
    quietHoursEnabled: true,
    quietHoursStartMin: 9 * 60,
    quietHoursEndMin: 9 * 60,
  });
  assert.ok(empty.includes("nothing is silenced"));

  const off = quietHoursSummary({
    quietHoursEnabled: false,
    quietHoursStartMin: 22 * 60,
    quietHoursEndMin: 7 * 60,
  });
  assert.ok(off.includes("any time"));
}

{
  assert.equal(routingSummary([]), "Will not interrupt you.");
  assert.equal(routingSummary(["overlay"]), "Shows as: Overlay card.");
  assert.equal(
    routingSummary(["overlay", "native"]),
    "Shows as: Overlay card and System notification.",
  );
  assert.equal(
    routingSummary(["native", "inApp", "overlay"]),
    "Shows as: In the app, Overlay card and System notification.",
  );
}

{
  // Rounded up: a countdown reading "0 min left" while still silencing looks like a bug.
  assert.equal(snoozeRemainingLabel(30_000), "1 min left");
  assert.equal(snoozeRemainingLabel(60_000), "1 min left");
  assert.equal(snoozeRemainingLabel(61_000), "2 min left");
  assert.equal(snoozeRemainingLabel(18 * 60_000), "18 min left");
  assert.equal(snoozeRemainingLabel(0), "");
  assert.equal(snoozeRemainingLabel(-1), "");
}

console.log("alertDelivery.test.ts passed");
