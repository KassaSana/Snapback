import assert from "node:assert/strict";

import { buildAppRulePreview } from "../src/appRulePreview";

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

console.log("appRulePreview.test.ts passed");
