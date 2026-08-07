import assert from "node:assert/strict";

import { myDataExportIsIncomplete, myDataExportMessage } from "../src/myDataExport";
import type { MyDataExportResult } from "../src/api";

const result = (over: Partial<MyDataExportResult> = {}): MyDataExportResult => ({
  outputPath: "C:\\data\\snapback_my_data.md",
  sessionCount: 12,
  windowCount: 340,
  episodeCount: 7,
  omittedSessions: 0,
  omittedWindows: 0,
  truncated: false,
  checksum: "deadbeefdeadbeef",
  ...over,
});

// A complete export says so. The feature's whole purpose is "here is everything"; a message
// that only lists counts leaves the reader to wonder what is missing.
{
  const message = myDataExportMessage(result());
  assert.ok(message.includes("12 sessions"));
  assert.ok(message.includes("340 captured windows"));
  assert.ok(message.includes("7 interruptions"));
  assert.ok(message.includes("complete history"));
  assert.ok(message.includes("nothing was left out"));
  assert.equal(myDataExportIsIncomplete(result()), false);
}

// THE BUG. Omitting windows from an included session used to report as complete, because the
// flag was set from the session cap alone. Whichever record type was dropped must be named.
{
  const windowsOnly = result({ omittedWindows: 42, truncated: true });
  const message = myDataExportMessage(windowsOnly);
  assert.ok(!message.includes("complete history"));
  assert.ok(!message.includes("nothing was left out"));
  assert.ok(message.includes("42 captured windows could not be included"));
  assert.equal(myDataExportIsIncomplete(windowsOnly), true);
}

{
  const sessionsOnly = result({ omittedSessions: 3, truncated: true });
  assert.ok(myDataExportMessage(sessionsOnly).includes("3 sessions could not be included"));
}

// Both, named separately, so the user knows the shape of what is missing rather than only
// that something is.
{
  const both = result({ omittedSessions: 3, omittedWindows: 42, truncated: true });
  const message = myDataExportMessage(both);
  assert.ok(message.includes("3 sessions"));
  assert.ok(message.includes("42 captured windows"));
  assert.ok(message.includes(" and "));
}

// Singulars, because "1 sessions" undermines a sentence whose job is to be believed.
{
  const one = result({ sessionCount: 1, windowCount: 1, episodeCount: 1 });
  const message = myDataExportMessage(one);
  assert.ok(message.includes("1 session,"));
  assert.ok(message.includes("1 captured window,"));
  assert.ok(message.includes("1 interruption"));
}

// An empty history still reports a path and a complete claim — the file is a real document
// that says there is nothing, not a failed write.
{
  const empty = result({ sessionCount: 0, windowCount: 0, episodeCount: 0 });
  const message = myDataExportMessage(empty);
  assert.ok(message.includes("0 sessions"));
  assert.ok(message.includes("complete history"));
}

// The path is always named, so the user can go and read what was written.
assert.ok(myDataExportMessage(result()).includes("snapback_my_data.md"));

console.log("myDataExport.test.ts passed");
