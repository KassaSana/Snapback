import assert from "node:assert/strict";

import type { AppRuleRecord, ContextSnapshot } from "../src/api";
import {
  WORK_APP_TEACH_DONE_KEY,
  WORK_APP_TEACH_LIMIT,
  clearWorkAppTeachComplete,
  readWorkAppTeachComplete,
  shouldShowWorkAppTeach,
  workAppCandidates,
  writeWorkAppTeachComplete,
} from "../src/workAppTeach";
import { MemoryStorage } from "./memoryStorage";

const snap = (
  appName: string,
  windowTitle = "",
): Pick<ContextSnapshot, "appName" | "windowTitle"> => ({
  appName,
  windowTitle,
});

const rule = (pattern: string, ruleType: "allow" | "block" = "allow"): AppRuleRecord => ({
  id: 1,
  pattern,
  ruleType,
  note: null,
  createdAtMs: 0,
  updatedAtMs: 0,
});

assert.deepEqual(workAppCandidates([], []), []);

// Empty names are capture noise, not something a user can teach.
assert.deepEqual(workAppCandidates([snap(""), snap("   ")], []), []);

const ranked = workAppCandidates(
  [
    snap("Discord", "#general"),
    snap("Code", "main.cpp"),
    snap("Discord", "#random"),
    snap("Chrome"),
  ],
  [],
);
assert.deepEqual(
  ranked.map((row) => row.appName),
  ["Discord", "Chrome", "Code"],
);
assert.equal(ranked[0]?.sampleCount, 2);
assert.equal(ranked[0]?.exampleTitle, "#general");

// Rules already covering an app drop it — the card asks only what is still unknown.
assert.deepEqual(
  workAppCandidates([snap("Discord"), snap("Code")], [rule("disc")]).map((row) => row.appName),
  ["Code"],
);

const many = Array.from({ length: 8 }, (_, index) => snap(`App${index}`));
assert.equal(workAppCandidates(many, []).length, WORK_APP_TEACH_LIMIT);

assert.equal(shouldShowWorkAppTeach({ dismissed: false, candidates: ranked }), true);
assert.equal(shouldShowWorkAppTeach({ dismissed: true, candidates: ranked }), false);
assert.equal(shouldShowWorkAppTeach({ dismissed: false, candidates: [] }), false);

const storage = new MemoryStorage();
assert.equal(readWorkAppTeachComplete(storage), false);
writeWorkAppTeachComplete(storage);
assert.equal(storage.getItem(WORK_APP_TEACH_DONE_KEY), "true");
assert.equal(readWorkAppTeachComplete(storage), true);
clearWorkAppTeachComplete(storage);
assert.equal(readWorkAppTeachComplete(storage), false);

// Disabled / missing storage must not throw — the card reappearing is the failure mode.
assert.equal(readWorkAppTeachComplete(null), false);
writeWorkAppTeachComplete(null);
clearWorkAppTeachComplete(null);

console.log("workAppTeach.test.ts passed");
