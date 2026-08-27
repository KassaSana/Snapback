import assert from "node:assert/strict";

import {
  ALERT_ACTIONS,
  alertDestination,
  isAlertAction,
  NO_DESTINATION,
} from "../src/alertDestination";

// Roadmap 2.16's action-routing half: where a clicked native alert lands in the app.

{
  // The wire format is not ours to choose. These strings must match alert_action_as_str in
  // src/app/alert_routing.hpp exactly — spaces and all — or every click silently becomes an
  // unknown destination and does nothing but raise the window.
  assert.deepEqual(
    [...ALERT_ACTIONS],
    ["return to work", "open session composer", "open pomodoro"],
  );
}

{
  const composer = alertDestination({ action: "open session composer", alertId: 7 });
  assert.equal(composer.surface, "now");
  assert.equal(composer.focus, "session");

  const pomodoro = alertDestination({ action: "open pomodoro", alertId: 8 });
  assert.equal(pomodoro.surface, "now");
  assert.equal(pomodoro.focus, "pomodoro");
}

{
  // Both land on Now, which is not a placeholder — it is where a session is started and where
  // the Pomodoro card lives. What separates them is the region, which is why `focus` exists
  // rather than the surface alone being the answer.
  const composer = alertDestination({ action: "open session composer" });
  const pomodoro = alertDestination({ action: "open pomodoro" });
  assert.equal(composer.surface, pomodoro.surface);
  assert.notEqual(composer.focus, pomodoro.focus);
}

{
  // "return to work" is handled natively by restore_snapback_target, which raises *another
  // application's* window. Navigating for it would pull the user back to Snapback at the exact
  // moment the native side was sending them away from it.
  const back = alertDestination({ action: "return to work" });
  assert.equal(back.focus, null);
}

{
  // Total, in every direction. This runs inside a host event listener, where a thrown
  // exception has no user-visible failure mode: the click just silently does nothing.
  for (const bad of [
    undefined,
    null,
    {},
    { action: null },
    { action: 42 },
    { action: "" },
    { action: "open the pod bay doors" },
    { action: "Open Pomodoro" }, // case matters; this is a wire format, not a label
    "open pomodoro", // the payload, not the action, is what is passed in
    [],
  ]) {
    assert.deepEqual(alertDestination(bad), NO_DESTINATION, `input: ${JSON.stringify(bad)}`);
  }
}

{
  // The fallback brings the window forward and changes nothing else — a destination this build
  // does not understand is not a licence to guess at a screen nobody chose.
  assert.equal(NO_DESTINATION.focus, null);
}

{
  assert.equal(isAlertAction("open pomodoro"), true);
  assert.equal(isAlertAction("open pomodoros"), false);
  assert.equal(isAlertAction(undefined), false);
  assert.equal(isAlertAction(7), false);
}

console.log("alertDestination tests passed");
