import assert from "node:assert/strict";

import {
  buildAppRulePreview,
  isBroadAppRulePattern,
} from "../src/appRulePreview";

assert.equal(buildAppRulePreview("", "allow"), null);
assert.equal(buildAppRulePreview("   ", "block"), null);

assert.equal(
  buildAppRulePreview("discord", "allow"),
  'Preview: windows matching "discord" will be treated as on-task.',
);

assert.equal(
  buildAppRulePreview("youtube", "block"),
  'Preview: windows matching "youtube" will raise distraction scoring. This does not close or block apps.',
);

assert.equal(
  buildAppRulePreview(" notion ", "allow", " study group "),
  'Preview: windows matching "notion" will be treated as on-task. Note saved with rule: study group.',
);

// isBroadAppRulePattern: 1–2 chars (after trim) are broad; 3+ and empty are not.
assert.equal(isBroadAppRulePattern("e"), true);
assert.equal(isBroadAppRulePattern(" in "), true);
assert.equal(isBroadAppRulePattern("yt"), true);
assert.equal(isBroadAppRulePattern("git"), false, "3 chars is specific enough");
assert.equal(isBroadAppRulePattern(""), false, "empty is handled separately, not 'broad'");
assert.equal(isBroadAppRulePattern("   "), false);

// A very short pattern appends the broadness guardrail to the preview.
{
  const preview = buildAppRulePreview("e", "block");
  assert.ok(preview?.startsWith("Preview: windows matching \"e\" will raise distraction"));
  assert.ok(preview?.includes("may match many unrelated windows"));
}

// The guardrail comes after any note, and still fires with a note present.
{
  const preview = buildAppRulePreview("in", "allow", "inbox");
  assert.ok(preview?.includes("Note saved with rule: inbox."));
  assert.ok(preview?.includes("may match many unrelated windows"));
}

// A specific (3+ char) pattern gets no guardrail — regression guard so we
// don't start nagging on normal patterns.
assert.ok(!buildAppRulePreview("slack", "allow")?.includes("Heads up"));

console.log("appRulePreview.test.ts passed");
