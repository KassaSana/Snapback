import assert from "node:assert/strict";

import {
  activityDeletionIsWarning,
  activityDeletionMessage,
  activityDeletionRetainedNote,
  mapActivityDeletionResult,
} from "../src/activityDeletion";

const clean = {
  deleted: ["your recorded sessions, predictions, and captured windows", "training exports"],
  failed: [],
  retained: ["your settings", "the app log and any support bundles"],
  complete: true,
};

const partial = {
  deleted: ["your recorded sessions, predictions, and captured windows"],
  failed: ["pre-migration database backup (permission denied)"],
  retained: ["your settings"],
  complete: false,
};

// A complete erasure gets the flat claim, and only a complete one does.
assert.equal(
  activityDeletionMessage(clean),
  "All locally collected activity data was deleted.",
);
assert.equal(activityDeletionIsWarning(clean), false);

// THE RULE. "The UI never says 'permanently deleted' for a partial result." A stale export
// held open by another program leaves the database cleared and a copy behind; that is a
// legitimate outcome and it has to read as one.
{
  const message = activityDeletionMessage(partial);
  assert.ok(!message.includes("All locally collected activity data was deleted"));
  assert.ok(message.includes("could not be removed"));
  // It names what remains, so the user can go and deal with it.
  assert.ok(message.includes("pre-migration database backup"));
  // And it still says what *did* go, so a partial result is not mistaken for a total failure.
  assert.ok(message.includes("sessions are gone"));
  assert.equal(activityDeletionIsWarning(partial), true);
}

// Singular and plural, because "1 items could not be removed" undermines a message whose
// whole job is to be believed.
assert.ok(activityDeletionMessage(partial).includes("1 item could not"));
assert.ok(
  activityDeletionMessage({ ...partial, failed: ["a", "b"] }).includes("2 items could not"),
);

// The retained note names what was deliberately kept. Silence here is what made the old
// classification an assumption the user had to read the source to discover.
{
  const note = activityDeletionRetainedNote(clean);
  assert.ok(note);
  assert.ok(note.includes("your settings"));
  assert.ok(note.includes("the app log"));
  assert.equal(activityDeletionRetainedNote({ ...clean, retained: [] }), null);
}

// An unreadable payload must not be reported as a completed erasure — the failure mode of a
// tolerant mapper is exactly the false reassurance this item exists to prevent.
assert.equal(mapActivityDeletionResult(undefined).complete, true); // nothing failed, nothing done
assert.equal(mapActivityDeletionResult({ failed: ["x"] }).complete, false);
assert.equal(mapActivityDeletionResult({ failed: ["x"], complete: true }).complete, true);
assert.deepEqual(mapActivityDeletionResult({ deleted: "not an array" }).deleted, []);
assert.deepEqual(mapActivityDeletionResult({ deleted: [1, 2] }).deleted, ["1", "2"]);

console.log("activityDeletion.test.ts passed");
